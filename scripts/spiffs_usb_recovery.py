#!/usr/bin/env python3
"""Fail-closed, selective VoxOne SPIFFS recovery over an explicit USB port.

The normal workflow is deliberately split into separate operations:

* ``manifest`` validates a release SPIFFS image entirely offline.
* ``prepare`` reads identity, partition table and SPIFFS twice and writes a
  read-only restore plan.  It never writes the ESP flash.
* ``show-plan`` validates and displays the exact planned range offline.
* ``execute`` is the only flash-writing command.  It requires the plan's
  per-operation token plus two explicit confirmations, repeats every live
  preflight, performs exactly one target-only write, verifies it and captures
  a readback.  It never retries, resets or erases the whole flash.

No command guesses offsets or sizes.  They are derived from the profile's
version-controlled partition CSV and must exactly match two live reads.
"""

from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import gzip
import hashlib
import importlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from typing import Callable, NamedTuple
import zlib


try:
    flash_tools = importlib.import_module("m0_flash_backup")
except ModuleNotFoundError:
    _FLASH_TOOLS_SPEC = importlib.util.spec_from_file_location(
        "voxone_m0_flash_backup",
        Path(__file__).resolve().with_name("m0_flash_backup.py"),
    )
    if _FLASH_TOOLS_SPEC is None or _FLASH_TOOLS_SPEC.loader is None:
        raise RuntimeError("cannot load shared flash validation tools")
    flash_tools = importlib.util.module_from_spec(_FLASH_TOOLS_SPEC)
    _FLASH_TOOLS_SPEC.loader.exec_module(flash_tools)


ROOT = Path(__file__).resolve().parents[1]
PARTITION_TABLE_READ_SIZE = 0x1000
IMAGE_MANIFEST_SCHEMA = "voxone-spiffs-recovery-image-v1"
RESTORE_PLAN_SCHEMA = "voxone-spiffs-usb-restore-plan-v1"
EXECUTION_RESULT_SCHEMA = "voxone-spiffs-usb-execution-v1"
SPIFFS_TYPE = 0x01
SPIFFS_SUBTYPE = 0x82
MAX_DECOMPRESSED_ASSET_BYTES = 2 * 1024 * 1024
MAC_PATTERN = re.compile(r"(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}")
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
BUILD_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+\-]{0,63}")
REQUIRED_FORBIDDEN_OPERATIONS = {
    "erase_flash", "erase_region", "full_flash_restore", "bootloader_write",
    "partition_table_write", "otadata_write", "nvs_write", "app_write",
}


class RecoveryError(RuntimeError):
    """Expected fail-closed result with a safe diagnostic."""


class ProfileSpec(NamedTuple):
    name: str
    chip: str
    flash_size: int
    partition_csv: Path


PROFILE_SPECS = {
    "W0": ProfileSpec("W0", "esp32", 4 * 1024 * 1024,
                      ROOT / "partitions_yoradio.csv"),
    "M0": ProfileSpec("M0", "esp32s3", 4 * 1024 * 1024,
                      ROOT / "partitions_yoradio.csv"),
    "S0": ProfileSpec("S0", "esp32s3", 4 * 1024 * 1024,
                      ROOT / "partitions_yoradio.csv"),
    "DIN0": ProfileSpec("DIN0", "esp32s3", 4 * 1024 * 1024,
                        ROOT / "partitions_yoradio.csv"),
    "L0": ProfileSpec("L0", "esp32s3", 16 * 1024 * 1024,
                      ROOT / "partitions_yoradio.csv"),
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical_sha256(value: str, label: str) -> str:
    normalized = value.strip().lower()
    if not SHA256_PATTERN.fullmatch(normalized):
        raise RecoveryError(f"{label} must be exactly 64 lowercase SHA256 hex digits")
    return normalized


def canonical_profile(value: str) -> str:
    profile = value.strip().upper()
    if profile not in PROFILE_SPECS:
        raise RecoveryError(
            f"unsupported profile {value!r}; expected one of "
            f"{', '.join(PROFILE_SPECS)}"
        )
    return profile


def canonical_mac(value: str) -> str:
    normalized = value.strip().upper().replace("-", ":")
    if not MAC_PATTERN.fullmatch(normalized):
        raise RecoveryError("expected MAC must use six colon-separated octets")
    return normalized


def canonical_build(value: str) -> str:
    normalized = value.strip()
    if not BUILD_PATTERN.fullmatch(normalized):
        raise RecoveryError(
            "WWW build must be 1-64 safe characters: letters, digits, dot, "
            "underscore, plus or hyphen"
        )
    return normalized


def parse_size(value: str) -> int:
    try:
        parsed = int(value.strip(), 0)
    except ValueError as error:
        raise RecoveryError(f"invalid partition number {value!r}") from error
    if parsed < 0:
        raise RecoveryError("partition numbers cannot be negative")
    return parsed


PARTITION_TYPES = {"app": 0x00, "data": 0x01}
PARTITION_SUBTYPES = {
    ("data", "ota"): 0x00,
    ("data", "nvs"): 0x02,
    ("data", "spiffs"): SPIFFS_SUBTYPE,
    ("app", "ota_0"): 0x10,
    ("app", "ota_1"): 0x11,
}


def partition_code(value: str, mapping: dict[str, int], label: str) -> int:
    text = value.strip().lower()
    if text in mapping:
        return mapping[text]
    try:
        return int(text, 0)
    except ValueError as error:
        raise RecoveryError(f"unsupported {label} value {value!r}") from error


def expected_partitions(profile: str) -> list[dict[str, int | str]]:
    spec = PROFILE_SPECS[canonical_profile(profile)]
    if not spec.partition_csv.is_file():
        raise RecoveryError(f"partition CSV is missing: {spec.partition_csv}")
    partitions: list[dict[str, int | str]] = []
    with spec.partition_csv.open(encoding="utf-8", newline="") as stream:
        rows = csv.reader(
            line for line in stream if not line.lstrip().startswith("#")
        )
        for row in rows:
            if not row or all(not item.strip() for item in row):
                continue
            if len(row) < 5:
                raise RecoveryError("partition CSV contains a short row")
            label, type_text, subtype_text, offset_text, size_text = (
                item.strip() for item in row[:5]
            )
            part_type = partition_code(type_text, PARTITION_TYPES, "partition type")
            subtype_key = (type_text.lower(), subtype_text.lower())
            subtype = (
                PARTITION_SUBTYPES[subtype_key]
                if subtype_key in PARTITION_SUBTYPES
                else partition_code(subtype_text, {}, "partition subtype")
            )
            flags = parse_size(row[5]) if len(row) > 5 and row[5].strip() else 0
            partitions.append(
                {
                    "label": label,
                    "type": part_type,
                    "subtype": subtype,
                    "offset": parse_size(offset_text),
                    "size": parse_size(size_text),
                    "flags": flags,
                }
            )
    validate_partition_ranges(partitions, spec.flash_size)
    return partitions


def normalized_partitions(
    partitions: list[dict[str, object]],
) -> list[dict[str, int | str]]:
    return [
        {
            "label": str(item["label"]),
            "type": int(item["type"]),
            "subtype": int(item["subtype"]),
            "offset": int(item["offset"]),
            "size": int(item["size"]),
            "flags": int(item.get("flags", 0)),
        }
        for item in sorted(partitions, key=lambda part: int(part["offset"]))
    ]


def validate_partition_ranges(
    partitions: list[dict[str, object]], flash_size: int
) -> None:
    labels: set[str] = set()
    previous_end = 0
    for item in normalized_partitions(partitions):
        label = str(item["label"])
        offset = int(item["offset"])
        size = int(item["size"])
        if not label or label in labels:
            raise RecoveryError(f"duplicate or empty partition label: {label!r}")
        labels.add(label)
        if size <= 0 or offset < previous_end or offset + size > flash_size:
            raise RecoveryError(
                f"partition {label} has an overlapping or out-of-flash range"
            )
        previous_end = offset + size


def layout_fingerprint(partitions: list[dict[str, object]]) -> str:
    encoded = json.dumps(
        normalized_partitions(partitions),
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return sha256_bytes(encoded)


def partition_table_from_region(
    region: bytes, profile: str
) -> tuple[list[dict[str, int | str]], dict[str, object]]:
    if len(region) != PARTITION_TABLE_READ_SIZE:
        raise RecoveryError(
            f"partition table read must be exactly {PARTITION_TABLE_READ_SIZE} bytes"
        )
    prefixed = b"\xff" * flash_tools.PARTITION_TABLE_OFFSET + region
    try:
        live = normalized_partitions(flash_tools.parse_partitions(prefixed))
        table = flash_tools.validate_partition_table(prefixed)
    except (flash_tools.HilError, ValueError) as error:
        raise RecoveryError(str(error)) from error
    expected = normalized_partitions(expected_partitions(profile))
    if live != expected:
        raise RecoveryError(
            "live partition table does not exactly match the selected profile layout"
        )
    return live, table


def spiffs_partition(partitions: list[dict[str, object]]) -> dict[str, int | str]:
    candidates = [
        item for item in normalized_partitions(partitions)
        if item["label"] == "spiffs"
        and item["type"] == SPIFFS_TYPE
        and item["subtype"] == SPIFFS_SUBTYPE
    ]
    if len(candidates) != 1:
        raise RecoveryError("layout must contain exactly one data/SPIFFS partition")
    return candidates[0]


def required_asset_paths() -> list[str]:
    header = ROOT / "src" / "core" / "www_readiness.h"
    if not header.is_file():
        raise RecoveryError(f"WWW readiness contract is missing: {header}")
    paths = re.findall(r'"(/www/[^"\n]+\.gz)"', header.read_text(encoding="utf-8"))
    if not paths or len(paths) != len(set(paths)):
        raise RecoveryError("WWW readiness contract has no unique required assets")
    return paths


def validate_spiffs_source(image: Path, expected_size: int) -> dict[str, object]:
    try:
        data = image.read_bytes()
    except OSError as error:
        raise RecoveryError(f"cannot read SPIFFS image: {error}") from error
    if len(data) != expected_size:
        raise RecoveryError(
            f"SPIFFS image size mismatch: expected {expected_size}, got {len(data)}"
        )
    if data == b"\xff" * len(data) or data == b"\x00" * len(data):
        raise RecoveryError("SPIFFS image is erased or blank")
    try:
        aggregate = flash_tools.validate_spiffs(data)
    except flash_tools.HilError as error:
        raise RecoveryError(str(error)) from error

    executable = flash_tools.find_mkspiffs()
    if executable is None:
        raise RecoveryError("mkspiffs is unavailable for source image validation")
    assets: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory(prefix="voxone-spiffs-recovery-") as temporary:
        root = Path(temporary)
        copy = root / "spiffs.bin"
        destination = root / "unpacked"
        copy.write_bytes(data)
        result = subprocess.run(
            [
                str(executable), "-u", str(destination), "-b", "4096",
                "-p", "256", "-s", str(expected_size), str(copy),
            ],
            check=False,
            capture_output=True,
        )
        if result.returncode:
            raise RecoveryError("SPIFFS source extraction failed")
        extracted = {
            "/" + path.relative_to(destination).as_posix(): path
            for path in destination.rglob("*") if path.is_file()
        }
        required = required_asset_paths()
        missing = sorted(set(required) - set(extracted))
        if missing:
            raise RecoveryError(
                "SPIFFS image is missing required WWW assets: " + ", ".join(missing)
            )
        outside_www = sorted(path for path in extracted if not path.startswith("/www/"))
        if outside_www:
            raise RecoveryError(
                "recovery image contains non-WWW files and could overwrite user data"
            )
        for name in sorted(required):
            path = extracted[name]
            digest = hashlib.sha256()
            decompressed_size = 0
            try:
                with path.open("rb") as raw, gzip.GzipFile(fileobj=raw) as stream:
                    while True:
                        chunk = stream.read(64 * 1024)
                        if not chunk:
                            break
                        decompressed_size += len(chunk)
                        if decompressed_size > MAX_DECOMPRESSED_ASSET_BYTES:
                            raise RecoveryError(f"decompressed asset is too large: {name}")
                        digest.update(chunk)
            except (OSError, EOFError, gzip.BadGzipFile, zlib.error) as error:
                raise RecoveryError(f"invalid gzip asset {name}: {type(error).__name__}") from error
            assets.append(
                {
                    "path": name,
                    "compressedSize": path.stat().st_size,
                    "compressedSha256": flash_tools.sha256(path),
                    "decompressedSize": decompressed_size,
                    "decompressedSha256": digest.hexdigest(),
                }
            )
    return {
        "size": len(data),
        "sha256": sha256_bytes(data),
        "spiffsIntegrity": aggregate,
        "requiredAssets": assets,
    }


def manifest_for_image(
    profile: str,
    image: Path,
    expected_sha256: str,
    www_build: str,
) -> dict[str, object]:
    profile = canonical_profile(profile)
    spec = PROFILE_SPECS[profile]
    expected_hash = canonical_sha256(expected_sha256, "expected image SHA256")
    build = canonical_build(www_build)
    partitions = expected_partitions(profile)
    spiffs = spiffs_partition(partitions)
    validation = validate_spiffs_source(image, int(spiffs["size"]))
    if validation["sha256"] != expected_hash:
        raise RecoveryError("SPIFFS image SHA256 does not match the operator value")
    return {
        "schema": IMAGE_MANIFEST_SCHEMA,
        "profile": profile,
        "chip": spec.chip,
        "flashSize": spec.flash_size,
        "layoutSha256": layout_fingerprint(partitions),
        "spiffsOffset": int(spiffs["offset"]),
        "spiffsSize": int(spiffs["size"]),
        "wwwBuild": build,
        "image": {
            "file": image.name,
            "size": validation["size"],
            "sha256": validation["sha256"],
        },
        "requiredAssets": validation["requiredAssets"],
        "limitations": [
            "wwwBuild is operator service metadata, not a cryptographic identity",
            "this manifest is not a cryptographic release signature",
        ],
    }


def write_private(path: Path, data: bytes) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    descriptor = os.open(path, flags, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
    except BaseException:
        try:
            path.unlink()
        except OSError:
            pass
        raise


def write_private_json(path: Path, value: dict[str, object]) -> None:
    write_private(
        path,
        (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )


def read_json(path: Path, label: str) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RecoveryError(f"cannot read {label}: {type(error).__name__}") from error
    if not isinstance(value, dict):
        raise RecoveryError(f"{label} must be a JSON object")
    return value


def validate_image_manifest(
    manifest_path: Path,
    image: Path,
    profile: str,
    expected_sha256: str,
    expected_www_build: str,
) -> dict[str, object]:
    profile = canonical_profile(profile)
    spec = PROFILE_SPECS[profile]
    expected_hash = canonical_sha256(expected_sha256, "expected image SHA256")
    build = canonical_build(expected_www_build)
    manifest = read_json(manifest_path, "image manifest")
    if manifest.get("schema") != IMAGE_MANIFEST_SCHEMA:
        raise RecoveryError("unsupported image manifest schema")
    if manifest.get("profile") != profile:
        raise RecoveryError("image manifest profile mismatch")
    if manifest.get("chip") != spec.chip or manifest.get("flashSize") != spec.flash_size:
        raise RecoveryError("image manifest chip or flash size mismatch")
    partitions = expected_partitions(profile)
    spiffs = spiffs_partition(partitions)
    if manifest.get("layoutSha256") != layout_fingerprint(partitions):
        raise RecoveryError("image manifest layout mismatch")
    if (
        manifest.get("spiffsOffset") != int(spiffs["offset"])
        or manifest.get("spiffsSize") != int(spiffs["size"])
    ):
        raise RecoveryError("image manifest SPIFFS range mismatch")
    if manifest.get("wwwBuild") != build:
        raise RecoveryError("image manifest WWW build mismatch")
    image_record = manifest.get("image")
    if not isinstance(image_record, dict):
        raise RecoveryError("image manifest has no image record")
    if image_record.get("sha256") != expected_hash:
        raise RecoveryError("manifest SHA256 differs from the operator value")
    validation = validate_spiffs_source(image, int(spiffs["size"]))
    if validation["sha256"] != expected_hash:
        raise RecoveryError("source image SHA256 mismatch")
    if image_record.get("size") != validation["size"]:
        raise RecoveryError("source image size differs from its manifest")
    if manifest.get("requiredAssets") != validation["requiredAssets"]:
        raise RecoveryError("source image asset inventory differs from its manifest")
    return manifest


def verify_explicit_port(port: Path) -> dict[str, str]:
    if not port.is_absolute() or port.parent != Path("/dev/serial/by-id"):
        raise RecoveryError("USB port must be an explicit /dev/serial/by-id path")
    resolved = flash_tools.resolved(port)
    if resolved is None:
        raise RecoveryError(f"USB by-id path is absent: {port}")
    try:
        properties = flash_tools.udev_properties(resolved)
        holders = flash_tools.process_holders(resolved)
    except (OSError, subprocess.SubprocessError) as error:
        raise RecoveryError(f"USB identity inspection failed: {error}") from error
    if not properties.get("ID_SERIAL"):
        raise RecoveryError("USB device has no stable udev serial identity")
    if holders:
        raise RecoveryError(
            "USB port has an owner; release it explicitly before recovery: "
            + ",".join(str(item["pid"]) for item in holders)
        )
    return {
        "portById": str(port),
        "resolvedPort": str(resolved),
        "usbSerial": properties["ID_SERIAL"],
        "usbVid": properties.get("ID_VENDOR_ID", ""),
        "usbPid": properties.get("ID_MODEL_ID", ""),
    }


def esptool_base(port: Path, chip: str) -> list[str]:
    return [
        str(flash_tools.PIO_PYTHON),
        str(flash_tools.ESPTOOL),
        "--chip", chip,
        "--port", str(port),
        "--baud", "460800",
        "--before", "no_reset",
        "--after", "no_reset",
        "--no-stub",
    ]


class EsptoolAdapter:
    """Minimal ROM-loader adapter.  Only write() mutates device flash."""

    def __init__(self, port: Path, spec: ProfileSpec):
        self.port = port
        self.spec = spec

    def _run(self, command: list[str]) -> str:
        result = subprocess.run(
            esptool_base(self.port, self.spec.chip) + command,
            check=False,
            capture_output=True,
            text=True,
        )
        output = result.stdout + result.stderr
        if result.returncode:
            raise RecoveryError(
                f"esptool {command[0]} failed; STOP without retry or reset"
            )
        return output

    def identity(self) -> dict[str, object]:
        chip_output = self._run(["chip_id"])
        flash_output = self._run(["flash_id"])
        chip_match = re.search(r"Chip is\s+(ESP32(?:-[A-Z0-9]+)?)", chip_output, re.I)
        mac_match = re.search(r"MAC:\s*((?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2})", chip_output)
        if not chip_match or not mac_match:
            raise RecoveryError("esptool did not report an unambiguous chip and MAC")
        chip_text = chip_match.group(1).upper()
        if chip_text.startswith("ESP32-S3"):
            chip = "esp32s3"
        elif chip_text.startswith(("ESP32-S2", "ESP32-C", "ESP32-H")):
            chip = "unknown"
        elif chip_text.startswith("ESP32"):
            # Classic ESP32 ROMs report variants such as ESP32-D0WD-V3.
            chip = "esp32"
        else:
            chip = "unknown"
        try:
            flash_size = flash_tools.detected_flash_bytes(flash_output)
        except flash_tools.HilError as error:
            raise RecoveryError(str(error)) from error
        return {
            "chip": chip,
            "chipDescription": chip_match.group(0),
            "mac": canonical_mac(mac_match.group(1)),
            "flashSize": flash_size,
        }

    def read(self, offset: int, size: int) -> bytes:
        with tempfile.TemporaryDirectory(prefix="voxone-spiffs-usb-read-") as temporary:
            target = Path(temporary) / "readback.bin"
            self._run(["read_flash", hex(offset), hex(size), str(target)])
            try:
                data = target.read_bytes()
            except OSError as error:
                raise RecoveryError(f"cannot read temporary flash capture: {error}") from error
            if len(data) != size:
                raise RecoveryError(
                    f"short flash read at 0x{offset:x}: expected {size}, got {len(data)}"
                )
            return data

    def write(self, offset: int, image: Path) -> None:
        self._run(["write_flash", hex(offset), str(image)])

    def verify(self, offset: int, image: Path) -> None:
        self._run(["verify_flash", hex(offset), str(image)])


def verify_device_identity(
    identity: dict[str, object], profile: str, expected_mac: str
) -> None:
    profile = canonical_profile(profile)
    spec = PROFILE_SPECS[profile]
    if identity.get("chip") != spec.chip:
        raise RecoveryError(
            f"chip mismatch: profile {profile} requires {spec.chip}, "
            f"device reports {identity.get('chip')}"
        )
    if int(identity.get("flashSize", 0)) != spec.flash_size:
        raise RecoveryError(
            f"flash size mismatch: profile {profile} requires {spec.flash_size}, "
            f"device reports {identity.get('flashSize')}"
        )
    if canonical_mac(str(identity.get("mac", ""))) != canonical_mac(expected_mac):
        raise RecoveryError("device MAC does not match the explicitly expected MAC")


def read_identical(
    adapter: EsptoolAdapter, offset: int, size: int, label: str
) -> tuple[bytes, bytes]:
    first = adapter.read(offset, size)
    second = adapter.read(offset, size)
    if first != second:
        raise RecoveryError(f"two independent {label} reads disagree")
    return first, second


def build_restore_plan(
    profile: str,
    port_identity: dict[str, str],
    device_identity: dict[str, object],
    expected_mac: str,
    table_a: bytes,
    table_b: bytes,
    backup_a: bytes,
    backup_b: bytes,
    image: Path,
    image_manifest: Path,
    expected_sha256: str,
    expected_www_build: str,
    output_dir: Path,
) -> dict[str, object]:
    profile = canonical_profile(profile)
    verify_device_identity(device_identity, profile, expected_mac)
    if table_a != table_b:
        raise RecoveryError("two independent partition-table reads disagree")
    partitions, table_validation = partition_table_from_region(table_a, profile)
    spiffs = spiffs_partition(partitions)
    if len(backup_a) != int(spiffs["size"]) or len(backup_b) != int(spiffs["size"]):
        raise RecoveryError("SPIFFS backup read has the wrong size")
    if backup_a != backup_b:
        raise RecoveryError("two independent SPIFFS backup reads disagree")
    manifest = validate_image_manifest(
        image_manifest,
        image,
        profile,
        expected_sha256,
        expected_www_build,
    )
    protected = [
        {
            "label": item["label"],
            "offset": item["offset"],
            "size": item["size"],
            "endExclusive": int(item["offset"]) + int(item["size"]),
            "write": False,
        }
        for item in partitions if item["label"] != "spiffs"
    ]
    plan: dict[str, object] = {
        "schema": RESTORE_PLAN_SCHEMA,
        "createdUtc": datetime.now(timezone.utc).isoformat(),
        "status": "READY_FOR_SEPARATE_WRITE_APPROVAL",
        "writeAuthorized": False,
        "profile": profile,
        "device": {
            **port_identity,
            "chip": device_identity["chip"],
            "chipDescription": device_identity.get("chipDescription", ""),
            "mac": canonical_mac(str(device_identity["mac"])),
            "flashSize": int(device_identity["flashSize"]),
        },
        "partitionTable": {
            "offset": flash_tools.PARTITION_TABLE_OFFSET,
            "readSize": PARTITION_TABLE_READ_SIZE,
            "sha256": sha256_bytes(table_a),
            "twoReadsMatch": True,
            "md5Valid": bool(table_validation.get("md5Valid")),
            "layoutSha256": layout_fingerprint(partitions),
        },
        "target": {
            "label": "spiffs",
            "type": SPIFFS_TYPE,
            "subtype": SPIFFS_SUBTYPE,
            "offset": int(spiffs["offset"]),
            "size": int(spiffs["size"]),
            "endExclusive": int(spiffs["offset"]) + int(spiffs["size"]),
            "onlyRangeAllowed": True,
        },
        "source": {
            "image": str(image.resolve()),
            "manifest": str(image_manifest.resolve()),
            "sha256": manifest["image"]["sha256"],
            "wwwBuild": manifest["wwwBuild"],
            "profile": manifest["profile"],
        },
        "backup": {
            "readA": str((output_dir / "spiffs-before-a.bin").resolve()),
            "readB": str((output_dir / "spiffs-before-b.bin").resolve()),
            "size": len(backup_a),
            "sha256": sha256_bytes(backup_a),
            "twoReadsMatch": True,
        },
        "protectedRanges": protected,
        "postWrite": {
            "verifyFlashRequired": True,
            "readbackRequired": True,
            "readback": str((output_dir / "spiffs-after-readback.bin").resolve()),
            "expectedSha256": manifest["image"]["sha256"],
            "automaticRetry": False,
            "automaticReset": False,
        },
        "forbiddenOperations": sorted(REQUIRED_FORBIDDEN_OPERATIONS),
    }
    unsigned = json.dumps(plan, sort_keys=True, separators=(",", ":")).encode("utf-8")
    plan["authorizationToken"] = "WRITE-SPIFFS-" + sha256_bytes(unsigned)[:16].upper()
    return plan


def validate_restore_plan(plan: dict[str, object]) -> dict[str, object]:
    if plan.get("schema") != RESTORE_PLAN_SCHEMA:
        raise RecoveryError("unsupported restore plan schema")
    profile = canonical_profile(str(plan.get("profile", "")))
    partitions = expected_partitions(profile)
    expected_target = spiffs_partition(partitions)
    target = plan.get("target")
    if not isinstance(target, dict):
        raise RecoveryError("restore plan has no target")
    expected_range = (
        "spiffs", SPIFFS_TYPE, SPIFFS_SUBTYPE,
        int(expected_target["offset"]), int(expected_target["size"]),
    )
    actual_range = (
        target.get("label"), target.get("type"), target.get("subtype"),
        target.get("offset"), target.get("size"),
    )
    if actual_range != expected_range or target.get("onlyRangeAllowed") is not True:
        raise RecoveryError("restore plan target is not the exact profile SPIFFS range")
    if target.get("endExclusive") != int(target["offset"]) + int(target["size"]):
        raise RecoveryError("restore plan target end is inconsistent")
    if plan.get("writeAuthorized") is not False:
        raise RecoveryError("stored restore plans must never pre-authorize a write")
    forbidden = plan.get("forbiddenOperations")
    if not isinstance(forbidden, list) or not REQUIRED_FORBIDDEN_OPERATIONS.issubset(
        set(str(item) for item in forbidden)
    ):
        raise RecoveryError("restore plan does not preserve forbidden-operation policy")
    source = plan.get("source")
    backup = plan.get("backup")
    device = plan.get("device")
    table = plan.get("partitionTable")
    post = plan.get("postWrite")
    if not all(isinstance(item, dict) for item in (source, backup, device, table, post)):
        raise RecoveryError("restore plan is incomplete")
    spec = PROFILE_SPECS[profile]
    if (
        device.get("chip") != spec.chip
        or device.get("flashSize") != spec.flash_size
        or not isinstance(device.get("portById"), str)
        or not str(device["portById"]).startswith("/dev/serial/by-id/")
    ):
        raise RecoveryError("restore plan device identity does not match its profile")
    canonical_mac(str(device.get("mac", "")))
    expected_protected = [
        {
            "label": item["label"],
            "offset": item["offset"],
            "size": item["size"],
            "endExclusive": int(item["offset"]) + int(item["size"]),
            "write": False,
        }
        for item in partitions if item["label"] != "spiffs"
    ]
    if plan.get("protectedRanges") != expected_protected:
        raise RecoveryError("restore plan protected ranges are incomplete or changed")
    if (
        table.get("offset") != flash_tools.PARTITION_TABLE_OFFSET
        or table.get("readSize") != PARTITION_TABLE_READ_SIZE
        or table.get("twoReadsMatch") is not True
        or table.get("md5Valid") is not True
        or table.get("layoutSha256") != layout_fingerprint(partitions)
    ):
        raise RecoveryError("restore plan layout is stale")
    canonical_sha256(str(table.get("sha256", "")), "partition table SHA256")
    if source.get("profile") != profile:
        raise RecoveryError("restore plan source profile mismatch")
    expected_hash = canonical_sha256(str(source.get("sha256", "")), "plan image SHA256")
    validate_image_manifest(
        Path(str(source["manifest"])),
        Path(str(source["image"])),
        profile,
        expected_hash,
        str(source.get("wwwBuild", "")),
    )
    backup_a = Path(str(backup.get("readA", "")))
    backup_b = Path(str(backup.get("readB", "")))
    try:
        first = backup_a.read_bytes()
        second = backup_b.read_bytes()
    except OSError as error:
        raise RecoveryError(f"cannot read planned backup: {error}") from error
    if (
        first != second
        or len(first) != int(target["size"])
        or backup.get("size") != int(target["size"])
        or sha256_bytes(first) != backup.get("sha256")
        or backup.get("twoReadsMatch") is not True
    ):
        raise RecoveryError("planned SPIFFS backup is short, changed or inconsistent")
    if (
        post.get("verifyFlashRequired") is not True
        or post.get("readbackRequired") is not True
        or post.get("expectedSha256") != expected_hash
        or post.get("automaticRetry") is not False
        or post.get("automaticReset") is not False
        or not isinstance(post.get("readback"), str)
        or Path(str(post["readback"])).parent != backup_a.parent
    ):
        raise RecoveryError("restore plan post-write safety contract is incomplete")
    token = plan.get("authorizationToken")
    if not isinstance(token, str):
        raise RecoveryError("restore plan has no authorization token")
    unsigned_plan = dict(plan)
    del unsigned_plan["authorizationToken"]
    unsigned = json.dumps(
        unsigned_plan, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    expected_token = "WRITE-SPIFFS-" + sha256_bytes(unsigned)[:16].upper()
    if token != expected_token:
        raise RecoveryError("restore plan authorization token is inconsistent")
    return plan


def write_plan_files(
    output_dir: Path,
    plan: dict[str, object],
    table_a: bytes,
    table_b: bytes,
    backup_a: bytes,
    backup_b: bytes,
) -> Path:
    if output_dir.exists():
        raise RecoveryError(f"refusing to reuse output directory: {output_dir}")
    output_dir.mkdir(mode=0o700, parents=True)
    output_dir.chmod(0o700)
    write_private(output_dir / "partition-table-a.bin", table_a)
    write_private(output_dir / "partition-table-b.bin", table_b)
    write_private(output_dir / "spiffs-before-a.bin", backup_a)
    write_private(output_dir / "spiffs-before-b.bin", backup_b)
    plan_path = output_dir / "restore-plan.json"
    write_private_json(plan_path, plan)
    write_private(
        output_dir / "restore-plan.json.sha256",
        (flash_tools.sha256(plan_path) + "  restore-plan.json\n").encode("ascii"),
    )
    return plan_path


def verify_plan_sidecar(plan_path: Path) -> None:
    sidecar = plan_path.with_name(plan_path.name + ".sha256")
    try:
        line = sidecar.read_text(encoding="ascii").strip()
    except (OSError, UnicodeError) as error:
        raise RecoveryError("restore plan SHA256 sidecar is unavailable") from error
    fields = line.split()
    if len(fields) != 2 or fields[1] != plan_path.name:
        raise RecoveryError("restore plan SHA256 sidecar format is invalid")
    if canonical_sha256(fields[0], "restore plan sidecar SHA256") != flash_tools.sha256(plan_path):
        raise RecoveryError("restore plan SHA256 sidecar mismatch")


def execute_restore(
    plan_path: Path,
    port: Path,
    expected_mac: str,
    authorization_token: str,
    confirm_rom_loader: bool,
    confirm_separate_approval: bool,
    adapter_factory: Callable[[Path, ProfileSpec], EsptoolAdapter] = EsptoolAdapter,
    port_verifier: Callable[[Path], dict[str, str]] = verify_explicit_port,
) -> dict[str, object]:
    if not confirm_rom_loader or not confirm_separate_approval:
        raise RecoveryError(
            "write requires both --confirm-rom-loader and --confirm-separate-approval"
        )
    verify_plan_sidecar(plan_path)
    plan = validate_restore_plan(read_json(plan_path, "restore plan"))
    if authorization_token != plan["authorizationToken"]:
        raise RecoveryError("write authorization token does not match this plan")
    device = plan["device"]
    if str(port) != device.get("portById"):
        raise RecoveryError("explicit USB port differs from the prepared plan")
    if canonical_mac(expected_mac) != device.get("mac"):
        raise RecoveryError("explicit MAC differs from the prepared plan")
    output_dir = plan_path.parent
    result_path = output_dir / "execution-result.json"
    readback_path = Path(str(plan["postWrite"]["readback"]))
    if result_path.exists() or readback_path.exists():
        raise RecoveryError("plan already has execution evidence; automatic repeat is forbidden")

    port_identity = port_verifier(port)
    if port_identity.get("usbSerial") != device.get("usbSerial"):
        raise RecoveryError("USB serial identity changed since plan preparation")
    profile = str(plan["profile"])
    spec = PROFILE_SPECS[profile]
    adapter = adapter_factory(port, spec)
    live_identity = adapter.identity()
    verify_device_identity(live_identity, profile, expected_mac)
    if live_identity.get("mac") != device.get("mac"):
        raise RecoveryError("live device identity differs from plan")

    table_a, table_b = read_identical(
        adapter,
        flash_tools.PARTITION_TABLE_OFFSET,
        PARTITION_TABLE_READ_SIZE,
        "partition-table",
    )
    partitions, _ = partition_table_from_region(table_a, profile)
    if sha256_bytes(table_a) != plan["partitionTable"]["sha256"]:
        raise RecoveryError("live partition table changed since plan preparation")
    target = spiffs_partition(partitions)
    current_a, current_b = read_identical(
        adapter, int(target["offset"]), int(target["size"]), "pre-write SPIFFS"
    )
    if sha256_bytes(current_a) != plan["backup"]["sha256"]:
        raise RecoveryError("live SPIFFS changed since the verified backup")

    image = Path(str(plan["source"]["image"]))
    source_hash = canonical_sha256(str(plan["source"]["sha256"]), "source SHA256")
    if flash_tools.sha256(image) != source_hash:
        raise RecoveryError("source image changed after plan preparation")

    # The only device-mutating call in the tool.  There is deliberately no
    # retry, erase_region, erase_flash, reset or alternate address.
    adapter.write(int(target["offset"]), image)
    adapter.verify(int(target["offset"]), image)
    readback = adapter.read(int(target["offset"]), int(target["size"]))
    write_private(readback_path, readback)
    readback_hash = sha256_bytes(readback)
    if readback_hash != source_hash:
        raise RecoveryError(
            "post-write readback SHA256 mismatch; STOP without retry or reset"
        )
    result: dict[str, object] = {
        "schema": EXECUTION_RESULT_SCHEMA,
        "status": "WRITE_AND_READBACK_PASS",
        "profile": profile,
        "mac": device["mac"],
        "targetOffset": int(target["offset"]),
        "targetSize": int(target["size"]),
        "sourceSha256": source_hash,
        "readbackSha256": readback_hash,
        "writeAttempts": 1,
        "verifyFlash": True,
        "automaticRetry": False,
        "automaticReset": False,
        "nextStep": "controlled boot requires separate approval",
    }
    write_private_json(result_path, result)
    return result


def command_manifest(args: argparse.Namespace) -> int:
    try:
        manifest = manifest_for_image(
            args.profile, args.image, args.expected_sha256, args.www_build
        )
        if args.output.exists():
            raise RecoveryError(f"refusing to overwrite manifest: {args.output}")
        args.output.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        write_private_json(args.output, manifest)
    except (RecoveryError, OSError, subprocess.SubprocessError) as error:
        print(f"RESULT BLOCKED: {error}")
        return 1
    print(
        f"PASS profile={manifest['profile']} image={manifest['image']['size']}B "
        f"sha256={manifest['image']['sha256']} wwwBuild={manifest['wwwBuild']}"
    )
    print("RESULT PASS (offline image manifest; no device access)")
    return 0


def command_prepare(args: argparse.Namespace) -> int:
    if not args.confirm_rom_loader:
        print("RESULT BLOCKED: prepare requires --confirm-rom-loader")
        return 2
    try:
        profile = canonical_profile(args.profile)
        manifest = validate_image_manifest(
            args.image_manifest,
            args.image,
            profile,
            args.expected_sha256,
            args.expected_www_build,
        )
        del manifest
        port_identity = verify_explicit_port(args.port)
        adapter = EsptoolAdapter(args.port, PROFILE_SPECS[profile])
        device_identity = adapter.identity()
        verify_device_identity(device_identity, profile, args.expected_mac)
        table_a, table_b = read_identical(
            adapter,
            flash_tools.PARTITION_TABLE_OFFSET,
            PARTITION_TABLE_READ_SIZE,
            "partition-table",
        )
        partitions, _ = partition_table_from_region(table_a, profile)
        target = spiffs_partition(partitions)
        backup_a, backup_b = read_identical(
            adapter, int(target["offset"]), int(target["size"]), "SPIFFS backup"
        )
        plan = build_restore_plan(
            profile,
            port_identity,
            device_identity,
            args.expected_mac,
            table_a,
            table_b,
            backup_a,
            backup_b,
            args.image,
            args.image_manifest,
            args.expected_sha256,
            args.expected_www_build,
            args.output,
        )
        plan_path = write_plan_files(
            args.output, plan, table_a, table_b, backup_a, backup_b
        )
        validate_restore_plan(plan)
    except (RecoveryError, OSError, subprocess.SubprocessError) as error:
        print(f"RESULT BLOCKED: {error}")
        return 1
    print(f"PASS read-only plan: {plan_path}")
    print(
        f"TARGET ONLY spiffs 0x{plan['target']['offset']:x}-"
        f"0x{plan['target']['endExclusive']:x} ({plan['target']['size']} bytes)"
    )
    print(f"WRITE NOT AUTHORIZED token={plan['authorizationToken']}")
    print("RESULT PASS (read-only; separate approval required for execute)")
    return 0


def command_show_plan(args: argparse.Namespace) -> int:
    try:
        verify_plan_sidecar(args.plan)
        plan = validate_restore_plan(read_json(args.plan, "restore plan"))
    except (RecoveryError, OSError, subprocess.SubprocessError) as error:
        print(f"RESULT BLOCKED: {error}")
        return 1
    target = plan["target"]
    print(f"PROFILE {plan['profile']} MAC {plan['device']['mac']}")
    print(
        f"TARGET ONLY {target['label']} 0x{target['offset']:x}-"
        f"0x{target['endExclusive']:x} ({target['size']} bytes)"
    )
    print(f"SOURCE SHA256 {plan['source']['sha256']}")
    print(f"BACKUP SHA256 {plan['backup']['sha256']} twoReadsMatch=true")
    print(f"WRITE NOT AUTHORIZED token={plan['authorizationToken']}")
    print("RESULT PASS (offline plan validation)")
    return 0


def command_execute(args: argparse.Namespace) -> int:
    try:
        result = execute_restore(
            args.plan,
            args.port,
            args.expected_mac,
            args.authorization_token,
            args.confirm_rom_loader,
            args.confirm_separate_approval,
        )
    except (RecoveryError, OSError, subprocess.SubprocessError) as error:
        print(f"RESULT STOP: {error}")
        return 1
    print(
        f"PASS one selective SPIFFS write and readback "
        f"sha256={result['readbackSha256']}"
    )
    print("INFO device remains in ROM loader; controlled boot needs separate approval")
    print("RESULT PASS")
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        description="Fail-closed selective VoxOne SPIFFS USB recovery"
    )
    commands = result.add_subparsers(dest="command", required=True)

    manifest = commands.add_parser(
        "manifest", help="validate a SPIFFS image and create offline service metadata"
    )
    manifest.add_argument("--profile", required=True)
    manifest.add_argument("--image", type=Path, required=True)
    manifest.add_argument("--expected-sha256", required=True)
    manifest.add_argument("--www-build", required=True)
    manifest.add_argument("--output", type=Path, required=True)
    manifest.set_defaults(function=command_manifest)

    prepare = commands.add_parser(
        "prepare", help="read device twice and create a plan; never writes flash"
    )
    prepare.add_argument("--profile", required=True)
    prepare.add_argument("--port", type=Path, required=True)
    prepare.add_argument("--expected-mac", required=True)
    prepare.add_argument("--image", type=Path, required=True)
    prepare.add_argument("--image-manifest", type=Path, required=True)
    prepare.add_argument("--expected-sha256", required=True)
    prepare.add_argument("--expected-www-build", required=True)
    prepare.add_argument("--output", type=Path, required=True)
    prepare.add_argument("--confirm-rom-loader", action="store_true")
    prepare.set_defaults(function=command_prepare)

    show = commands.add_parser(
        "show-plan", help="validate a prepared plan entirely offline"
    )
    show.add_argument("--plan", type=Path, required=True)
    show.set_defaults(function=command_show_plan)

    execute = commands.add_parser(
        "execute", help="WRITE SPIFFS once; requires a separately approved plan"
    )
    execute.add_argument("--plan", type=Path, required=True)
    execute.add_argument("--port", type=Path, required=True)
    execute.add_argument("--expected-mac", required=True)
    execute.add_argument("--authorization-token", required=True)
    execute.add_argument("--confirm-rom-loader", action="store_true")
    execute.add_argument("--confirm-separate-approval", action="store_true")
    execute.set_defaults(function=command_execute)
    return result


def main() -> int:
    args = parser().parse_args()
    return args.function(args)


if __name__ == "__main__":
    sys.exit(main())
