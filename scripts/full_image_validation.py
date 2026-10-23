"""Fail-closed validation for VoxOne complete flash image components."""

from __future__ import annotations

import csv
import hashlib
import json
import re
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from www_asset_manifest import ManifestError, parse_manifest, verify_assets


FIRMWARE_MANIFEST_SCHEMA = "voxone-firmware-validation-v1"
FULL_IMAGE_MANIFEST_SCHEMA = "voxone-full-image-v1"
PARTITION_BINARY_SIZE = 0xC00
PARTITION_ENTRY_SIZE = 32
PARTITION_MAGIC = 0x50AA
PARTITION_MD5_MAGIC = 0xEBEB
ESP_IMAGE_MAGIC = 0xE9
ESP_IMAGE_CHECKSUM_SEED = 0xEF
ESP_APP_DESC_MAGIC = 0xABCD5432
ESP_APP_DESC_SIZE = 256
ESP_APP_DESC_ELF_SHA_OFFSET = 144


class ValidationError(RuntimeError):
    """A component cannot safely be included in a complete flash image."""


PROFILE_CONTRACTS: dict[str, dict[str, Any]] = {
    "W0": {
        "environment": "w0",
        "mcu": "esp32",
        "chipId": 0,
        "flashSize": 4 * 1024 * 1024,
        "flashMode": "dio",
        "flashFrequency": "40m",
        "bootloaderOffset": 0x1000,
    },
    "M0": {
        "environment": "m0",
        "mcu": "esp32s3",
        "chipId": 9,
        "flashSize": 4 * 1024 * 1024,
        "flashMode": "dio",
        "flashFrequency": "80m",
        "bootloaderOffset": 0,
    },
    "S0": {
        "environment": "s0",
        "mcu": "esp32s3",
        "chipId": 9,
        "flashSize": 4 * 1024 * 1024,
        "flashMode": "dio",
        "flashFrequency": "80m",
        "bootloaderOffset": 0,
    },
    "DIN0": {
        "environment": "din0",
        "mcu": "esp32s3",
        "chipId": 9,
        "flashSize": 4 * 1024 * 1024,
        "flashMode": "dio",
        "flashFrequency": "80m",
        "bootloaderOffset": 0,
    },
    "L0": {
        "environment": "l0",
        "mcu": "esp32s3",
        "chipId": 9,
        "flashSize": 16 * 1024 * 1024,
        "flashMode": "dio",
        "flashFrequency": "80m",
        "bootloaderOffset": 0,
    },
}

PROFILE_DEFINE_NAMES = {
    "W0": "VOXONE_PROFILE_W0",
    "M0": "VOXONE_PROFILE_M0",
    "S0": "VOXONE_PROFILE_S0",
    "DIN0": "VOXONE_PROFILE_DIN0",
    "L0": "VOXONE_PROFILE_L0",
}

PARTITION_TYPES = {"app": 0x00, "data": 0x01}
PARTITION_SUBTYPES = {
    ("data", "ota"): 0x00,
    ("data", "nvs"): 0x02,
    ("data", "spiffs"): 0x82,
    ("app", "ota_0"): 0x10,
    ("app", "ota_1"): 0x11,
}
FLASH_MODE_CODES = {"qio": 0, "qout": 1, "dio": 2, "dout": 3}
FLASH_FREQUENCY_CODES = {"40m": 0, "26m": 1, "20m": 2, "80m": 0xF}
FLASH_SIZE_CODES = {
    1 * 1024 * 1024: 0,
    2 * 1024 * 1024: 1,
    4 * 1024 * 1024: 2,
    8 * 1024 * 1024: 3,
    16 * 1024 * 1024: 4,
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_flash_size(value: str) -> int:
    match = re.fullmatch(r"(\d+)(KB|MB)", value, re.IGNORECASE)
    if not match:
        raise ValidationError(f"unsupported flash size: {value}")
    multiplier = 1024 if match.group(2).upper() == "KB" else 1024 * 1024
    return int(match.group(1)) * multiplier


def normalize_context(context: dict[str, Any], require_standard: bool) -> dict[str, Any]:
    profile = str(context.get("profile", "")).upper()
    if profile not in PROFILE_CONTRACTS:
        raise ValidationError(f"unsupported VoxOne profile: {profile or '<empty>'}")
    contract = PROFILE_CONTRACTS[profile]
    normalized = {
        "profile": profile,
        "pioEnvironment": str(context.get("pioEnvironment", "")),
        "mcu": str(context.get("mcu", "")).lower(),
        "flashSize": int(context.get("flashSize", 0)),
        "flashMode": str(context.get("flashMode", "")).lower(),
        "flashFrequency": str(context.get("flashFrequency", "")).lower(),
        "version": str(context.get("version", "")),
        "channel": str(context.get("channel", "")),
        "buildIdentity": str(context.get("buildIdentity", "")),
        "security": str(context.get("security", "")).lower(),
    }
    for key in ("mcu", "flashSize", "flashMode", "flashFrequency"):
        if normalized[key] != contract[key]:
            raise ValidationError(
                f"{profile} {key} mismatch: expected {contract[key]}, "
                f"got {normalized[key]}"
            )
    if require_standard and normalized["pioEnvironment"] != contract["environment"]:
        raise ValidationError(
            f"{profile} complete image requires PlatformIO environment "
            f"{contract['environment']}, got {normalized['pioEnvironment']}"
        )
    if not re.fullmatch(r"\d+\.\d+\.\d+", normalized["version"]):
        raise ValidationError("firmware version is not SemVer")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,31}", normalized["channel"]):
        raise ValidationError("firmware channel is invalid")
    if not re.fullmatch(r"(?:[0-9a-f]{7}(?:-dirty)?|unknown)", normalized["buildIdentity"]):
        raise ValidationError("build identity is invalid")
    if normalized["security"] not in ("production", "test"):
        raise ValidationError("SECURITY mode is neither production nor test")
    if require_standard and normalized["security"] != "production":
        raise ValidationError("complete images require production SECURITY")
    return normalized


def _value_enabled(value: Any) -> bool:
    if isinstance(value, str):
        return value.strip().strip('\\""') not in ("", "0")
    return bool(value)


def cpp_defines(definitions: Any) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for item in definitions or []:
        if isinstance(item, (tuple, list)) and len(item) == 2:
            result[str(item[0])] = item[1]
        else:
            result[str(item)] = 1
    return result


def context_from_environment(
    environment: Any,
    version: str,
    channel: str,
) -> dict[str, Any]:
    definitions = cpp_defines(environment.get("CPPDEFINES", []))
    profiles = [
        profile for profile, define in PROFILE_DEFINE_NAMES.items()
        if define in definitions and _value_enabled(definitions[define])
    ]
    if len(profiles) != 1:
        raise ValidationError(
            f"expected exactly one VoxOne profile define, got {profiles}"
        )
    test_security = _value_enabled(
        definitions.get("VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST", 0)
    )
    explicit_production = definitions.get("VOXONE_SECURITY_HTTP_ENFORCEMENT")
    production_security = not test_security
    if explicit_production is not None:
        production_security = _value_enabled(explicit_production)
    if production_security and test_security:
        raise ValidationError("SECURITY cannot be production and test")
    build_identity = environment.get("VOXONE_BUILD_SHA_VALUE")
    if not build_identity:
        raise ValidationError("build identity was not exported by build_identity.py")
    context = {
        "profile": profiles[0],
        "pioEnvironment": environment.subst("$PIOENV"),
        "mcu": environment.BoardConfig().get("build.mcu"),
        "flashSize": parse_flash_size(
            environment.BoardConfig().get("upload.flash_size")
        ),
        "flashMode": environment.subst(
            "${__get_board_flash_mode(__env__)}"
        ),
        "flashFrequency": environment.subst(
            "${__get_board_f_image(__env__)}"
        ),
        "version": version,
        "channel": channel,
        "buildIdentity": str(build_identity),
        "security": "production" if production_security else "test",
    }
    return normalize_context(context, require_standard=False)


def _validate_esp_image(
    data: bytes,
    profile: str,
    label: str,
    require_app_descriptor: bool,
) -> dict[str, Any]:
    contract = PROFILE_CONTRACTS[profile]
    if len(data) < 24 or data[0] != ESP_IMAGE_MAGIC:
        raise ValidationError(f"{label} has invalid ESP image magic")
    segment_count = data[1]
    if not 1 <= segment_count <= 16:
        raise ValidationError(f"{label} has invalid segment count")
    expected_mode = FLASH_MODE_CODES[contract["flashMode"]]
    if data[2] != expected_mode:
        raise ValidationError(
            f"{label} flash mode mismatch: expected {expected_mode}, got {data[2]}"
        )
    expected_size_code = FLASH_SIZE_CODES[contract["flashSize"]]
    expected_frequency_code = FLASH_FREQUENCY_CODES[contract["flashFrequency"]]
    if data[3] >> 4 != expected_size_code or data[3] & 0x0F != expected_frequency_code:
        raise ValidationError(f"{label} flash size/frequency header mismatch")
    chip_id = struct.unpack_from("<H", data, 12)[0]
    if chip_id != contract["chipId"]:
        raise ValidationError(
            f"{label} MCU mismatch: expected chip id {contract['chipId']}, got {chip_id}"
        )

    offset = 24
    checksum = ESP_IMAGE_CHECKSUM_SEED
    first_segment_data = None
    for index in range(segment_count):
        if offset + 8 > len(data):
            raise ValidationError(f"{label} has a truncated segment header")
        _, length = struct.unpack_from("<II", data, offset)
        offset += 8
        if length > len(data) - offset:
            raise ValidationError(f"{label} has truncated segment data")
        if index == 0:
            first_segment_data = offset
        for value in data[offset:offset + length]:
            checksum ^= value
        offset += length
    checksum_offset = offset | 0x0F
    if checksum_offset >= len(data) or data[checksum_offset] != checksum:
        raise ValidationError(f"{label} checksum mismatch")
    if data[23] == 0:
        raise ValidationError(f"{label} has no appended validation hash")
    hash_offset = checksum_offset + 1
    if hash_offset + 32 > len(data):
        raise ValidationError(f"{label} validation hash is truncated")
    expected_hash = hashlib.sha256(data[:hash_offset]).digest()
    if data[hash_offset:hash_offset + 32] != expected_hash:
        raise ValidationError(f"{label} validation hash mismatch")
    logical_size = hash_offset + 32
    if logical_size != len(data):
        raise ValidationError(
            f"{label} size does not match its ESP image: {len(data)} != {logical_size}"
        )

    report: dict[str, Any] = {
        "size": len(data),
        "sha256": sha256_bytes(data),
        "segments": segment_count,
        "chipId": chip_id,
    }
    if require_app_descriptor:
        if first_segment_data is None or first_segment_data + ESP_APP_DESC_SIZE > len(data):
            raise ValidationError("firmware app descriptor is truncated")
        magic = struct.unpack_from("<I", data, first_segment_data)[0]
        if magic != ESP_APP_DESC_MAGIC:
            raise ValidationError("firmware app descriptor magic mismatch")
        sha_offset = first_segment_data + ESP_APP_DESC_ELF_SHA_OFFSET
        report["embeddedElfSha256"] = data[sha_offset:sha_offset + 32].hex()
    return report


def _require_identity_tokens(data: bytes, context: dict[str, Any]) -> None:
    profile = context["profile"]
    expected = b"\0" + profile.encode("ascii") + b"\0"
    if expected not in data:
        raise ValidationError(f"firmware does not contain profile identity {profile}")
    for other in PROFILE_CONTRACTS:
        if other != profile and b"\0" + other.encode("ascii") + b"\0" in data:
            raise ValidationError(
                f"firmware contains conflicting profile identity {other}"
            )
    build = context["buildIdentity"].encode("ascii")
    if b"\0" + build + b"\0" not in data:
        raise ValidationError(
            f"firmware does not contain build identity {context['buildIdentity']}"
        )


def firmware_manifest_data(
    firmware: Path,
    firmware_elf: Path,
    context: dict[str, Any],
) -> dict[str, Any]:
    context = normalize_context(context, require_standard=False)
    try:
        firmware_data = firmware.read_bytes()
    except OSError as error:
        raise ValidationError(f"cannot read firmware: {error}") from error
    if not firmware_elf.is_file():
        raise ValidationError(f"firmware ELF is missing: {firmware_elf}")
    report = _validate_esp_image(
        firmware_data, context["profile"], "firmware", True
    )
    _require_identity_tokens(firmware_data, context)
    elf_hash = sha256_file(firmware_elf)
    if report["embeddedElfSha256"] != elf_hash:
        raise ValidationError("firmware embedded ELF SHA256 does not match firmware.elf")
    return {
        "schema": FIRMWARE_MANIFEST_SCHEMA,
        "context": context,
        "firmware": {
            "size": len(firmware_data),
            "sha256": report["sha256"],
            "embeddedElfSha256": report["embeddedElfSha256"],
        },
        "elf": {
            "size": firmware_elf.stat().st_size,
            "sha256": elf_hash,
        },
    }


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".new")
    temporary.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def write_firmware_manifest(
    manifest: Path,
    firmware: Path,
    firmware_elf: Path,
    context: dict[str, Any],
) -> dict[str, Any]:
    value = firmware_manifest_data(firmware, firmware_elf, context)
    write_json(manifest, value)
    return value


def validate_firmware_manifest(
    manifest: Path,
    firmware: Path,
    firmware_elf: Path,
    context: dict[str, Any],
) -> dict[str, Any]:
    if not manifest.is_file():
        raise ValidationError(f"firmware validation manifest is missing: {manifest}")
    try:
        actual = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValidationError(f"cannot read firmware validation manifest: {error}") from error
    expected = firmware_manifest_data(firmware, firmware_elf, context)
    if actual != expected:
        raise ValidationError(
            "firmware validation manifest does not match the current BIN, ELF and build context"
        )
    if actual.get("schema") != FIRMWARE_MANIFEST_SCHEMA:
        raise ValidationError("unsupported firmware validation manifest schema")
    return actual


def _partition_number(value: str) -> int:
    try:
        return int(value.strip(), 0)
    except ValueError as error:
        raise ValidationError(f"invalid partition number: {value}") from error


def partition_contract(csv_path: Path) -> list[dict[str, Any]]:
    partitions: list[dict[str, Any]] = []
    try:
        with csv_path.open(encoding="utf-8", newline="") as handle:
            rows = csv.reader(
                line for line in handle if not line.lstrip().startswith("#")
            )
            for row in rows:
                if not row or all(not value.strip() for value in row):
                    continue
                if len(row) < 5:
                    raise ValidationError("partition CSV row is incomplete")
                name, type_name, subtype_name = (value.strip() for value in row[:3])
                key = (type_name, subtype_name)
                if type_name not in PARTITION_TYPES or key not in PARTITION_SUBTYPES:
                    raise ValidationError(f"unsupported partition type: {type_name}/{subtype_name}")
                partitions.append({
                    "label": name,
                    "type": PARTITION_TYPES[type_name],
                    "subtype": PARTITION_SUBTYPES[key],
                    "offset": _partition_number(row[3]),
                    "size": _partition_number(row[4]),
                    "flags": _partition_number(row[5]) if len(row) > 5 and row[5].strip() else 0,
                })
    except OSError as error:
        raise ValidationError(f"cannot read partition CSV: {error}") from error
    if not partitions:
        raise ValidationError("partition CSV is empty")
    return partitions


def validate_partition_binary(
    partition_image: Path,
    expected: list[dict[str, Any]],
) -> dict[str, Any]:
    data = partition_image.read_bytes()
    if len(data) != PARTITION_BINARY_SIZE:
        raise ValidationError(
            f"partition image size mismatch: expected {PARTITION_BINARY_SIZE}, got {len(data)}"
        )
    entries: list[dict[str, Any]] = []
    for index in range(len(expected)):
        offset = index * PARTITION_ENTRY_SIZE
        magic, type_id, subtype, part_offset, size, raw_label, flags = struct.unpack_from(
            "<HBBII16sI", data, offset
        )
        if magic != PARTITION_MAGIC:
            raise ValidationError(f"partition entry {index} magic mismatch")
        label = raw_label.split(b"\0", 1)[0].decode("ascii", "strict")
        entries.append({
            "label": label,
            "type": type_id,
            "subtype": subtype,
            "offset": part_offset,
            "size": size,
            "flags": flags,
        })
    if entries != expected:
        raise ValidationError("partition image does not match partitions_yoradio.csv")
    trailer_offset = len(expected) * PARTITION_ENTRY_SIZE
    if struct.unpack_from("<H", data, trailer_offset)[0] != PARTITION_MD5_MAGIC:
        raise ValidationError("partition image has no MD5 trailer")
    if data[trailer_offset + 2:trailer_offset + 16] != b"\xff" * 14:
        raise ValidationError("partition MD5 trailer header is invalid")
    expected_md5 = hashlib.md5(data[:trailer_offset]).digest()
    if data[trailer_offset + 16:trailer_offset + 32] != expected_md5:
        raise ValidationError("partition image MD5 mismatch")
    if data[trailer_offset + 32:] != b"\xff" * (len(data) - trailer_offset - 32):
        raise ValidationError("partition image contains data after the MD5 trailer")
    return {"size": len(data), "sha256": sha256_bytes(data), "md5Valid": True}


def _partition_by_label(
    partitions: list[dict[str, Any]], label: str
) -> dict[str, Any]:
    matches = [item for item in partitions if item["label"] == label]
    if len(matches) != 1:
        raise ValidationError(f"partition layout must contain exactly one {label}")
    return matches[0]


def _ota_crc32(sequence: int) -> int:
    crc = 0
    for value in struct.pack("<I", sequence):
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return (~crc) & 0xFFFFFFFF


def validate_initial_otadata(path: Path, expected_size: int) -> dict[str, Any]:
    data = path.read_bytes()
    if expected_size != 0x2000 or len(data) != expected_size:
        raise ValidationError(
            f"initial otadata size mismatch: expected {expected_size}, got {len(data)}"
        )
    expected = bytearray(b"\xff" * expected_size)
    struct.pack_into("<I20sII", expected, 0, 1, b"\xff" * 20, 0xFFFFFFFF, _ota_crc32(1))
    struct.pack_into("<I", expected, 0x1000, 0)
    if data != bytes(expected):
        raise ValidationError("otadata is not the expected initial app0 selection image")
    return {"size": len(data), "sha256": sha256_bytes(data), "initialSlot": "app0"}


def find_mkspiffs(explicit: Path | None = None) -> Path:
    candidates = [explicit, Path(shutil.which("mkspiffs") or "")]
    candidates.extend([
        Path.home() / ".platformio/packages/tool-mkspiffs/mkspiffs_espressif32_arduino",
        Path.home() / ".platformio/packages/tool-mkspiffs/mkspiffs",
    ])
    for candidate in candidates:
        if candidate and str(candidate) not in ("", ".") and candidate.is_file():
            return candidate
    raise ValidationError("mkspiffs is unavailable for full image validation")


def _source_data_files(data_dir: Path) -> dict[str, Path]:
    return {
        "/" + path.relative_to(data_dir).as_posix(): path
        for path in data_dir.rglob("*")
        if path.is_file()
        and not any(part.startswith(".") for part in path.relative_to(data_dir).parts)
    }


def validate_spiffs_image(
    image: Path,
    expected_size: int,
    data_dir: Path,
    work_dir: Path,
    mkspiffs: Path | None = None,
) -> dict[str, Any]:
    data = image.read_bytes()
    if len(data) != expected_size:
        raise ValidationError(
            f"SPIFFS image size mismatch: expected {expected_size}, got {len(data)}"
        )
    if data in (b"\xff" * len(data), b"\x00" * len(data)):
        raise ValidationError("SPIFFS image is blank")
    executable = find_mkspiffs(mkspiffs)
    work_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="full-image-spiffs-", dir=work_dir
    ) as temporary:
        destination = Path(temporary) / "unpacked"
        result = subprocess.run(
            [
                str(executable), "-u", str(destination), "-b", "4096",
                "-p", "256", "-s", str(expected_size), str(image),
            ],
            check=False,
            capture_output=True,
        )
        if result.returncode:
            raise ValidationError("SPIFFS image cannot be unpacked")
        extracted = {
            "/" + path.relative_to(destination).as_posix(): path
            for path in destination.rglob("*") if path.is_file()
        }
        sources = _source_data_files(data_dir)
        if set(extracted) != set(sources):
            missing = sorted(set(sources) - set(extracted))
            extra = sorted(set(extracted) - set(sources))
            raise ValidationError(
                f"SPIFFS file set mismatch: missing={missing}, extra={extra}"
            )
        for name, source in sources.items():
            if extracted[name].read_bytes() != source.read_bytes():
                raise ValidationError(f"SPIFFS content differs from build data: {name}")
        manifest_path = destination / "www" / "assets.vxwm"
        try:
            parsed = parse_manifest(manifest_path.read_bytes())
            verify_assets(destination / "www", parsed)
        except (OSError, ManifestError) as error:
            raise ValidationError(f"SPIFFS WWW manifest validation failed: {error}") from error
    return {
        "size": len(data),
        "sha256": sha256_bytes(data),
        "files": len(sources),
    }


def expected_component_offsets(profile: str, partitions: list[dict[str, Any]]) -> dict[str, int]:
    contract = PROFILE_CONTRACTS[profile]
    return {
        "bootloader": contract["bootloaderOffset"],
        "partitions": 0x8000,
        "otadata": _partition_by_label(partitions, "otadata")["offset"],
        "firmware": _partition_by_label(partitions, "app0")["offset"],
        "spiffs": _partition_by_label(partitions, "spiffs")["offset"],
    }


def validate_component_set(
    context: dict[str, Any],
    components: dict[str, Path],
    firmware_elf: Path,
    firmware_manifest: Path,
    partition_csv: Path,
    data_dir: Path,
    work_dir: Path,
    mkspiffs: Path | None = None,
) -> dict[str, Any]:
    context = normalize_context(context, require_standard=True)
    required = {"bootloader", "partitions", "otadata", "firmware", "spiffs"}
    if set(components) != required:
        raise ValidationError(
            f"component set mismatch: expected {sorted(required)}, got {sorted(components)}"
        )
    for label, path in components.items():
        if not path.is_file():
            raise ValidationError(f"required {label} component is missing: {path}")

    profile = context["profile"]
    partitions = partition_contract(partition_csv)
    reports: dict[str, Any] = {}
    bootloader_data = components["bootloader"].read_bytes()
    reports["bootloader"] = _validate_esp_image(
        bootloader_data, profile, "bootloader", False
    )
    if len(bootloader_data) > 0x8000 - PROFILE_CONTRACTS[profile]["bootloaderOffset"]:
        raise ValidationError("bootloader overlaps the partition table")
    reports["partitions"] = validate_partition_binary(
        components["partitions"], partitions
    )
    otadata = _partition_by_label(partitions, "otadata")
    reports["otadata"] = validate_initial_otadata(
        components["otadata"], otadata["size"]
    )
    reports["firmwareManifest"] = validate_firmware_manifest(
        firmware_manifest, components["firmware"], firmware_elf, context
    )
    app0 = _partition_by_label(partitions, "app0")
    firmware_size = components["firmware"].stat().st_size
    if firmware_size > app0["size"]:
        raise ValidationError(
            f"firmware exceeds app0: {firmware_size} > {app0['size']}"
        )
    spiffs = _partition_by_label(partitions, "spiffs")
    reports["spiffs"] = validate_spiffs_image(
        components["spiffs"], spiffs["size"], data_dir, work_dir, mkspiffs
    )
    offsets = expected_component_offsets(profile, partitions)
    ranges = sorted(
        (offsets[label], offsets[label] + components[label].stat().st_size, label)
        for label in required
    )
    previous_end = 0
    for start, end, label in ranges:
        if start < previous_end:
            raise ValidationError(f"component {label} overlaps a previous component")
        if end > context["flashSize"]:
            raise ValidationError(f"component {label} extends beyond target flash")
        previous_end = end
    return {
        "context": context,
        "offsets": offsets,
        "partitions": partitions,
        "components": reports,
    }


def validate_full_image(
    full_image: Path,
    validation: dict[str, Any],
    components: dict[str, Path],
) -> dict[str, Any]:
    data = full_image.read_bytes()
    expected_size = validation["context"]["flashSize"]
    if len(data) != expected_size:
        raise ValidationError(
            f"full image size mismatch: expected {expected_size}, got {len(data)}"
        )
    ranges = []
    for label, offset in validation["offsets"].items():
        component = components[label].read_bytes()
        if data[offset:offset + len(component)] != component:
            raise ValidationError(f"full image differs from {label} at {hex(offset)}")
        ranges.append((offset, offset + len(component), label))
    cursor = 0
    for start, end, label in sorted(ranges):
        if any(value != 0xFF for value in data[cursor:start]):
            raise ValidationError(f"full image has unexpected data before {label}")
        cursor = end
    if any(value != 0xFF for value in data[cursor:]):
        raise ValidationError("full image has unexpected data after the final component")
    component_records = {
        label: {
            "offset": validation["offsets"][label],
            "size": path.stat().st_size,
            "sha256": sha256_file(path),
        }
        for label, path in sorted(components.items())
    }
    set_material = json.dumps(
        {
            "context": validation["context"],
            "components": component_records,
        },
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return {
        "schema": FULL_IMAGE_MANIFEST_SCHEMA,
        "context": validation["context"],
        "artifactSetId": sha256_bytes(set_material),
        "components": component_records,
        "fullImage": {
            "size": len(data),
            "sha256": sha256_bytes(data),
        },
    }
