Import("env")

import csv
import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

from SCons.Script import COMMAND_LINE_TARGETS


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
SCRIPTS_DIR = PROJECT_DIR / "scripts"
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from full_image_validation import (  # noqa: E402
    ValidationError,
    context_from_environment,
    expected_component_offsets,
    partition_contract,
    validate_component_set,
    validate_full_image,
    write_firmware_manifest,
    write_json,
)


VERSION_HEADER = PROJECT_DIR / "src" / "core" / "version.h"
VERSION_PATTERN = re.compile(
    r'^\s*#define\s+VOXONE_VERSION\s+"(\d+\.\d+\.\d+)"\s*$',
    re.MULTILINE,
)
CHANNEL_PATTERN = re.compile(
    r'^\s*#define\s+VOXONE_BUILD_CHANNEL\s+"([A-Za-z0-9][A-Za-z0-9._+-]*)"\s*$',
    re.MULTILINE,
)


def read_product_version():
    match = VERSION_PATTERN.search(VERSION_HEADER.read_text(encoding="utf-8"))
    if not match:
        raise RuntimeError(
            "VOXONE_VERSION must be a SemVer value in src/core/version.h"
        )
    return match.group(1)


PRODUCT_VERSION = read_product_version()
VERSION_TEXT = VERSION_HEADER.read_text(encoding="utf-8")
CHANNEL_MATCH = CHANNEL_PATTERN.search(VERSION_TEXT)
if not CHANNEL_MATCH:
    raise RuntimeError("VOXONE_BUILD_CHANNEL is missing from src/core/version.h")
BUILD_CHANNEL = CHANNEL_MATCH.group(1)
BUILD_DIR = Path(env.subst("$BUILD_DIR"))
FIRMWARE_IMAGE = BUILD_DIR / f"{env.subst('$PROGNAME')}.bin"
FIRMWARE_ELF = BUILD_DIR / f"{env.subst('$PROGNAME')}.elf"
FIRMWARE_MANIFEST = BUILD_DIR / "firmware.validation.json"
SPIFFS_IMAGE = BUILD_DIR / "spiffs.bin"
FULL_IMAGE = BUILD_DIR / f"VoxOne-{PRODUCT_VERSION}-full.bin"
FULL_IMAGE_MANIFEST = BUILD_DIR / f"VoxOne-{PRODUCT_VERSION}-full.manifest.json"
BUILD_CONTEXT = context_from_environment(env, PRODUCT_VERSION, BUILD_CHANNEL)


def versioned_copy_action(artifact_name):
    def copy_versioned_artifact(target, source, env):
        source_path = Path(str(target[0]))
        destination = source_path.with_name(
            f"VoxOne-{PRODUCT_VERSION}-{artifact_name}.bin"
        )
        shutil.copy2(source_path, destination)
        print(f"Versioned artifact: {destination}")

    return copy_versioned_artifact


def validate_and_copy_firmware(target, source, env):
    source_path = Path(str(target[0]))
    manifest = write_firmware_manifest(
        FIRMWARE_MANIFEST, source_path, FIRMWARE_ELF, BUILD_CONTEXT
    )
    destination = source_path.with_name(
        f"VoxOne-{PRODUCT_VERSION}-firmware.bin"
    )
    shutil.copy2(source_path, destination)
    print(
        f"Firmware validation manifest: {FIRMWARE_MANIFEST}, "
        f"profile={manifest['context']['profile']} "
        f"security={manifest['context']['security']}"
    )
    print(f"Versioned artifact: {destination}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", validate_and_copy_firmware)
env.AddPostAction(
    "$BUILD_DIR/spiffs.bin", versioned_copy_action("spiffs")
)


def read_partition(partition_name):
    partitions_file = PROJECT_DIR / env.GetProjectOption(
        "board_build.partitions"
    )
    with partitions_file.open(encoding="utf-8", newline="") as handle:
        for row in csv.reader(
            line for line in handle if not line.lstrip().startswith("#")
        ):
            if len(row) >= 5 and row[0].strip() == partition_name:
                return int(row[3].strip(), 0), int(row[4].strip(), 0)
    raise RuntimeError(f"Partition {partition_name!r} not found")


def flash_components():
    components = [
        (int(str(offset), 0), Path(env.subst(str(image))))
        for offset, image in env["FLASH_EXTRA_IMAGES"]
    ]
    components.append((int(env["ESP32_APP_OFFSET"], 0), FIRMWARE_IMAGE))
    spiffs_offset, _ = read_partition("spiffs")
    components.append((spiffs_offset, SPIFFS_IMAGE))
    return sorted(components)


FLASH_COMPONENTS = flash_components()


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def build_full_image(target, source, env):
    output = Path(str(target[0]))
    flash_mode = env.subst("${__get_board_flash_mode(__env__)}")
    flash_frequency = env.subst("${__get_board_f_image(__env__)}")
    flash_size = env.BoardConfig().get("upload.flash_size")
    size_match = re.fullmatch(r"(\d+)(KB|MB)", flash_size)
    if not size_match:
        raise RuntimeError(f"Unsupported flash size: {flash_size}")
    multiplier = 1024 if size_match.group(2) == "KB" else 1024 * 1024
    maximum_size = int(size_match.group(1)) * multiplier

    for _, image in FLASH_COMPONENTS:
        if not image.is_file():
            raise RuntimeError(f"Required image is missing: {image}")

    partitions_file = PROJECT_DIR / env.GetProjectOption(
        "board_build.partitions"
    )
    partitions = partition_contract(partitions_file)
    expected_offsets = expected_component_offsets(
        BUILD_CONTEXT["profile"], partitions
    )
    by_offset = {offset: image for offset, image in FLASH_COMPONENTS}
    if len(by_offset) != len(FLASH_COMPONENTS):
        raise RuntimeError("Flash component offsets are not unique")
    if set(by_offset) != set(expected_offsets.values()):
        raise RuntimeError(
            "Flash component offsets do not match the VoxOne partition contract"
        )
    components = {
        label: by_offset[offset] for label, offset in expected_offsets.items()
    }
    mkspiffs_value = env.subst("$MKFSTOOL")
    mkspiffs = Path(mkspiffs_value)
    if not mkspiffs.is_file():
        mkspiffs = None
    try:
        validation = validate_component_set(
            BUILD_CONTEXT,
            components,
            FIRMWARE_ELF,
            FIRMWARE_MANIFEST,
            partitions_file,
            Path(env.subst("$PROJECT_DATA_DIR")),
            BUILD_DIR,
            mkspiffs,
        )
    except ValidationError as error:
        raise RuntimeError(f"Full image component validation failed: {error}") from error

    command = [
        env.subst("$PYTHONEXE"),
        env.subst("$UPLOADER"),
        "--chip",
        env.BoardConfig().get("build.mcu"),
        "merge_bin",
        "--output",
        str(output),
        "--fill-flash-size",
        flash_size,
    ]
    for offset, image in FLASH_COMPONENTS:
        command.extend((hex(offset), str(image)))

    print(f"Board flash parameters: {flash_mode}, {flash_frequency}, {flash_size}")

    subprocess.run(command, check=True)

    try:
        full_manifest = validate_full_image(output, validation, components)
    except ValidationError as error:
        raise RuntimeError(f"Full image validation failed: {error}") from error
    if full_manifest["fullImage"]["size"] != maximum_size:
        raise RuntimeError("Validated full image size differs from board flash size")
    write_json(FULL_IMAGE_MANIFEST, full_manifest)

    for label, offset in sorted(
        expected_offsets.items(), key=lambda item: item[1]
    ):
        image = components[label]
        source_data = image.read_bytes()
        print(
            f"Verified {label} at {hex(offset)} {image.name}: "
            f"{len(source_data)} bytes, SHA-256 {sha256(source_data)}"
        )

    shutil.copy2(
        FIRMWARE_IMAGE,
        BUILD_DIR / f"VoxOne-{PRODUCT_VERSION}-firmware.bin",
    )
    shutil.copy2(
        SPIFFS_IMAGE,
        BUILD_DIR / f"VoxOne-{PRODUCT_VERSION}-spiffs.bin",
    )
    print(
        f"Full flash image: {output}, {full_manifest['fullImage']['size']} bytes, "
        f"SHA-256 {full_manifest['fullImage']['sha256']}"
    )
    print(
        f"Full image manifest: {FULL_IMAGE_MANIFEST}, "
        f"artifactSetId={full_manifest['artifactSetId']}"
    )

if "fullimage" in COMMAND_LINE_TARGETS:
    spiffs_target = env.DataToBin(
        str(BUILD_DIR / "spiffs"), env.subst("$PROJECT_DATA_DIR")
    )
    env.NoCache(spiffs_target)
    env.AlwaysBuild(spiffs_target)

    full_sources = []
    for _, image in FLASH_COMPONENTS:
        if image == SPIFFS_IMAGE:
            full_sources.extend(spiffs_target)
        else:
            full_sources.append(str(image))

    full_image_target = env.Command(
        str(FULL_IMAGE), full_sources, build_full_image
    )
    env.AlwaysBuild(full_image_target)
    env.AddCustomTarget(
        name="fullimage",
        dependencies=full_image_target,
        actions=[],
        title="Build complete flash image",
        description="Build firmware, SPIFFS and a validated recovery image",
    )
