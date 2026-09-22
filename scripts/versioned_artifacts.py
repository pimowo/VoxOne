Import("env")

import csv
import hashlib
import re
import shutil
import subprocess
from pathlib import Path

from SCons.Script import COMMAND_LINE_TARGETS


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
VERSION_HEADER = PROJECT_DIR / "src" / "core" / "version.h"
VERSION_PATTERN = re.compile(
    r'^\s*#define\s+YOVOXONE_VERSION\s+"(\d+\.\d+\.\d+)"\s*$',
    re.MULTILINE,
)


def read_product_version():
    match = VERSION_PATTERN.search(VERSION_HEADER.read_text(encoding="utf-8"))
    if not match:
        raise RuntimeError(
            "YOVOXONE_VERSION must be a SemVer value in src/core/version.h"
        )
    return match.group(1)


PRODUCT_VERSION = read_product_version()
BUILD_DIR = Path(env.subst("$BUILD_DIR"))
FIRMWARE_IMAGE = BUILD_DIR / f"{env.subst('$PROGNAME')}.bin"
SPIFFS_IMAGE = BUILD_DIR / "spiffs.bin"
FULL_IMAGE = BUILD_DIR / f"yoVoxOne-{PRODUCT_VERSION}-full.bin"


def versioned_copy_action(artifact_name):
    def copy_versioned_artifact(target, source, env):
        source_path = Path(str(target[0]))
        destination = source_path.with_name(
            f"yoVoxOne-{PRODUCT_VERSION}-{artifact_name}.bin"
        )
        shutil.copy2(source_path, destination)
        print(f"Versioned artifact: {destination}")

    return copy_versioned_artifact


env.AddPostAction(
    "$BUILD_DIR/${PROGNAME}.bin", versioned_copy_action("firmware")
)
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

    full_data = output.read_bytes()
    if len(full_data) != maximum_size:
        raise RuntimeError(
            f"Full image size is {len(full_data)}, expected {maximum_size}"
        )

    for offset, image in FLASH_COMPONENTS:
        source_data = image.read_bytes()
        embedded_data = full_data[offset : offset + len(source_data)]
        if embedded_data != source_data:
            raise RuntimeError(
                f"Full image fragment differs at {hex(offset)}: {image}"
            )
        print(
            f"Verified {hex(offset)} {image.name}: "
            f"{len(source_data)} bytes, SHA-256 {sha256(source_data)}"
        )

    shutil.copy2(
        FIRMWARE_IMAGE,
        BUILD_DIR / f"yoVoxOne-{PRODUCT_VERSION}-firmware.bin",
    )
    shutil.copy2(
        SPIFFS_IMAGE,
        BUILD_DIR / f"yoVoxOne-{PRODUCT_VERSION}-spiffs.bin",
    )
    print(
        f"Full flash image: {output}, {len(full_data)} bytes, "
        f"SHA-256 {sha256(full_data)}"
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