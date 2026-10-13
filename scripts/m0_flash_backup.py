#!/usr/bin/env python3
"""Read-only preparation and ROM-loader backup for the VoxOne M0 HIL.

The capture command never toggles reset lines, uploads a flasher stub, writes
flash, or starts the application.  It must only be used after a person has put
the M0 in the ROM download loader and after the tty holder has been released.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import zlib


M0_BY_ID = Path(
    "/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_"
    "D0:CF:13:2F:EE:00-if00"
)
FT232R_BY_ID = Path(
    "/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A7039N0Z-if00-port0"
)
M0_VID_PID = ("303a", "1001")
FT232R_VID_PID = ("0403", "6001")
PIO_PYTHON = Path("/home/piotrek/.local/venvs/platformio/bin/python")
ESPTOOL = Path("/home/piotrek/.platformio/packages/tool-esptoolpy/esptool.py")
PARTITION_TABLE_OFFSET = 0x8000
PARTITION_ENTRY_SIZE = 32
PARTITION_MAGIC = 0x50AA
PARTITION_MD5_MAGIC = 0xEBEB
ESP_IMAGE_MAGIC = 0xE9
ESP_IMAGE_CHECKSUM_SEED = 0xEF
NVS_PAGE_SIZE = 4096
NVS_ENTRY_SIZE = 32
NVS_ENTRY_COUNT = 126
OTA_STATES = {
    0: "NEW",
    1: "PENDING_VERIFY",
    2: "VALID",
    3: "INVALID",
    4: "ABORTED",
    0xFFFFFFFF: "UNDEFINED",
}


class HilError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolved(path: Path) -> Path | None:
    try:
        return path.resolve(strict=True)
    except FileNotFoundError:
        return None


def udev_properties(device: Path) -> dict[str, str]:
    result = subprocess.run(
        ["udevadm", "info", "--query=property", f"--name={device}"],
        check=True,
        capture_output=True,
        text=True,
    )
    props: dict[str, str] = {}
    for line in result.stdout.splitlines():
        key, separator, value = line.partition("=")
        if separator:
            props[key] = value
    return props


def verify_usb_identity(path: Path, expected: tuple[str, str], label: str) -> Path:
    device = resolved(path)
    if device is None:
        raise HilError(f"{label}: expected by-id device is absent: {path}")
    props = udev_properties(device)
    actual = (props.get("ID_VENDOR_ID", ""), props.get("ID_MODEL_ID", ""))
    if actual != expected:
        raise HilError(
            f"{label}: VID:PID mismatch, expected {expected[0]}:{expected[1]}, "
            f"got {actual[0]}:{actual[1]}"
        )
    return device


def process_holders(device: Path) -> list[dict[str, object]]:
    holders: list[dict[str, object]] = []
    canonical = str(device.resolve())
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        fd_dir = proc / "fd"
        try:
            descriptors = list(fd_dir.iterdir())
        except (FileNotFoundError, PermissionError):
            continue
        matching: list[int] = []
        for descriptor in descriptors:
            try:
                target = os.readlink(descriptor)
            except (FileNotFoundError, PermissionError, OSError):
                continue
            if target == canonical:
                matching.append(int(descriptor.name))
        if not matching:
            continue
        try:
            command = (proc / "cmdline").read_bytes().replace(b"\0", b" ").decode(
                "utf-8", "replace"
            ).strip()
            status = (proc / "status").read_text(encoding="utf-8", errors="replace")
            owner = next(
                line.split()[1] for line in status.splitlines() if line.startswith("Uid:")
            )
        except (FileNotFoundError, PermissionError, StopIteration):
            command, owner = "<unavailable>", "<unavailable>"
        holders.append(
            {"pid": int(proc.name), "uid": owner, "fds": matching, "command": command}
        )
    return holders


def readiness(host: str) -> tuple[bool, str]:
    try:
        with urllib.request.urlopen(
            f"http://{host}/api/ready", timeout=3
        ) as response:
            body = json.loads(response.read(4096))
    except (OSError, urllib.error.URLError, ValueError, json.JSONDecodeError) as error:
        return False, type(error).__name__
    boot_id = body.get("bootId")
    return response.status == 200 and body.get("ready") is True, (
        boot_id if isinstance(boot_id, str) and re.fullmatch(r"[0-9a-fA-F]{8}", boot_id)
        else "invalid"
    )


def command_preflight(args: argparse.Namespace) -> int:
    failures = 0
    for path, identity, label in (
        (M0_BY_ID, M0_VID_PID, "M0"),
        (FT232R_BY_ID, FT232R_VID_PID, "FT232R"),
    ):
        try:
            device = verify_usb_identity(path, identity, label)
            print(f"PASS {label} {path.name} -> {device}")
            holders = process_holders(device)
            if holders:
                for holder in holders:
                    executable = str(holder["command"]).split(" ", 1)[0]
                    print(
                        f"INFO {label} holder pid={holder['pid']} uid={holder['uid']} "
                        f"fds={holder['fds']} executable={executable}"
                    )
                if label == "M0":
                    failures += 1
                    print("BLOCK M0 ROM readback requires a separately approved port release")
            else:
                print(f"PASS {label} serial port has no holder")
        except (HilError, OSError, subprocess.SubprocessError) as error:
            failures += 1
            print(f"FAIL {error}")

    ready, detail = readiness(args.host)
    if ready:
        print(f"PASS M0 network readiness bootId={detail}")
    else:
        failures += 1
        print(f"FAIL M0 network readiness ({detail})")

    if PIO_PYTHON.is_file() and ESPTOOL.is_file():
        print("PASS pinned PlatformIO Python and esptool are available")
    else:
        failures += 1
        print("FAIL pinned PlatformIO Python or esptool is absent")
    print("RESULT " + ("PASS" if failures == 0 else "BLOCKED"))
    return 0 if failures == 0 else 1


def esptool_base(port: Path) -> list[str]:
    return [
        str(PIO_PYTHON),
        str(ESPTOOL),
        "--chip", "esp32s3",
        "--port", str(port),
        "--baud", "460800",
        "--before", "no_reset",
        "--after", "no_reset",
        "--no-stub",
    ]


def run_esptool(port: Path, command: list[str]) -> str:
    result = subprocess.run(
        esptool_base(port) + command,
        check=False,
        capture_output=True,
        text=True,
    )
    output = result.stdout + result.stderr
    if result.returncode:
        raise HilError(
            f"esptool {' '.join(command)} failed without any reset; "
            "confirm that M0 is already in the ROM loader"
        )
    return output


def detected_flash_bytes(output: str) -> int:
    match = re.search(r"Detected flash size:\s*(\d+)\s*(KB|MB)", output, re.I)
    if not match:
        raise HilError("esptool did not report a detected flash size")
    multiplier = 1024 if match.group(2).upper() == "KB" else 1024 * 1024
    size = int(match.group(1)) * multiplier
    if size < 4 * 1024 * 1024 or size > 128 * 1024 * 1024:
        raise HilError(f"implausible detected flash size: {size}")
    return size


def esp_ota_crc32(sequence: int) -> int:
    # Exact esp_rom_crc32_le(UINT32_MAX, ota_seq_le, 4) used by ESP-IDF.
    crc = 0
    for value in struct.pack("<I", sequence):
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return (~crc) & 0xFFFFFFFF


def parse_partitions(image: bytes) -> list[dict[str, object]]:
    partitions: list[dict[str, object]] = []
    for offset in range(PARTITION_TABLE_OFFSET, PARTITION_TABLE_OFFSET + 0xC00,
                        PARTITION_ENTRY_SIZE):
        raw = image[offset:offset + PARTITION_ENTRY_SIZE]
        if len(raw) != PARTITION_ENTRY_SIZE:
            break
        magic = struct.unpack_from("<H", raw)[0]
        if magic == 0xFFFF:
            break
        if magic == PARTITION_MD5_MAGIC:
            break
        if magic != PARTITION_MAGIC:
            raise HilError(f"invalid partition entry magic 0x{magic:04x} at 0x{offset:x}")
        _, part_type, subtype, part_offset, size, raw_label, flags = struct.unpack(
            "<HBBII16sI", raw
        )
        label = raw_label.split(b"\0", 1)[0].decode("ascii", "replace")
        partitions.append(
            {
                "label": label,
                "type": part_type,
                "subtype": subtype,
                "offset": part_offset,
                "size": size,
                "flags": flags,
            }
        )
    if not partitions:
        raise HilError("no plaintext partition table found at 0x8000")
    return partitions


def validate_partition_table(image: bytes) -> dict[str, object]:
    table = image[PARTITION_TABLE_OFFSET:PARTITION_TABLE_OFFSET + 0xC00]
    trailer_offset = None
    for offset in range(0, len(table), PARTITION_ENTRY_SIZE):
        magic = struct.unpack_from("<H", table, offset)[0]
        if magic == PARTITION_MD5_MAGIC:
            trailer_offset = offset
            break
        if magic == 0xFFFF:
            break
        if magic != PARTITION_MAGIC:
            raise HilError(
                f"invalid partition entry magic 0x{magic:04x} at "
                f"0x{PARTITION_TABLE_OFFSET + offset:x}"
            )
    if trailer_offset is None:
        raise HilError("partition table has no MD5 trailer")
    stored = table[trailer_offset + 16:trailer_offset + 32]
    calculated = hashlib.md5(table[:trailer_offset]).digest()
    if stored != calculated:
        raise HilError("partition table MD5 mismatch")
    return {
        "md5Trailer": True,
        "md5Valid": True,
        "entryBytes": trailer_offset,
    }


def validate_esp_image(partition: bytes) -> dict[str, object]:
    if len(partition) < 24 or partition[0] != ESP_IMAGE_MAGIC:
        raise HilError("invalid ESP application image magic")
    segment_count = partition[1]
    if not 1 <= segment_count <= 16:
        raise HilError(f"invalid ESP application segment count: {segment_count}")

    offset = 24
    checksum = ESP_IMAGE_CHECKSUM_SEED
    for _ in range(segment_count):
        if offset + 8 > len(partition):
            raise HilError("truncated ESP application segment header")
        _, length = struct.unpack_from("<II", partition, offset)
        offset += 8
        if length > len(partition) - offset:
            raise HilError("truncated ESP application segment data")
        for value in partition[offset:offset + length]:
            checksum ^= value
        offset += length

    # ESP images pad the final segment so that the checksum is the last byte
    # of a 16-byte block.
    checksum_offset = offset | 0x0F
    if checksum_offset >= len(partition):
        raise HilError("ESP application image has no checksum byte")
    stored_checksum = partition[checksum_offset]
    if stored_checksum != checksum:
        raise HilError("ESP application checksum mismatch")

    hash_appended = partition[23] != 0
    logical_size = checksum_offset + 1
    validation_hash_valid = None
    if hash_appended:
        if logical_size + 32 > len(partition):
            raise HilError("ESP application validation hash is truncated")
        stored_hash = partition[logical_size:logical_size + 32]
        calculated_hash = hashlib.sha256(partition[:logical_size]).digest()
        validation_hash_valid = stored_hash == calculated_hash
        if not validation_hash_valid:
            raise HilError("ESP application validation hash mismatch")
        logical_size += 32

    return {
        "segmentCount": segment_count,
        "checksumValid": True,
        "hashAppended": hash_appended,
        "validationHashValid": validation_hash_valid,
        "logicalSize": logical_size,
    }


def validate_nvs(partition: bytes) -> dict[str, object]:
    if not partition or len(partition) % NVS_PAGE_SIZE:
        raise HilError("NVS partition is not page-aligned")

    page_states = {
        0xFFFFFFFF: "empty",
        0xFFFFFFFE: "active",
        0xFFFFFFFC: "full",
        0xFFFFFFF8: "freeing",
    }
    fixed_types = {0x01, 0x11, 0x02, 0x12, 0x04, 0x14, 0x08, 0x18}
    variable_types = {0x21, 0x41, 0x42}
    page_counts = {name: 0 for name in page_states.values()}
    page_header_crcs = 0
    entry_crcs = 0
    payload_crcs = 0
    written_items = 0

    for page_index in range(len(partition) // NVS_PAGE_SIZE):
        page = partition[
            page_index * NVS_PAGE_SIZE:(page_index + 1) * NVS_PAGE_SIZE
        ]
        state = struct.unpack_from("<I", page, 0)[0]
        if state not in page_states:
            raise HilError(f"NVS page {page_index} has an invalid state")
        state_name = page_states[state]
        page_counts[state_name] += 1
        if state_name == "empty":
            if page != b"\xff" * NVS_PAGE_SIZE:
                raise HilError(f"NVS empty page {page_index} contains programmed data")
            continue

        version = page[8]
        if version not in (0xFF, 0xFE):
            raise HilError(f"NVS page {page_index} has an unsupported version")
        stored_page_crc = struct.unpack_from("<I", page, 28)[0]
        calculated_page_crc = zlib.crc32(page[4:28], 0xFFFFFFFF) & 0xFFFFFFFF
        if stored_page_crc != calculated_page_crc:
            raise HilError(f"NVS page {page_index} header CRC mismatch")
        page_header_crcs += 1

        bitmap = page[32:64]
        states = [
            (bitmap[index // 4] >> ((index % 4) * 2)) & 0x03
            for index in range(NVS_ENTRY_COUNT)
        ]
        if 1 in states:
            raise HilError(f"NVS page {page_index} has an illegal entry state")

        consumed: set[int] = set()
        for entry_index, entry_state in enumerate(states):
            if entry_state != 2 or entry_index in consumed:
                continue
            offset = 64 + entry_index * NVS_ENTRY_SIZE
            entry = page[offset:offset + NVS_ENTRY_SIZE]
            item_type = entry[1]
            span = entry[2]
            if span < 1 or entry_index + span > NVS_ENTRY_COUNT:
                raise HilError(
                    f"NVS page {page_index} entry {entry_index} has an invalid span"
                )
            if any(states[index] != 2 for index in range(entry_index, entry_index + span)):
                raise HilError(
                    f"NVS page {page_index} entry {entry_index} span state mismatch"
                )
            consumed.update(range(entry_index, entry_index + span))

            stored_entry_crc = struct.unpack_from("<I", entry, 4)[0]
            calculated_entry_crc = zlib.crc32(
                entry[:4] + entry[8:32], 0xFFFFFFFF
            ) & 0xFFFFFFFF
            if stored_entry_crc != calculated_entry_crc:
                raise HilError(
                    f"NVS page {page_index} entry {entry_index} CRC mismatch"
                )
            entry_crcs += 1
            written_items += 1

            if item_type in fixed_types:
                if span != 1:
                    raise HilError(
                        f"NVS page {page_index} entry {entry_index} has invalid fixed span"
                    )
            elif item_type in variable_types:
                data_size = struct.unpack_from("<H", entry, 24)[0]
                stored_data_crc = struct.unpack_from("<I", entry, 28)[0]
                capacity = (span - 1) * NVS_ENTRY_SIZE
                if data_size > capacity:
                    raise HilError(
                        f"NVS page {page_index} entry {entry_index} payload exceeds span"
                    )
                payload_offset = 64 + (entry_index + 1) * NVS_ENTRY_SIZE
                payload = page[payload_offset:payload_offset + data_size]
                calculated_data_crc = zlib.crc32(payload, 0xFFFFFFFF) & 0xFFFFFFFF
                if stored_data_crc != calculated_data_crc:
                    raise HilError(
                        f"NVS page {page_index} entry {entry_index} payload CRC mismatch"
                    )
                payload_crcs += 1
            elif item_type == 0x48:
                if span != 1:
                    raise HilError(
                        f"NVS page {page_index} entry {entry_index} has invalid index span"
                    )
            else:
                raise HilError(
                    f"NVS page {page_index} entry {entry_index} has unknown type"
                )

    return {
        "valid": True,
        "pageCount": len(partition) // NVS_PAGE_SIZE,
        "pageStates": page_counts,
        "pageHeaderCrcsChecked": page_header_crcs,
        "writtenItems": written_items,
        "entryCrcsChecked": entry_crcs,
        "payloadCrcsChecked": payload_crcs,
    }


def find_mkspiffs() -> Path | None:
    for executable in (
        shutil.which("mkspiffs"),
        shutil.which("mkspiffs_espressif32_arduino"),
        shutil.which("mkspiffs_espressif32_arduino.exe"),
        str(Path.home() / ".platformio/packages/tool-mkspiffs/mkspiffs_espressif32_arduino"),
        str(Path.home() / ".platformio/packages/tool-mkspiffs/mkspiffs_espressif32_arduino.exe"),
    ):
        if executable and Path(executable).is_file():
            return Path(executable)
    return None


def validate_spiffs(partition: bytes, tool: Path | None = None) -> dict[str, object]:
    executable = tool or find_mkspiffs()
    if executable is None:
        raise HilError("mkspiffs is unavailable for offline SPIFFS validation")
    with tempfile.TemporaryDirectory(prefix="voxone-spiffs-inspect-") as temporary:
        root = Path(temporary)
        image = root / "spiffs.bin"
        destination = root / "unpacked"
        image.write_bytes(partition)
        image.chmod(0o600)
        result = subprocess.run(
            [
                str(executable), "-u", str(destination), "-b", "4096",
                "-p", "256", "-s", str(len(partition)), str(image),
            ],
            check=False, capture_output=True,
        )
        if result.returncode:
            raise HilError("SPIFFS integrity validation failed")
        files = [path for path in destination.rglob("*") if path.is_file()]
        return {
            "valid": True,
            "fileCount": len(files),
            "payloadBytes": sum(path.stat().st_size for path in files),
        }


def ota_record(raw: bytes, sector: int, app_count: int) -> dict[str, object]:
    sequence, _, state, stored_crc = struct.unpack("<I20sII", raw[:32])
    calculated_crc = esp_ota_crc32(sequence)
    crc_valid = stored_crc == calculated_crc
    selectable = (
        sequence != 0xFFFFFFFF
        and state not in (3, 4)
        and crc_valid
    )
    slot_index = (sequence - 1) % app_count if selectable and app_count else None
    return {
        "sector": sector,
        "sequence": sequence,
        "state": OTA_STATES.get(state, f"UNKNOWN_0x{state:08x}"),
        "storedCrc": f"{stored_crc:08x}",
        "calculatedCrc": f"{calculated_crc:08x}",
        "crcValid": crc_valid,
        "selectable": selectable,
        "slotIndex": slot_index,
    }


def inspect_image(image_path: Path, output_dir: Path | None = None) -> dict[str, object]:
    image = image_path.read_bytes()
    partitions = parse_partitions(image)
    partition_table = validate_partition_table(image)
    by_label = {str(part["label"]): part for part in partitions}
    required = {
        "nvs": (0x9000, 0x5000),
        "otadata": (0xE000, 0x2000),
        "app0": (0x10000, 0x1E0000),
        "app1": (0x1F0000, 0x1E0000),
        "spiffs": (0x3D0000, 0x30000),
    }
    for label, expected in required.items():
        actual = by_label.get(label)
        if actual is None or (actual["offset"], actual["size"]) != expected:
            raise HilError(f"partition {label} does not match VoxOne layout {expected}")
        if int(actual["offset"]) + int(actual["size"]) > len(image):
            raise HilError(f"partition {label} extends beyond captured flash")

    apps = sorted(
        (part for part in partitions if part["type"] == 0 and part["subtype"] in (0x10, 0x11)),
        key=lambda part: int(part["subtype"]),
    )
    ota = by_label["otadata"]
    ota_offset = int(ota["offset"])
    records = [
        ota_record(image[ota_offset:ota_offset + 32], 0, len(apps)),
        ota_record(image[ota_offset + 0x1000:ota_offset + 0x1000 + 32], 1, len(apps)),
    ]
    candidates = [record for record in records if record["selectable"]]
    active_record = max(candidates, key=lambda record: int(record["sequence"])) if candidates else None

    app_report: list[dict[str, object]] = []
    for index, app in enumerate(apps):
        offset, size = int(app["offset"]), int(app["size"])
        partition = image[offset:offset + size]
        states = [record["state"] for record in records if record["slotIndex"] == index]
        app_report.append(
            {
                "label": app["label"],
                "offset": offset,
                "size": size,
                "espImageMagic": image[offset] == ESP_IMAGE_MAGIC,
                "recordedStates": states,
                "partitionSha256": hashlib.sha256(partition).hexdigest(),
                "imageValidation": validate_esp_image(partition),
            }
        )

    active_slot = None
    if active_record is not None:
        index = int(active_record["slotIndex"])
        if 0 <= index < len(apps):
            active_slot = apps[index]["label"]
    previous_candidates = [
        app["label"] for index, app in enumerate(app_report)
        if app["label"] != active_slot
        and app["espImageMagic"]
        and "VALID" in app["recordedStates"]
    ]

    report: dict[str, object] = {
        "source": image_path.name,
        "size": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
        "partitions": partitions,
        "partitionTable": partition_table,
        "otadata": records,
        "selectedBootSlot": active_slot,
        "selectedRecordSector": active_record["sector"] if active_record else None,
        "apps": app_report,
        "nvsIntegrity": validate_nvs(
            image[
                int(by_label["nvs"]["offset"]):
                int(by_label["nvs"]["offset"]) + int(by_label["nvs"]["size"])
            ]
        ),
        "spiffsIntegrity": validate_spiffs(
            image[
                int(by_label["spiffs"]["offset"]):
                int(by_label["spiffs"]["offset"]) + int(by_label["spiffs"]["size"])
            ]
        ),
        "conservativePreviousValidCandidates": previous_candidates,
        "limitations": [
            "selectedBootSlot is inferred from otadata captured after manual ROM-loader entry",
            "offline inspection cannot prove chip revision, anti-rollback eFuse, or runtime health",
            "a rollback candidate is reported only with image magic and a CRC-valid VALID record",
            "NVS and SPIFFS reports expose aggregate integrity only, never keys, names, or values",
        ],
    }

    if output_dir is not None:
        for label in ("nvs", "otadata", "app0", "app1", "spiffs"):
            part = by_label[label]
            offset, size = int(part["offset"]), int(part["size"])
            target = output_dir / f"{label}-0x{offset:x}-0x{size:x}.bin"
            target.write_bytes(image[offset:offset + size])
            target.chmod(0o600)
        boot_region = output_dir / "boot-region-0x0-0x9000.bin"
        boot_region.write_bytes(image[:0x9000])
        boot_region.chmod(0o600)
        inspection = output_dir / "inspection.json"
        inspection.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        inspection.chmod(0o600)
    return report


def command_inspect(args: argparse.Namespace) -> int:
    try:
        report = inspect_image(args.image)
    except (HilError, OSError, struct.error) as error:
        print(f"RESULT BLOCKED: {error}")
        return 1
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


def command_capture(args: argparse.Namespace) -> int:
    if not args.confirm_rom_loader:
        print(
            "RESULT BLOCKED: capture requires --confirm-rom-loader after a separately "
            "approved manual BOOT+RESET entry"
        )
        return 2
    try:
        port = verify_usb_identity(M0_BY_ID, M0_VID_PID, "M0")
        holders = process_holders(port)
        if holders:
            raise HilError(f"M0 port still has holders: {[item['pid'] for item in holders]}")
        if args.output.exists():
            raise HilError(f"refusing to overwrite existing output: {args.output}")

        flash_info = run_esptool(port, ["flash_id"])
        flash_size = detected_flash_bytes(flash_info)
        security_info = run_esptool(port, ["get_security_info"])

        args.output.mkdir(mode=0o700, parents=True)
        args.output.chmod(0o700)
        full = args.output / "m0-full-flash.bin"
        run_esptool(port, ["read_flash", "0", hex(flash_size), str(full)])
        full.chmod(0o600)
        if full.stat().st_size != flash_size:
            raise HilError(
                f"short flash read: expected {flash_size}, got {full.stat().st_size}"
            )

        # The configured VoxOne layout occupies exactly the first 4 MiB.  A
        # second independent read verifies every byte needed for recovery,
        # including both app slots and all persistent configuration data.
        verified_size = 4 * 1024 * 1024
        verification = args.output / "m0-first-4m-verification.bin"
        run_esptool(
            port, ["read_flash", "0", hex(verified_size), str(verification)]
        )
        verification.chmod(0o600)
        if verification.stat().st_size != verified_size:
            raise HilError(
                f"short verification read: expected {verified_size}, "
                f"got {verification.stat().st_size}"
            )
        with full.open("rb") as full_stream, verification.open("rb") as verify_stream:
            if full_stream.read(verified_size) != verify_stream.read():
                raise HilError("independent reads disagree in the configured 4 MiB")

        device_info = args.output / "device-info.txt"
        device_info.write_text(flash_info + "\n" + security_info, encoding="utf-8")
        device_info.chmod(0o600)
        report = inspect_image(full, args.output)
        manifest = {
            "fullFlash": {
                "file": full.name,
                "size": full.stat().st_size,
                "sha256": sha256(full),
            },
            "configuredRegionVerification": {
                "file": verification.name,
                "size": verification.stat().st_size,
                "sha256": sha256(verification),
                "matchesFullFlashPrefix": True,
            },
            "selectedBootSlot": report["selectedBootSlot"],
            "conservativePreviousValidCandidates": report[
                "conservativePreviousValidCandidates"
            ],
            "deviceWasNotResetByTool": True,
            "flasherStubUsed": False,
            "restoreAuthorized": False,
        }
        manifest_path = args.output / "manifest.json"
        manifest_path.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        manifest_path.chmod(0o600)
    except (HilError, OSError, subprocess.SubprocessError, struct.error) as error:
        print(f"RESULT BLOCKED: {error}")
        return 1

    print(f"PASS full flash captured: {full.stat().st_size} bytes sha256={sha256(full)}")
    print(f"PASS offline selected boot slot: {report['selectedBootSlot']}")
    print(
        "INFO conservative previous VALID candidates: "
        f"{report['conservativePreviousValidCandidates']}"
    )
    print("INFO M0 remains in ROM loader; returning to the app needs separate approval")
    print("RESULT PASS")
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        description="Read-only M0 USB preflight, raw flash capture and offline OTA inspection"
    )
    subparsers = result.add_subparsers(dest="command", required=True)
    preflight = subparsers.add_parser("preflight", help="do not open either serial port")
    preflight.add_argument("--host", default="192.168.1.114")
    preflight.set_defaults(function=command_preflight)

    capture = subparsers.add_parser(
        "capture", help="read flash only; M0 must already be in the ROM loader"
    )
    capture.add_argument("--output", type=Path, required=True)
    capture.add_argument("--confirm-rom-loader", action="store_true")
    capture.set_defaults(function=command_capture)

    inspect = subparsers.add_parser("inspect", help="inspect an existing local raw dump")
    inspect.add_argument("image", type=Path)
    inspect.set_defaults(function=command_inspect)
    return result


def main() -> int:
    args = parser().parse_args()
    return args.function(args)


if __name__ == "__main__":
    sys.exit(main())
