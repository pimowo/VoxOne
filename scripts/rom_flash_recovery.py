#!/usr/bin/env python3
"""Fail-closed full-flash backup and restore tooling for VoxOne S0 and L0.

The tool never toggles reset/boot lines and never uploads a flasher stub.
Every device operation requires an operator to confirm that the correct MCU
was already put in the ROM downloader.  ``restore`` is the only writing
operation.  It is disabled unless a fresh restore plan, its bound token and
three explicit confirmations are supplied.

Raw images contain credentials and other private data.  Output directories
are created with mode 0700 and files with mode 0600.  They must stay outside
the repository.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from email.parser import Parser
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import socket
import stat
import subprocess
import sys
from typing import Any, Iterable, Mapping, Sequence


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_USB_REGISTRY = ROOT / "docs" / "DEV_USB_PORTS.json"
DEFAULT_TOOLCHAIN_MANIFEST = ROOT / "docs" / "ROM_FLASH_TOOLCHAIN.json"
PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_READ_SIZE = 0x1000

DEVICE_IDENTITY_SCHEMA = "voxone-rom-device-identity-v2"
BACKUP_STATE_SCHEMA = "voxone-rom-backup-state-v2"
VERIFIED_BACKUP_SCHEMA = "voxone-rom-verified-backup-v2"
RESTORE_PLAN_SCHEMA = "voxone-rom-restore-plan-v2"
RESTORE_APPROVAL_SCHEMA = "voxone-rom-restore-approval-v1"
RESTORE_RESULT_SCHEMA = "voxone-rom-restore-result-v2"
POSTCHECK_SCHEMA = "voxone-rom-postcheck-v2"
TOOLCHAIN_SCHEMA = "voxone-rom-toolchain-v2"
OFFSITE_ATTESTATION_SCHEMA = "voxone-rom-offsite-attestation-v1"

SHA256_RE = re.compile(r"[0-9a-f]{64}")
MAC_RE = re.compile(r"(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}")
FINGERPRINT_NAME_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}")
BOOT_ID_RE = re.compile(r"[0-9A-Fa-f]{8}")
PROFILE_NAMES = ("M0", "L0", "S0", "DIN0")
REQUIRED_LOGICAL_FINGERPRINTS = {"vxcf", "hardware", "stations"}
ESP32_S3_IMAGE_CHIP_ID = 9
RESTORE_PLAN_MAX_AGE = timedelta(hours=2)
TRUSTED_FINDMNT = Path("/usr/bin/findmnt")
TRUSTED_LSBLK = Path("/usr/bin/lsblk")
TRUSTED_UDEVADM = Path("/usr/bin/udevadm")


def _load_local_module(name: str, path: Path) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load local module {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules.setdefault(name, module)
    spec.loader.exec_module(module)
    return module


flash_tools = _load_local_module(
    "voxone_m0_flash_backup", ROOT / "scripts" / "m0_flash_backup.py"
)
spiffs_tools = _load_local_module(
    "voxone_spiffs_usb_recovery", ROOT / "scripts" / "spiffs_usb_recovery.py"
)


class RecoveryError(RuntimeError):
    """A safety precondition failed; no write may be attempted."""


class OperationInterrupted(RuntimeError):
    """A supported termination signal interrupted an operation."""


class FlashStateUnknown(RecoveryError):
    """A write began, but exact Flash contents were not subsequently proven."""


@dataclass(frozen=True)
class RuntimeSelection:
    profile: str | None = None
    root: Path | None = None
    confirm_port_open_may_reset: bool = False


_runtime_selection = RuntimeSelection()


@dataclass(frozen=True)
class ProfileSpec:
    name: str
    chip: str
    flash_size: int
    allowed_roles: tuple[str, ...]


PROFILE_SPECS = {
    "S0": ProfileSpec("S0", "esp32s3", 4 * 1024 * 1024, ("S0_NATIVE",)),
    "L0": ProfileSpec("L0", "esp32s3", 16 * 1024 * 1024, ("L0_NATIVE",)),
}


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def canonical_profile(value: str) -> str:
    profile = value.strip().upper()
    if profile not in PROFILE_SPECS:
        raise RecoveryError("profile must be S0 or L0")
    return profile


def canonical_mac(value: str) -> str:
    mac = value.strip().upper().replace("-", ":")
    if not MAC_RE.fullmatch(mac):
        raise RecoveryError("MAC must contain six colon-separated octets")
    return mac


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def trusted_subprocess_environment() -> dict[str, str]:
    """Minimal environment: no loader/Python injection or user-controlled PATH."""
    return {"PATH": "/usr/bin:/bin", "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8"}


def trusted_system_tool(path: Path) -> str:
    reject_final_symlink(path)
    info = path.stat()
    if not stat.S_ISREG(info.st_mode) or info.st_uid != 0:
        raise RecoveryError(f"system tool is not a root-owned regular file: {path}")
    if stat.S_IMODE(info.st_mode) & 0o022:
        raise RecoveryError(f"system tool is group/other writable: {path}")
    return str(path)


def esptool_package_tree_sha256(package_root: Path) -> str:
    """Hash all executable Python/data inputs loaded by the esptool wrapper."""
    digest = hashlib.sha256()
    included: list[Path] = [package_root / "esptool.py"]
    for directory_name in ("esptool", "_contrib"):
        directory = package_root / directory_name
        if not directory.is_dir():
            raise RecoveryError(f"approved esptool package is incomplete: {directory}")
        included.extend(
            path for path in directory.rglob("*")
            if path.is_file()
            and "__pycache__" not in path.parts
            and path.suffix not in {".pyc", ".pyo"}
        )
    for path in sorted(included, key=lambda item: item.relative_to(package_root).as_posix()):
        reject_symlink_components(path)
        relative = path.relative_to(package_root).as_posix().encode("utf-8")
        digest.update(relative + b"\0")
        digest.update(bytes.fromhex(sha256_file(path)))
        digest.update(b"\0")
    return digest.hexdigest()


def configure_runtime_selection(
    profile: str | None = None,
    root: Path | None = None,
    confirm_port_open_may_reset: bool = False,
) -> None:
    """Select a hash-pinned runtime without accepting executable overrides."""
    global _runtime_selection
    if profile is not None and not FINGERPRINT_NAME_RE.fullmatch(profile):
        raise RecoveryError("invalid runtime profile name")
    _runtime_selection = RuntimeSelection(
        profile=profile,
        root=root.expanduser().absolute() if root is not None else None,
        confirm_port_open_may_reset=confirm_port_open_may_reset,
    )


def runtime_path(root: Path, relative: Any, label: str) -> Path:
    value = Path(str(relative))
    if value.is_absolute() or ".." in value.parts or not value.parts:
        raise RecoveryError(f"{label} must be a safe relative runtime path")
    candidate = Path(os.path.abspath(root / value))
    try:
        candidate.relative_to(root)
    except ValueError as error:
        raise RecoveryError(f"{label} escapes the runtime root") from error
    return candidate


def distribution_tree_sha256(
    distribution_root: Path,
    dist_info: str,
) -> tuple[str, int]:
    """Hash installed files listed by RECORD, excluding scripts outside site root."""
    distribution_root = distribution_root.resolve(strict=True)
    record = runtime_path(
        distribution_root, Path(dist_info) / "RECORD", "dependency RECORD"
    )
    if not record.is_file():
        raise RecoveryError(f"dependency RECORD is missing: {dist_info}")
    included: dict[str, Path] = {}
    try:
        with record.open(newline="", encoding="utf-8") as stream:
            rows = list(csv.reader(stream))
    except (OSError, UnicodeError, csv.Error) as error:
        raise RecoveryError(f"cannot parse dependency RECORD: {dist_info}") from error
    for row in rows:
        if not row:
            continue
        candidate = (distribution_root / row[0]).resolve(strict=False)
        try:
            relative = candidate.relative_to(distribution_root)
        except ValueError:
            # Console entry points live in the venv bin directory and are not
            # imported by esptool.  Runtime module files remain mandatory.
            continue
        if "__pycache__" in relative.parts or relative.suffix in {".pyc", ".pyo"}:
            continue
        if not candidate.is_file():
            raise RecoveryError(f"dependency file is missing: {relative}")
        included[relative.as_posix()] = candidate
    digest = hashlib.sha256()
    for relative in sorted(included):
        digest.update(relative.encode("utf-8") + b"\0")
        digest.update(bytes.fromhex(sha256_file(included[relative])))
        digest.update(b"\0")
    return digest.hexdigest(), len(included)


def dependency_metadata(distribution_root: Path, dist_info: str) -> tuple[str, str]:
    metadata = runtime_path(
        distribution_root.resolve(strict=True),
        Path(dist_info) / "METADATA",
        "dependency METADATA",
    )
    if not metadata.is_file():
        raise RecoveryError(f"dependency METADATA is missing: {dist_info}")
    try:
        message = Parser().parsestr(metadata.read_text(encoding="utf-8"))
    except (OSError, UnicodeError) as error:
        raise RecoveryError(f"cannot parse dependency METADATA: {dist_info}") from error
    return str(message.get("Name", "")), str(message.get("Version", ""))


def select_toolchain_profile(
    manifest: Mapping[str, Any],
) -> tuple[str, Mapping[str, Any]]:
    profiles = manifest.get("profiles")
    if not isinstance(profiles, dict) or not profiles:
        raise RecoveryError("toolchain manifest has no profiles")
    system = platform.system()
    architecture = platform.machine().lower()
    requested = _runtime_selection.profile
    if requested is not None:
        profile = profiles.get(requested)
        if not isinstance(profile, dict):
            raise RecoveryError(f"unknown runtime profile: {requested}")
        candidates = [(requested, profile)]
    else:
        candidates = [
            (str(name), value)
            for name, value in profiles.items()
            if isinstance(value, dict)
            and value.get("system") == system
            and architecture
            in {
                str(item).lower()
                for item in value.get("architectureAliases", [])
            }
        ]
    if len(candidates) != 1:
        raise RecoveryError(
            f"no unique runtime profile for {system}/{architecture}"
        )
    name, profile = candidates[0]
    aliases = {
        str(item).lower() for item in profile.get("architectureAliases", [])
    }
    if profile.get("system") != system or architecture not in aliases:
        raise RecoveryError(
            f"runtime profile {name} does not match {system}/{architecture}"
        )
    return name, profile


def canonical_json(value: Mapping[str, Any]) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def parse_utc(value: Any, label: str) -> datetime:
    try:
        parsed = datetime.fromisoformat(str(value).replace("Z", "+00:00"))
    except ValueError as error:
        raise RecoveryError(f"{label} is not an ISO-8601 timestamp") from error
    if parsed.tzinfo is None:
        raise RecoveryError(f"{label} has no timezone")
    return parsed.astimezone(timezone.utc)


def require_outside_repo(path: Path) -> None:
    resolved = path.expanduser().resolve(strict=False)
    try:
        resolved.relative_to(ROOT)
    except ValueError:
        return
    raise RecoveryError(f"sensitive recovery artifact must stay outside repository: {path}")


def reject_final_symlink(path: Path) -> None:
    try:
        info = path.lstat()
    except FileNotFoundError:
        return
    if stat.S_ISLNK(info.st_mode):
        raise RecoveryError(f"symlink is not allowed for recovery artifact: {path}")


def reject_symlink_components(path: Path) -> None:
    """Reject any existing symlink in an artifact path, including parents."""
    current = path.expanduser().absolute()
    for component in (current, *current.parents):
        try:
            info = component.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode):
            raise RecoveryError(
                f"symlink component is not allowed for recovery artifact: {component}"
            )


def require_new_private_dir(path: Path) -> Path:
    require_outside_repo(path)
    reject_symlink_components(path)
    if path.exists():
        raise RecoveryError(f"refusing to reuse output directory: {path}")
    path.mkdir(mode=0o700, parents=True)
    path.chmod(0o700)
    return path.resolve()


def require_private_dir(path: Path) -> None:
    reject_symlink_components(path)
    try:
        info = path.stat()
    except FileNotFoundError as error:
        raise RecoveryError(f"private directory is missing: {path}") from error
    if not stat.S_ISDIR(info.st_mode):
        raise RecoveryError(f"not a directory: {path}")
    if stat.S_IMODE(info.st_mode) & 0o077:
        raise RecoveryError(f"private directory has group/other permissions: {path}")


def require_private_file(path: Path) -> None:
    reject_symlink_components(path)
    try:
        info = path.stat()
    except FileNotFoundError as error:
        raise RecoveryError(f"private artifact is missing: {path}") from error
    if not stat.S_ISREG(info.st_mode):
        raise RecoveryError(f"not a regular file: {path}")
    if stat.S_IMODE(info.st_mode) & 0o077:
        raise RecoveryError(f"private artifact has group/other permissions: {path}")


def create_private_empty(path: Path) -> None:
    if path.exists() or path.is_symlink():
        raise RecoveryError(f"refusing to overwrite existing file: {path}")
    descriptor = os.open(
        path,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
        0o600,
    )
    os.close(descriptor)


def copy_private_file(source: Path, target: Path) -> dict[str, Any]:
    require_private_file(source)
    if target.exists() or target.is_symlink():
        raise RecoveryError(f"refusing to overwrite private copy: {target}")
    descriptor = os.open(
        target,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
        0o600,
    )
    try:
        with source.open("rb") as input_stream, os.fdopen(descriptor, "wb") as output_stream:
            shutil.copyfileobj(input_stream, output_stream, 1024 * 1024)
            output_stream.flush()
            os.fsync(output_stream.fileno())
    except BaseException:
        target.chmod(0o600)
        raise
    if target.stat().st_size != source.stat().st_size or sha256_file(target) != sha256_file(source):
        raise RecoveryError(f"private copy verification failed: {target}")
    return {
        "file": target.name,
        "size": target.stat().st_size,
        "sha256": sha256_file(target),
    }


def write_private_bytes(path: Path, value: bytes) -> None:
    if path.exists() or path.is_symlink():
        raise RecoveryError(f"refusing to overwrite existing file: {path}")
    descriptor = os.open(
        path,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
        0o600,
    )
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(value)
            stream.flush()
            os.fsync(stream.fileno())
    except BaseException:
        try:
            path.unlink()
        except OSError:
            pass
        raise


def write_private_json(path: Path, value: Mapping[str, Any]) -> None:
    write_private_bytes(
        path, json.dumps(value, indent=2, sort_keys=True).encode("utf-8") + b"\n"
    )


def append_event(directory: Path, operation: str, status: str, **fields: Any) -> None:
    record = {
        "at": now_iso(),
        "operation": operation,
        "status": status,
        **fields,
    }
    target = directory / "operation-log.jsonl"
    flags = os.O_WRONLY | os.O_CREAT | os.O_APPEND | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(target, flags, 0o600)
    with os.fdopen(descriptor, "ab") as stream:
        stream.write(canonical_json(record) + b"\n")
        stream.flush()
        os.fsync(stream.fileno())


def load_json(path: Path, expected_schema: str | None = None) -> dict[str, Any]:
    reject_final_symlink(path)
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise RecoveryError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise RecoveryError(f"JSON root is not an object: {path}")
    if expected_schema is not None and value.get("schema") != expected_schema:
        raise RecoveryError(
            f"unexpected schema in {path}: {value.get('schema')!r}"
        )
    return value


def _flatten_block_devices(items: Sequence[Mapping[str, Any]]) -> dict[str, Mapping[str, Any]]:
    flattened: dict[str, Mapping[str, Any]] = {}
    for item in items:
        name = str(item.get("name", ""))
        if name:
            flattened[name] = item
        children = item.get("children")
        if isinstance(children, list):
            flattened.update(_flatten_block_devices(children))
    return flattened


def storage_identity(path: Path) -> dict[str, Any]:
    resolved = path.resolve(strict=True)
    result = subprocess.run(
        [
            trusted_system_tool(TRUSTED_FINDMNT), "--json", "--target", str(resolved),
            "-o", "TARGET,SOURCE,FSTYPE,UUID,MAJ:MIN,FSROOT",
        ],
        check=False,
        capture_output=True,
        text=True,
        env=trusted_subprocess_environment(),
    )
    if result.returncode:
        raise RecoveryError(f"cannot identify storage mount for {resolved}")
    try:
        filesystems = json.loads(result.stdout)["filesystems"]
        mount = filesystems[0]
    except (KeyError, IndexError, TypeError, json.JSONDecodeError) as error:
        raise RecoveryError(f"invalid findmnt result for {resolved}") from error
    source = str(mount.get("source", ""))
    fstype = str(mount.get("fstype", ""))
    identity: dict[str, Any] = {
        "host": socket.gethostname(),
        "target": str(mount.get("target", "")),
        "source": source,
        "fstype": fstype,
        "uuid": mount.get("uuid"),
        "majorMinor": str(mount.get("maj:min", "")),
        "fsRoot": str(mount.get("fsroot", "")),
        "deviceClass": "unsupported",
        "durable": False,
        "failureDomain": None,
    }
    source_device = source.split("[", 1)[0]
    if source_device.startswith("/dev/"):
        block = subprocess.run(
            [trusted_system_tool(TRUSTED_LSBLK), "--json", "-p", "-o", "NAME,PKNAME,TYPE,SERIAL,WWN,MAJ:MIN"],
            check=False,
            capture_output=True,
            text=True,
            env=trusted_subprocess_environment(),
        )
        if block.returncode:
            raise RecoveryError("cannot identify the physical block device")
        try:
            devices = _flatten_block_devices(json.loads(block.stdout)["blockdevices"])
        except (KeyError, TypeError, json.JSONDecodeError) as error:
            raise RecoveryError("invalid lsblk result") from error
        current = devices.get(source_device)
        if current is None:
            raise RecoveryError(f"mount source is absent from lsblk: {source_device}")
        visited: set[str] = set()
        while current.get("pkname"):
            name = str(current["name"])
            if name in visited:
                raise RecoveryError("block-device parent cycle detected")
            visited.add(name)
            parent = devices.get(str(current["pkname"]))
            if parent is None:
                raise RecoveryError("block-device parent identity is incomplete")
            current = parent
        media_id = current.get("wwn") or current.get("serial") or current.get("maj:min")
        if not media_id:
            raise RecoveryError("physical block device has no stable identity")
        identity.update(
            {
                "deviceClass": "block",
                "durable": True,
                "physicalDevice": str(current.get("name")),
                "mediaId": str(media_id),
                "failureDomain": f"block:{current.get('name')}:{media_id}",
            }
        )
    elif fstype.lower() in {"nfs", "nfs4", "cifs", "smb3", "sshfs", "fuse.sshfs"}:
        if source.startswith("//"):
            remote_host = source[2:].split("/", 1)[0]
        else:
            remote_host = source.split(":", 1)[0]
        if not remote_host or remote_host.lower() in {
            "localhost", "127.0.0.1", "::1", socket.gethostname().lower()
        }:
            raise RecoveryError("network storage is not on an independent host")
        identity.update(
            {
                "deviceClass": "network",
                "durable": True,
                "remoteHost": remote_host,
                "failureDomain": f"network:{remote_host}:{source}",
            }
        )
    return identity


def storage_identities_match(recorded: Mapping[str, Any], actual: Mapping[str, Any]) -> bool:
    keys = (
        "host", "target", "source", "fstype", "uuid", "majorMinor", "fsRoot",
        "deviceClass", "durable", "failureDomain",
    )
    return all(recorded.get(key) == actual.get(key) for key in keys)


def validate_offsite_attestation(
    path: Path,
    bench: Mapping[str, Any],
    offsite: Mapping[str, Any],
) -> dict[str, Any]:
    require_outside_repo(path)
    require_private_file(path)
    value = load_json(path, OFFSITE_ATTESTATION_SCHEMA)
    if value.get("independentPhysicalMediaConfirmed") is not True:
        raise RecoveryError("offsite attestation does not confirm independent media")
    if value.get("offBenchCopyConfirmed") is not True:
        raise RecoveryError("offsite attestation does not confirm an off-bench copy")
    if not str(value.get("operator", "")).strip() or not str(value.get("approvalId", "")).strip():
        raise RecoveryError("offsite attestation has no operator/approval identity")
    confirmed = parse_utc(value.get("confirmedAt"), "offsite confirmedAt")
    if confirmed > datetime.now(timezone.utc) + timedelta(minutes=5):
        raise RecoveryError("offsite attestation timestamp is in the future")
    if value.get("benchFailureDomain") != bench.get("failureDomain"):
        raise RecoveryError("offsite attestation bench failure domain mismatch")
    if value.get("offsiteFailureDomain") != offsite.get("failureDomain"):
        raise RecoveryError("offsite attestation storage failure domain mismatch")
    if (
        bench.get("durable") is not True
        or offsite.get("durable") is not True
        or bench.get("failureDomain") == offsite.get("failureDomain")
    ):
        raise RecoveryError("offsite storage is not a distinct durable failure domain")
    return value


def parse_fingerprints(items: Iterable[str]) -> dict[str, dict[str, Any]]:
    fingerprints: dict[str, dict[str, Any]] = {}
    for item in items:
        name, separator, raw_path = item.partition("=")
        if not separator or not FINGERPRINT_NAME_RE.fullmatch(name):
            raise RecoveryError("fingerprints must use NAME=/private/path")
        if name in fingerprints:
            raise RecoveryError(f"duplicate fingerprint name: {name}")
        path = Path(raw_path).resolve(strict=True)
        if not path.is_file():
            raise RecoveryError(f"fingerprint source is not a file: {path}")
        require_outside_repo(path)
        require_private_file(path)
        fingerprints[name] = {
            "size": path.stat().st_size,
            "sha256": sha256_file(path),
        }
    return fingerprints


def load_usb_role(registry_path: Path, role_name: str, profile: str) -> dict[str, Any]:
    reject_final_symlink(registry_path)
    registry_path = registry_path.resolve(strict=True)
    registry = load_json(registry_path)
    roles = registry.get("roles")
    if not isinstance(roles, list):
        raise RecoveryError("USB registry has no roles array")
    matches = [item for item in roles if isinstance(item, dict) and item.get("name") == role_name]
    if len(matches) != 1:
        raise RecoveryError(f"USB role {role_name} is missing or ambiguous")
    role = dict(matches[0])
    spec = PROFILE_SPECS[canonical_profile(profile)]
    if role_name not in spec.allowed_roles:
        raise RecoveryError(f"USB role {role_name} is not allowed for {spec.name}")
    required = {"vid", "pid", "serial", "byId", "identityConfidence"}
    if not required.issubset(role):
        raise RecoveryError(f"USB role {role_name} is incomplete")
    if role.get("identityConfidence") != "CONFIRMED":
        raise RecoveryError(f"USB role {role_name} identity is not CONFIRMED")
    by_id = Path(str(role["byId"]))
    if by_id.parent != Path("/dev/serial/by-id"):
        raise RecoveryError("USB role must use a canonical /dev/serial/by-id path")
    role["registrySha256"] = sha256_file(registry_path)
    return role


def udev_properties(device: Path) -> dict[str, str]:
    result = subprocess.run(
        [trusted_system_tool(TRUSTED_UDEVADM), "info", "--query=property", f"--name={device}"],
        check=False,
        capture_output=True,
        text=True,
        env=trusted_subprocess_environment(),
    )
    if result.returncode:
        raise RecoveryError("udev identity query failed")
    properties: dict[str, str] = {}
    for line in result.stdout.splitlines():
        key, separator, value = line.partition("=")
        if separator:
            properties[key] = value
    return properties


def verify_usb_role(role: Mapping[str, Any]) -> Path:
    path = Path(str(role["byId"]))
    try:
        device = path.resolve(strict=True)
    except OSError as error:
        raise RecoveryError(f"USB by-id path is unavailable: {path}") from error
    properties = udev_properties(device)
    actual = (
        properties.get("ID_VENDOR_ID", "").lower(),
        properties.get("ID_MODEL_ID", "").lower(),
        properties.get("ID_SERIAL_SHORT", ""),
    )
    expected = (
        str(role["vid"]).lower(),
        str(role["pid"]).lower(),
        str(role["serial"]),
    )
    if actual != expected:
        raise RecoveryError(f"USB identity mismatch for {role['name']}")
    holders = flash_tools.process_holders(device)
    if holders:
        raise RecoveryError(
            f"serial port has holders: {[int(item['pid']) for item in holders]}"
        )
    return device


def resolve_esptool_runtime(
    manifest_path: Path = DEFAULT_TOOLCHAIN_MANIFEST,
) -> tuple[Path, Path, Path]:
    """Resolve only the hash-pinned runtime before touching a serial port."""
    if os.environ.get("VOXONE_PIO_PYTHON") or os.environ.get("VOXONE_ESPTOOL"):
        raise RecoveryError("physical mode rejects VOXONE_PIO_PYTHON/VOXONE_ESPTOOL overrides")
    reject_final_symlink(manifest_path)
    manifest_path = manifest_path.resolve(strict=True)
    manifest = load_json(manifest_path, TOOLCHAIN_SCHEMA)
    _, profile = select_toolchain_profile(manifest)
    python_record = profile.get("python")
    esptool_record = profile.get("esptool")
    retry_guard = manifest.get("writeRetryGuard")
    if (
        not isinstance(python_record, dict)
        or not isinstance(esptool_record, dict)
        or not isinstance(retry_guard, dict)
    ):
        raise RecoveryError("toolchain runtime profile is incomplete")
    if (
        retry_guard.get("maxBlockAttempts") != 1
        or retry_guard.get("maxWriteFlashAttempts") != 1
    ):
        raise RecoveryError("write retry guard policy is not single-attempt")
    retry_guard_path = runtime_path(
        ROOT, retry_guard.get("relativePath"), "write retry guard path"
    )
    reject_symlink_components(retry_guard_path)
    if (
        not retry_guard_path.is_file()
        or sha256_file(retry_guard_path) != retry_guard.get("sha256")
    ):
        raise RecoveryError("approved write retry guard SHA256 is unavailable")
    root = _runtime_selection.root
    if root is None:
        if profile.get("requiresExplicitRoot") is True:
            raise RecoveryError("runtime profile requires explicit --runtime-root")
        root = Path.home()
    try:
        root = root.resolve(strict=True)
    except OSError as error:
        raise RecoveryError("runtime root is unavailable") from error
    if not root.is_dir():
        raise RecoveryError("runtime root is not a directory")
    python_path = runtime_path(
        root, python_record.get("relativePath"), "Python runtime path"
    )
    esptool_path = runtime_path(
        root, esptool_record.get("relativePath"), "esptool runtime path"
    )
    if not python_path.is_file() or sha256_file(python_path) != python_record.get("sha256"):
        raise RecoveryError("approved Python SHA256 is unavailable")
    if not esptool_path.is_file() or sha256_file(esptool_path) != esptool_record.get("sha256"):
        raise RecoveryError("approved esptool SHA256 is unavailable")
    if esptool_package_tree_sha256(esptool_path.parent) != esptool_record.get(
        "packageTreeSha256"
    ):
        raise RecoveryError("approved esptool package tree SHA256 mismatch")
    dependency_versions = manifest.get("dependencies")
    dependency_roots = profile.get("dependencyRoots")
    dependencies = profile.get("dependencies")
    if (
        not isinstance(dependency_versions, dict)
        or not isinstance(dependency_roots, dict)
        or not isinstance(dependencies, dict)
        or set(dependencies) != set(dependency_versions)
    ):
        raise RecoveryError("toolchain dependency manifest is incomplete")
    for name in sorted(dependency_versions):
        record = dependencies.get(name)
        if not isinstance(record, dict):
            raise RecoveryError(f"dependency record is invalid: {name}")
        root_name = str(record.get("root", ""))
        if root_name not in dependency_roots:
            raise RecoveryError(f"dependency root is invalid: {name}")
        distribution_root = runtime_path(
            root, dependency_roots[root_name], f"dependency root {name}"
        )
        dist_info = str(record.get("distInfo", ""))
        metadata_name, metadata_version = dependency_metadata(
            distribution_root, dist_info
        )
        if metadata_name.lower() != name.lower():
            raise RecoveryError(f"dependency name mismatch: {name}")
        if metadata_version != str(dependency_versions[name]):
            raise RecoveryError(f"dependency version mismatch: {name}")
        tree_sha256, file_count = distribution_tree_sha256(
            distribution_root, dist_info
        )
        if (
            tree_sha256 != record.get("treeSha256")
            or file_count != record.get("fileCount")
        ):
            raise RecoveryError(f"dependency tree mismatch: {name}")
    python_version = subprocess.run(
        [str(python_path), "-E", "-s", "--version"], check=False,
        capture_output=True, text=True, env=trusted_subprocess_environment(),
    )
    if python_version.returncode or str(python_record.get("version")) not in (
        python_version.stdout + python_version.stderr
    ):
        raise RecoveryError("approved Python version check failed")
    esptool_version = subprocess.run(
        [
            str(python_path), "-E", "-s", str(retry_guard_path),
            "--pinned-esptool-root", str(esptool_path.parent), "version",
        ],
        check=False, capture_output=True, text=True,
        env=trusted_subprocess_environment(),
    )
    if esptool_version.returncode or str(esptool_record.get("version")) not in (
        esptool_version.stdout + esptool_version.stderr
    ):
        raise RecoveryError("approved esptool version check failed")
    return python_path, esptool_path, retry_guard_path


def esptool_base(profile: str, port: Path) -> list[str]:
    spec = PROFILE_SPECS[canonical_profile(profile)]
    python_path, esptool_path, retry_guard_path = resolve_esptool_runtime()
    return [
        str(python_path),
        "-E", "-s",
        str(retry_guard_path),
        "--pinned-esptool-root", str(esptool_path.parent),
        "--chip", spec.chip,
        "--port", str(port),
        "--baud", "460800",
        "--before", "no_reset",
        "--after", "no_reset",
        "--no-stub",
    ]


def esptool_command(profile: str, port: Path, operation: Sequence[str]) -> list[str]:
    return esptool_base(profile, port) + list(operation)


def run_esptool(
    profile: str,
    port: Path,
    operation: Sequence[str],
    log_directory: Path,
    phase: str,
) -> str:
    require_private_dir(log_directory)
    if not FINGERPRINT_NAME_RE.fullmatch(phase):
        raise RecoveryError("invalid esptool log phase")
    command = esptool_command(profile, port, operation)
    if not _runtime_selection.confirm_port_open_may_reset:
        raise RecoveryError(
            "opening the serial port may assert DTR/RTS; "
            "--confirm-port-open-may-reset is required"
        )
    log_path = log_directory / f"esptool-{phase}.log"
    if log_path.exists() or log_path.is_symlink():
        raise RecoveryError(f"esptool phase log already exists: {phase}")
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=trusted_subprocess_environment(),
    )
    output = ""
    try:
        output, _ = process.communicate()
    except BaseException:
        process.terminate()
        try:
            tail, _ = process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            tail, _ = process.communicate()
        output += tail or ""
        write_private_bytes(log_path, output.encode("utf-8", "replace"))
        append_event(
            log_directory,
            "ESPTOOL",
            "INTERRUPTED",
            phase=phase,
            command=str(operation[0]),
            outputSha256=sha256_bytes(output.encode("utf-8", "replace")),
        )
        raise
    write_private_bytes(log_path, output.encode("utf-8", "replace"))
    append_event(
        log_directory,
        "ESPTOOL",
        "PASS" if process.returncode == 0 else "FAIL",
        phase=phase,
        command=str(operation[0]),
        returnCode=process.returncode,
        outputSha256=sha256_bytes(output.encode("utf-8", "replace")),
    )
    if process.returncode:
        raise RecoveryError(
            f"esptool {operation[0]} failed with rc={process.returncode}; "
            f"private evidence: {log_path.name}"
        )
    return output


def parse_chip(output: str) -> str:
    if re.search(r"ESP32[- ]S3", output, re.IGNORECASE):
        return "esp32s3"
    raise RecoveryError("esptool did not identify an ESP32-S3")


def parse_mac(output: str) -> str:
    matches = MAC_RE.findall(output)
    if not matches:
        raise RecoveryError("esptool did not report a MAC")
    unique = {canonical_mac(value) for value in matches}
    if len(unique) != 1:
        raise RecoveryError("esptool reported ambiguous MAC values")
    return unique.pop()


def parse_flash_size(output: str) -> int:
    try:
        return int(flash_tools.detected_flash_bytes(output))
    except flash_tools.HilError as error:
        raise RecoveryError(str(error)) from error


def live_identity(
    profile: str,
    role: Mapping[str, Any],
    expected_mac: str,
    log_directory: Path,
    phase_prefix: str,
) -> tuple[Path, dict[str, Any]]:
    profile = canonical_profile(profile)
    expected_mac = canonical_mac(expected_mac)
    spec = PROFILE_SPECS[profile]
    port = verify_usb_role(role)
    chip_output = run_esptool(
        profile, port, ["chip_id"], log_directory, f"{phase_prefix}-chip-id"
    )
    flash_output = run_esptool(
        profile, port, ["flash_id"], log_directory, f"{phase_prefix}-flash-id"
    )
    security_output = run_esptool(
        profile,
        port,
        ["get_security_info"],
        log_directory,
        f"{phase_prefix}-security-info",
    )
    chip = parse_chip(chip_output)
    mac = parse_mac(chip_output)
    flash_size = parse_flash_size(flash_output)
    if chip != spec.chip:
        raise RecoveryError(f"chip mismatch: expected {spec.chip}, got {chip}")
    if flash_size != spec.flash_size:
        raise RecoveryError(
            f"flash size mismatch for {profile}: expected {spec.flash_size}, got {flash_size}"
        )
    if mac != expected_mac:
        raise RecoveryError(f"MAC mismatch: expected {expected_mac}, got {mac}")
    return port, {
        "profile": profile,
        "chip": chip,
        "mac": mac,
        "flashSize": flash_size,
        "usbRole": str(role["name"]),
        "usbVid": str(role["vid"]).lower(),
        "usbPid": str(role["pid"]).lower(),
        "usbSerial": str(role["serial"]),
        "usbById": str(role["byId"]),
        "usbRegistrySha256": str(role["registrySha256"]),
        "securityInfoSha256": sha256_bytes(security_output.encode("utf-8")),
        "toolchainManifestSha256": sha256_file(DEFAULT_TOOLCHAIN_MANIFEST),
        "resetLinesUsed": "UNVERIFIED",
        "automaticResetRequested": False,
        "portOpenMayAssertDtrRts": True,
        "operatorAcceptedPortOpenResetRisk": True,
        "flasherStubUsed": False,
    }


def expected_partitions(profile: str) -> list[dict[str, Any]]:
    return [dict(item) for item in spiffs_tools.expected_partitions(profile)]


def normalized_partitions(items: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [dict(item) for item in spiffs_tools.normalized_partitions(items)]


def inspect_backup_image(profile: str, image: Path) -> dict[str, Any]:
    profile = canonical_profile(profile)
    spec = PROFILE_SPECS[profile]
    if not image.is_file():
        raise RecoveryError(f"backup image is missing: {image}")
    size = image.stat().st_size
    if size != spec.flash_size:
        raise RecoveryError(
            f"{profile} backup size mismatch: expected {spec.flash_size}, got {size}"
        )
    raw = image.read_bytes()
    boot_region = raw[:PARTITION_TABLE_OFFSET]
    if not boot_region or boot_region == b"\xff" * len(boot_region):
        raise RecoveryError("bootloader region is empty")
    try:
        boot_validation = flash_tools.validate_esp_image(boot_region)
    except flash_tools.HilError as error:
        raise RecoveryError(f"bootloader validation failed: {error}") from error
    chip_id = int.from_bytes(boot_region[12:14], "little")
    if chip_id != ESP32_S3_IMAGE_CHIP_ID:
        raise RecoveryError(
            f"bootloader chip id mismatch: expected {ESP32_S3_IMAGE_CHIP_ID}, got {chip_id}"
        )
    boot_logical_size = int(boot_validation["logicalSize"])
    if boot_logical_size <= 0 or boot_logical_size > PARTITION_TABLE_OFFSET:
        raise RecoveryError("bootloader logical size is outside the boot region")
    if any(value != 0xFF for value in boot_region[boot_logical_size:]):
        raise RecoveryError("bootloader has programmed bytes beyond its validated image")
    try:
        actual = normalized_partitions(flash_tools.parse_partitions(raw))
    except (flash_tools.HilError, ValueError) as error:
        raise RecoveryError(str(error)) from error
    expected = normalized_partitions(expected_partitions(profile))
    if actual != expected:
        raise RecoveryError("backup partition table does not match the profile")
    try:
        report = flash_tools.inspect_image(image)
    except (flash_tools.HilError, OSError, ValueError) as error:
        raise RecoveryError(str(error)) from error
    if report.get("nvsIntegrity", {}).get("valid") is not True:
        raise RecoveryError("backup NVS integrity validation failed")
    if report.get("spiffsIntegrity", {}).get("valid") is not True:
        raise RecoveryError("backup SPIFFS integrity validation failed")
    by_label = {str(item["label"]): item for item in actual}
    app_profiles: dict[str, str] = {}
    for app in report["apps"]:
        if not app.get("bootable"):
            continue
        validation = app.get("imageValidation")
        if not isinstance(validation, dict):
            raise RecoveryError(f"bootable {app['label']} has no image validation")
        offset = int(app["offset"])
        logical_size = int(validation["logicalSize"])
        payload = raw[offset:offset + logical_size]
        present = [
            name for name in PROFILE_NAMES
            if b"\0" + name.encode("ascii") + b"\0" in payload
        ]
        if present != [profile]:
            raise RecoveryError(
                f"application {app['label']} profile identity mismatch: {present}"
            )
        app_profiles[str(app["label"])] = profile
    partition_hashes = {}
    for label, item in by_label.items():
        offset, length = int(item["offset"]), int(item["size"])
        partition_hashes[label] = {
            "offset": offset,
            "size": length,
            "sha256": sha256_bytes(raw[offset:offset + length]),
        }
    return {
        "profile": profile,
        "size": size,
        "sha256": sha256_bytes(raw),
        "bootloader": {
            "offset": 0,
            "regionSize": PARTITION_TABLE_OFFSET,
            "logicalSize": boot_logical_size,
            "sha256": sha256_bytes(boot_region[:boot_logical_size]),
            "regionSha256": sha256_bytes(boot_region),
            "chipId": chip_id,
            "validation": boot_validation,
        },
        "partitions": actual,
        "partitionHashes": partition_hashes,
        "partitionTable": report["partitionTable"],
        "otadata": report["otadataSummary"],
        "selectedBootSlot": report["selectedBootSlot"],
        "rollbackAvailable": report["rollbackAvailable"],
        "apps": report["apps"],
        "applicationProfiles": app_profiles,
        "nvsIntegrity": report["nvsIntegrity"],
        "spiffsIntegrity": report["spiffsIntegrity"],
    }


def validate_device_identity(device: Mapping[str, Any]) -> tuple[str, str, str]:
    profile = canonical_profile(str(device.get("profile", "")))
    mac = canonical_mac(str(device.get("mac", "")))
    role = str(device.get("usbRole", ""))
    spec = PROFILE_SPECS[profile]
    if role not in spec.allowed_roles:
        raise RecoveryError("identity manifest role/profile mismatch")
    if device.get("chip") != spec.chip or device.get("flashSize") != spec.flash_size:
        raise RecoveryError("identity manifest chip/flash mismatch")
    for key in (
        "usbVid", "usbPid", "usbSerial", "usbById", "usbRegistrySha256",
        "securityInfoSha256", "toolchainManifestSha256",
    ):
        if not str(device.get(key, "")):
            raise RecoveryError(f"identity manifest has no {key}")
    if not SHA256_RE.fullmatch(str(device.get("usbRegistrySha256"))):
        raise RecoveryError("identity manifest USB registry SHA256 is invalid")
    if not SHA256_RE.fullmatch(str(device.get("securityInfoSha256"))):
        raise RecoveryError("identity manifest security-info SHA256 is invalid")
    if not SHA256_RE.fullmatch(str(device.get("toolchainManifestSha256"))):
        raise RecoveryError("identity manifest toolchain SHA256 is invalid")
    if device.get("toolchainManifestSha256") != sha256_file(DEFAULT_TOOLCHAIN_MANIFEST):
        raise RecoveryError("identity manifest was created with a different toolchain")
    if Path(str(device.get("usbById"))).parent != Path("/dev/serial/by-id"):
        raise RecoveryError("identity manifest USB path is not canonical by-id")
    return profile, mac, role


def validate_identity_manifest(value: Mapping[str, Any]) -> tuple[str, str, str]:
    if value.get("schema") != DEVICE_IDENTITY_SCHEMA:
        raise RecoveryError("invalid device identity manifest")
    device = value.get("device")
    if not isinstance(device, dict):
        raise RecoveryError("identity manifest has no device object")
    return validate_device_identity(device)


def identity_matches(left: Mapping[str, Any], right: Mapping[str, Any]) -> bool:
    keys = (
        "profile", "chip", "mac", "flashSize", "usbRole", "usbVid", "usbPid",
        "usbSerial", "usbById", "usbRegistrySha256", "securityInfoSha256",
        "toolchainManifestSha256",
    )
    return all(left.get(key) == right.get(key) for key in keys)


def read_flash(
    profile: str,
    port: Path,
    offset: int,
    size: int,
    target: Path,
    log_directory: Path,
    phase: str,
) -> None:
    create_private_empty(target)
    try:
        run_esptool(
            profile,
            port,
            ["read_flash", hex(offset), hex(size), str(target)],
            log_directory,
            phase,
        )
    finally:
        if target.exists():
            target.chmod(0o600)
    if target.stat().st_size != size:
        raise RecoveryError(
            f"short flash read: expected {size}, got {target.stat().st_size}"
        )


def file_record(path: Path) -> dict[str, Any]:
    require_private_file(path)
    return {"file": path.name, "size": path.stat().st_size, "sha256": sha256_file(path)}


def resolve_recorded_file(directory: Path, record: Mapping[str, Any], label: str) -> Path:
    name = str(record.get("file", ""))
    if not name or Path(name).name != name:
        raise RecoveryError(f"{label} file name is not a safe basename")
    candidate = directory / name
    reject_final_symlink(candidate)
    try:
        candidate = candidate.resolve(strict=True)
    except FileNotFoundError as error:
        raise RecoveryError(f"{label} file is missing") from error
    if candidate.parent != directory:
        raise RecoveryError(f"{label} path escapes its private directory")
    require_private_file(candidate)
    if candidate.stat().st_size != record.get("size"):
        raise RecoveryError(f"{label} size changed")
    if not SHA256_RE.fullmatch(str(record.get("sha256", ""))):
        raise RecoveryError(f"{label} SHA256 is invalid")
    if sha256_file(candidate) != record.get("sha256"):
        raise RecoveryError(f"{label} SHA256 changed")
    return candidate


def files_equal(left: Path, right: Path) -> bool:
    if left.stat().st_size != right.stat().st_size:
        return False
    with left.open("rb") as first, right.open("rb") as second:
        while True:
            left_chunk = first.read(1024 * 1024)
            right_chunk = second.read(1024 * 1024)
            if left_chunk != right_chunk:
                return False
            if not left_chunk:
                return True


def finalize_verified_manifest(core: Mapping[str, Any]) -> dict[str, Any]:
    manifest = dict(core)
    manifest["manifestSha256"] = sha256_bytes(canonical_json(core))
    return manifest


def validate_manifest_checksum(manifest: Mapping[str, Any]) -> None:
    core = {key: value for key, value in manifest.items() if key != "manifestSha256"}
    if manifest.get("manifestSha256") != sha256_bytes(canonical_json(core)):
        raise RecoveryError("verified manifest content changed")


def validate_verified_backup(
    manifest_path: Path,
) -> tuple[dict[str, Any], Path, dict[str, Any]]:
    reject_final_symlink(manifest_path)
    manifest_path = manifest_path.resolve(strict=True)
    require_outside_repo(manifest_path)
    require_private_dir(manifest_path.parent)
    require_private_file(manifest_path)
    manifest = load_json(manifest_path, VERIFIED_BACKUP_SCHEMA)
    validate_manifest_checksum(manifest)
    if manifest.get("benchDirectory") != str(manifest_path.parent):
        raise RecoveryError("verified manifest is not in its recorded bench directory")
    if manifest.get("twoIndependentReadsMatch") is not True:
        raise RecoveryError("backup does not contain two matching independent reads")
    if manifest.get("offsiteCopyVerified") is not True:
        raise RecoveryError("backup has no verified offsite copy")
    profile = canonical_profile(str(manifest.get("profile", "")))
    device = manifest.get("device")
    if not isinstance(device, dict):
        raise RecoveryError("verified manifest has no device identity")
    device_profile, _, _ = validate_device_identity(device)
    if device_profile != profile:
        raise RecoveryError("verified manifest device/profile mismatch")

    required_records = {
        "backupState": BACKUP_STATE_SCHEMA,
        "identityManifest": DEVICE_IDENTITY_SCHEMA,
        "image": None,
        "verificationRead": None,
        "offsiteAttestation": OFFSITE_ATTESTATION_SCHEMA,
    }
    local_files: dict[str, Path] = {}
    for key, schema in required_records.items():
        record = manifest.get(key)
        if not isinstance(record, dict):
            raise RecoveryError(f"verified manifest has no {key} record")
        path = resolve_recorded_file(manifest_path.parent, record, key)
        if schema is not None:
            load_json(path, schema)
        local_files[key] = path

    image = local_files["image"]
    second = local_files["verificationRead"]
    spec = PROFILE_SPECS[profile]
    if image.stat().st_size != spec.flash_size or second.stat().st_size != spec.flash_size:
        raise RecoveryError("full-read size does not match the profile Flash size")
    if sha256_file(image) != sha256_file(second) or not files_equal(image, second):
        raise RecoveryError("two independent full-flash reads no longer match")

    state = load_json(local_files["backupState"], BACKUP_STATE_SCHEMA)
    identity_manifest = load_json(local_files["identityManifest"], DEVICE_IDENTITY_SCHEMA)
    validate_identity_manifest(identity_manifest)
    state_device = state.get("device")
    identity_device = identity_manifest.get("device")
    if not isinstance(state_device, dict) or not isinstance(identity_device, dict):
        raise RecoveryError("backup lineage has no device identity")
    if not identity_matches(device, state_device) or not identity_matches(device, identity_device):
        raise RecoveryError("backup lineage belongs to a different device")
    state_image = state.get("imageA")
    if not isinstance(state_image, dict) or state_image != manifest.get("image"):
        raise RecoveryError("backup-state image lineage mismatch")
    if state.get("identityManifestSha256") != sha256_file(
        local_files["identityManifest"]
    ):
        raise RecoveryError("backup-state identity-manifest lineage mismatch")
    if state.get("profile") != profile:
        raise RecoveryError("backup-state profile mismatch")

    inspection = inspect_backup_image(profile, image)
    if canonical_json(state.get("inspectionA", {})) != canonical_json(inspection):
        raise RecoveryError("backup-state inspection differs from fresh byte inspection")
    if canonical_json(inspection) != canonical_json(manifest.get("inspection", {})):
        raise RecoveryError("manifest inspection differs from fresh byte inspection")
    ota = inspection.get("otadata")
    if (
        not isinstance(ota, dict)
        or ota.get("activeState") != "VALID"
        or ota.get("selectionConsistent") is not True
        or ota.get("issues")
    ):
        raise RecoveryError("actual backup otadata is not clean terminal VALID")
    for app in inspection.get("apps", []):
        if app.get("status") not in {"VALID", "EMPTY"}:
            raise RecoveryError(f"application slot {app.get('label')} is corrupt/partial")

    storage = manifest.get("storage")
    offsite_copy = manifest.get("offsiteCopy")
    if not isinstance(storage, dict) or not isinstance(offsite_copy, dict):
        raise RecoveryError("verified manifest has no storage/offsite evidence")
    recorded_bench = storage.get("bench")
    recorded_offsite = storage.get("offsite")
    if not isinstance(recorded_bench, dict) or not isinstance(recorded_offsite, dict):
        raise RecoveryError("storage identity evidence is incomplete")
    offsite_directory = Path(str(offsite_copy.get("directory", "")))
    reject_final_symlink(offsite_directory)
    offsite_directory = offsite_directory.resolve(strict=True)
    require_outside_repo(offsite_directory)
    require_private_dir(offsite_directory)
    offsite_manifest = offsite_directory / "verified-backup.json"
    reject_symlink_components(offsite_manifest)
    require_private_file(offsite_manifest)
    if not files_equal(manifest_path, offsite_manifest):
        raise RecoveryError("offsite verified manifest differs from bench")
    actual_bench = storage_identity(manifest_path.parent)
    actual_offsite = storage_identity(offsite_directory)
    if not storage_identities_match(recorded_bench, actual_bench):
        raise RecoveryError("bench storage identity changed")
    if not storage_identities_match(recorded_offsite, actual_offsite):
        raise RecoveryError("offsite storage identity changed")
    validate_offsite_attestation(
        local_files["offsiteAttestation"], actual_bench, actual_offsite
    )
    if (
        actual_bench.get("failureDomain") == actual_offsite.get("failureDomain")
        or actual_bench.get("durable") is not True
        or actual_offsite.get("durable") is not True
    ):
        raise RecoveryError("offsite is not an independent durable failure domain")

    offsite_files = offsite_copy.get("files")
    if not isinstance(offsite_files, dict):
        raise RecoveryError("offsite file inventory is missing")
    for key in required_records:
        local_record = manifest[key]
        remote_record = offsite_files.get(key)
        if not isinstance(remote_record, dict) or remote_record != local_record:
            raise RecoveryError(f"offsite {key} record differs from bench")
        resolve_recorded_file(offsite_directory, remote_record, f"offsite {key}")
    return manifest, image, inspection


def read_table_twice(profile: str, port: Path, directory: Path, prefix: str) -> bytes:
    first = directory / f"{prefix}-partition-table-a.bin"
    second = directory / f"{prefix}-partition-table-b.bin"
    read_flash(
        profile, port, PARTITION_TABLE_OFFSET, PARTITION_TABLE_READ_SIZE,
        first, directory, f"{prefix}-partition-a",
    )
    read_flash(
        profile, port, PARTITION_TABLE_OFFSET, PARTITION_TABLE_READ_SIZE,
        second, directory, f"{prefix}-partition-b",
    )
    first_data, second_data = first.read_bytes(), second.read_bytes()
    if first_data != second_data:
        raise RecoveryError("two live partition-table reads disagree")
    return first_data


def table_region_from_image(image: Path) -> bytes:
    with image.open("rb") as stream:
        stream.seek(PARTITION_TABLE_OFFSET)
        value = stream.read(PARTITION_TABLE_READ_SIZE)
    if len(value) != PARTITION_TABLE_READ_SIZE:
        raise RecoveryError("backup has no complete partition-table region")
    return value


def restore_plan_core(
    manifest_path: Path,
    image: Path,
    identity: Mapping[str, Any],
    table_data: bytes,
) -> dict[str, Any]:
    profile = canonical_profile(str(identity["profile"]))
    created = datetime.now(timezone.utc)
    return {
        "schema": RESTORE_PLAN_SCHEMA,
        "createdAt": created.isoformat().replace("+00:00", "Z"),
        "expiresAt": (created + RESTORE_PLAN_MAX_AGE).isoformat().replace("+00:00", "Z"),
        "profile": profile,
        "device": dict(identity),
        "source": {
            "verifiedManifest": str(manifest_path.resolve()),
            "image": str(image.resolve()),
            "size": image.stat().st_size,
            "sha256": sha256_file(image),
        },
        "livePartitionTable": {
            "offset": PARTITION_TABLE_OFFSET,
            "size": PARTITION_TABLE_READ_SIZE,
            "sha256": sha256_bytes(table_data),
            "twoReadsMatch": True,
            "matchesBackup": table_data == table_region_from_image(image),
        },
        "write": {
            "offset": 0,
            "size": image.stat().st_size,
            "automaticEraseFlash": False,
            "automaticReset": False,
            "automaticRetry": False,
            "automaticWriteRetry": False,
            "romSyncRetriesMayOccurBeforeWrite": True,
            "writeAuthorized": False,
        },
    }


def authorization_token(core: Mapping[str, Any]) -> str:
    return "WRITE-FULL-FLASH-" + sha256_bytes(canonical_json(core))[:24].upper()


def finalize_restore_plan(core: Mapping[str, Any]) -> dict[str, Any]:
    plan = dict(core)
    plan["planSha256"] = sha256_bytes(canonical_json(core))
    plan["authorizationToken"] = authorization_token(core)
    return plan


def validate_restore_plan(plan: Mapping[str, Any]) -> None:
    if plan.get("schema") != RESTORE_PLAN_SCHEMA:
        raise RecoveryError("invalid restore plan schema")
    core = {
        key: value
        for key, value in plan.items()
        if key not in {"planSha256", "authorizationToken"}
    }
    if plan.get("planSha256") != sha256_bytes(canonical_json(core)):
        raise RecoveryError("restore plan content changed")
    if plan.get("authorizationToken") != authorization_token(core):
        raise RecoveryError("restore plan token binding is invalid")
    created = parse_utc(plan.get("createdAt"), "restore plan createdAt")
    expires = parse_utc(plan.get("expiresAt"), "restore plan expiresAt")
    now = datetime.now(timezone.utc)
    if expires <= created or expires - created > RESTORE_PLAN_MAX_AGE:
        raise RecoveryError("restore plan validity window is invalid")
    if now < created - timedelta(minutes=5) or now >= expires:
        raise RecoveryError("restore plan is not currently valid")
    write = plan.get("write")
    table = plan.get("livePartitionTable")
    source = plan.get("source")
    device = plan.get("device")
    if not all(isinstance(item, dict) for item in (write, table, source, device)):
        raise RecoveryError("restore plan is incomplete")
    profile = canonical_profile(str(plan.get("profile", "")))
    spec = PROFILE_SPECS[profile]
    device_profile, _, _ = validate_device_identity(device)
    if (
        device_profile != profile
        or device.get("chip") != spec.chip
        or device.get("flashSize") != spec.flash_size
        or device.get("usbRole") not in spec.allowed_roles
    ):
        raise RecoveryError("restore plan device/profile identity is inconsistent")
    if (
        source.get("size") != spec.flash_size
        or not SHA256_RE.fullmatch(str(source.get("sha256", "")))
    ):
        raise RecoveryError("restore plan source size/SHA is invalid")
    if (
        write.get("offset") != 0
        or write.get("size") != spec.flash_size
        or write.get("automaticEraseFlash") is not False
        or write.get("automaticReset") is not False
        or write.get("automaticRetry") is not False
        or write.get("automaticWriteRetry") is not False
        or write.get("romSyncRetriesMayOccurBeforeWrite") is not True
    ):
        raise RecoveryError("restore plan write scope/safety flags are invalid")
    if (
        table.get("offset") != PARTITION_TABLE_OFFSET
        or table.get("size") != PARTITION_TABLE_READ_SIZE
        or not SHA256_RE.fullmatch(str(table.get("sha256", "")))
    ):
        raise RecoveryError("restore plan partition-table evidence is invalid")
    if write.get("writeAuthorized") is not False:
        raise RecoveryError("restore plan must remain writeAuthorized=false")
    if table.get("twoReadsMatch") is not True or table.get("matchesBackup") is not True:
        raise RecoveryError("restore plan has no matching live partition table")


def validate_restore_approval(path: Path, plan: Mapping[str, Any]) -> dict[str, Any]:
    require_outside_repo(path)
    require_private_file(path)
    approval = load_json(path, RESTORE_APPROVAL_SCHEMA)
    if approval.get("decision") != "APPROVED":
        raise RecoveryError("restore approval decision is not APPROVED")
    if approval.get("separateOperatorApproval") is not True:
        raise RecoveryError("restore approval is not explicitly separate")
    if not str(approval.get("operator", "")).strip() or not str(approval.get("approvalId", "")).strip():
        raise RecoveryError("restore approval has no operator/approval identity")
    source = plan.get("source", {})
    device = plan.get("device", {})
    expected = {
        "planSha256": plan.get("planSha256"),
        "profile": plan.get("profile"),
        "mac": device.get("mac"),
        "imageSha256": source.get("sha256"),
    }
    if any(approval.get(key) != value for key, value in expected.items()):
        raise RecoveryError("restore approval is not bound to this plan/device/image")
    approved = parse_utc(approval.get("approvedAt"), "restore approval approvedAt")
    expires = parse_utc(approval.get("expiresAt"), "restore approval expiresAt")
    plan_created = parse_utc(plan.get("createdAt"), "restore plan createdAt")
    now = datetime.now(timezone.utc)
    if approved < plan_created or expires <= approved or expires - approved > RESTORE_PLAN_MAX_AGE:
        raise RecoveryError("restore approval validity window is invalid")
    if now < approved - timedelta(minutes=5) or now >= expires:
        raise RecoveryError("restore approval is not currently valid")
    return approval


def require_restore_authorization(
    args: argparse.Namespace, plan: Mapping[str, Any]
) -> dict[str, Any]:
    if not args.confirm_rom_loader:
        raise RecoveryError("restore requires --confirm-rom-loader")
    if not getattr(args, "confirm_port_open_may_reset", False):
        raise RecoveryError("restore requires --confirm-port-open-may-reset")
    if not args.confirm_separate_approval:
        raise RecoveryError("restore requires --confirm-separate-approval")
    if not args.confirm_full_flash_overwrite:
        raise RecoveryError("restore requires --confirm-full-flash-overwrite")
    if args.authorization_token != plan.get("authorizationToken"):
        raise RecoveryError("restore plan checksum mismatch")
    return validate_restore_approval(args.approval_file, plan)


def restore_write_operation(image: Path) -> list[str]:
    """Return the only allowed full restore operation.

    ``write_flash`` necessarily erases sectors that it rewrites, but the tool
    never issues the broad ``erase_flash`` or ``erase_region`` commands.  The
    flash-size header is preserved and the MCU is left in the ROM loader.
    """
    return ["write_flash", "--flash_size", "keep", "0x0", str(image)]


def restore_failure_status(write_started: bool) -> str:
    """Classify an aborted restore without ever retrying it automatically."""
    return "FLASH_STATE_UNKNOWN" if write_started else "STOP"


def validate_ready_document(value: Mapping[str, Any]) -> dict[str, Any]:
    if value.get("ready") is not True:
        raise RecoveryError("postcheck READY is not true")
    boot_id = str(value.get("bootId", ""))
    if not BOOT_ID_RE.fullmatch(boot_id):
        raise RecoveryError("postcheck bootId is invalid")
    ota = value.get("ota")
    if not isinstance(ota, dict):
        raise RecoveryError("postcheck has no OTA object")
    if ota.get("imageState") != "valid" or ota.get("pending") is not False:
        raise RecoveryError("postcheck OTA image is not terminal VALID")
    if ota.get("runningSlot") not in {"app0", "app1"}:
        raise RecoveryError("postcheck running slot is invalid")
    if ota.get("runningSlot") != ota.get("bootSlot"):
        raise RecoveryError("postcheck running/boot slots disagree")
    if ota.get("firmwareUpdateReady") is not True:
        raise RecoveryError("postcheck firmware update gate is not ready")
    return {"bootId": boot_id, "ota": dict(ota)}


def recursive_values(value: Any, key: str) -> list[Any]:
    found: list[Any] = []
    if isinstance(value, dict):
        for current_key, current_value in value.items():
            if current_key == key:
                found.append(current_value)
            found.extend(recursive_values(current_value, key))
    elif isinstance(value, list):
        for item in value:
            found.extend(recursive_values(item, key))
    return found


def configure_runtime_from_args(args: argparse.Namespace) -> None:
    configure_runtime_selection(
        profile=getattr(args, "runtime_profile", None),
        root=getattr(args, "runtime_root", None),
        confirm_port_open_may_reset=bool(
            getattr(args, "confirm_port_open_may_reset", False)
        ),
    )


def require_port_open_confirmation(args: argparse.Namespace, operation: str) -> bool:
    if not getattr(args, "confirm_port_open_may_reset", False):
        print(
            f"RESULT BLOCKED: {operation} requires "
            "--confirm-port-open-may-reset"
        )
        return False
    return True


def command_identify(args: argparse.Namespace) -> int:
    if not args.confirm_rom_loader:
        print("RESULT BLOCKED: identify requires --confirm-rom-loader")
        return 2
    if not require_port_open_confirmation(args, "identify"):
        return 2
    configure_runtime_from_args(args)
    output: Path | None = None
    try:
        profile = canonical_profile(args.profile)
        role = load_usb_role(args.usb_registry, args.port_role, profile)
        output = require_new_private_dir(args.output)
        append_event(output, "IDENTIFY", "START", profile=profile, usbRole=args.port_role)
        _, identity = live_identity(
            profile, role, args.expected_mac, output, "identify"
        )
        manifest = {
            "schema": DEVICE_IDENTITY_SCHEMA,
            "createdAt": now_iso(),
            "device": identity,
            "usbRegistry": {
                "path": str(args.usb_registry.resolve()),
                "sha256": sha256_file(args.usb_registry),
            },
            "toolchainManifest": {
                "path": str(DEFAULT_TOOLCHAIN_MANIFEST.resolve()),
                "sha256": sha256_file(DEFAULT_TOOLCHAIN_MANIFEST),
            },
            "operatorConfirmedRomLoader": True,
            "writesPerformed": False,
        }
        write_private_json(output / "device-identity.json", manifest)
        append_event(output, "IDENTIFY", "PASS", mac=identity["mac"])
    except (OperationInterrupted, KeyboardInterrupt) as error:
        if output is not None:
            append_event(output, "IDENTIFY", "INTERRUPTED", reason=type(error).__name__)
        print("RESULT BLOCKED: IDENTIFY interrupted")
        return 130
    except (RecoveryError, OSError, subprocess.SubprocessError) as error:
        if output is not None:
            append_event(output, "IDENTIFY", "STOP", reason=str(error))
        print(f"RESULT BLOCKED: {error}")
        return 1
    print(f"PASS {profile} {identity['chip']} flash={identity['flashSize']} MAC={identity['mac']}")
    print("RESULT PASS")
    return 0


def command_backup(args: argparse.Namespace) -> int:
    if not args.confirm_rom_loader:
        print("RESULT BLOCKED: backup requires --confirm-rom-loader")
        return 2
    if not require_port_open_confirmation(args, "backup"):
        return 2
    configure_runtime_from_args(args)
    output: Path | None = None
    try:
        require_outside_repo(args.identity)
        require_private_file(args.identity)
        identity_manifest = load_json(args.identity, DEVICE_IDENTITY_SCHEMA)
        profile, mac, role_name = validate_identity_manifest(identity_manifest)
        role = load_usb_role(args.usb_registry, role_name, profile)
        output = require_new_private_dir(args.output)
        append_event(output, "BACKUP", "START", profile=profile, usbRole=role_name)
        port, live = live_identity(profile, role, mac, output, "backup-live")
        recorded = identity_manifest["device"]
        if not identity_matches(recorded, live):
            raise RecoveryError("live identity differs from IDENTIFY manifest")
        target = output / "full-flash-a.bin"
        read_flash(
            profile, port, 0, PROFILE_SPECS[profile].flash_size,
            target, output, "backup-full-a",
        )
        inspection = inspect_backup_image(profile, target)
        fingerprints = parse_fingerprints(args.fingerprint)
        missing_fingerprints = REQUIRED_LOGICAL_FINGERPRINTS - fingerprints.keys()
        if missing_fingerprints:
            raise RecoveryError(
                "backup requires logical fingerprints: "
                + ", ".join(sorted(missing_fingerprints))
            )
        state = {
            "schema": BACKUP_STATE_SCHEMA,
            "createdAt": now_iso(),
            "profile": profile,
            "device": live,
            "imageA": file_record(target),
            "inspectionA": inspection,
            "logicalFingerprints": fingerprints,
            "secondIndependentReadPresent": False,
            "restoreAuthorized": False,
        }
        identity_copy = output / "device-identity.json"
        write_private_json(identity_copy, identity_manifest)
        state["identityManifestSha256"] = sha256_file(identity_copy)
        write_private_json(output / "backup-state.json", state)
        append_event(output, "BACKUP", "PASS", sha256=state["imageA"]["sha256"])
    except (OperationInterrupted, KeyboardInterrupt) as error:
        if output is not None:
            append_event(output, "BACKUP", "INTERRUPTED", reason=type(error).__name__)
        print("RESULT BLOCKED: BACKUP interrupted")
        return 130
    except (RecoveryError, OSError, subprocess.SubprocessError) as error:
        if output is not None:
            append_event(output, "BACKUP", "STOP", reason=str(error))
        print(f"RESULT BLOCKED: {error}")
        return 1
    print(f"PASS first full read {target.stat().st_size} bytes sha256={sha256_file(target)}")
    print("INFO backup is NOT verified and is NOT evidence of a working restore")
    print("RESULT PASS")
    return 0


def copy_verified_offsite(
    sources: Mapping[str, Path],
    destination: Path,
    bench_identity: Mapping[str, Any],
    offsite_identity: Mapping[str, Any],
) -> dict[str, Any]:
    if bench_identity.get("majorMinor") == offsite_identity.get("majorMinor"):
        raise RecoveryError("offsite destination is on the same filesystem as the bench backup")
    if (
        bench_identity.get("durable") is not True
        or offsite_identity.get("durable") is not True
        or bench_identity.get("failureDomain") == offsite_identity.get("failureDomain")
    ):
        raise RecoveryError("offsite destination is not an independent durable failure domain")
    destination = require_new_private_dir(destination)
    copied: dict[str, dict[str, Any]] = {}
    for name, source in sources.items():
        copied[name] = copy_private_file(source, destination / source.name)
    return {
        "directory": str(destination),
        "files": copied,
        "differentFilesystem": True,
        "independentFailureDomain": True,
    }


def command_verify(args: argparse.Namespace) -> int:
    if not args.confirm_rom_loader:
        print("RESULT BLOCKED: verify requires --confirm-rom-loader")
        return 2
    if not require_port_open_confirmation(args, "verify"):
        return 2
    configure_runtime_from_args(args)
    try:
        reject_final_symlink(args.backup_dir)
        directory = args.backup_dir.resolve(strict=True)
        require_outside_repo(directory)
        require_private_dir(directory)
        require_private_file(directory / "backup-state.json")
        state = load_json(directory / "backup-state.json", BACKUP_STATE_SCHEMA)
        profile = canonical_profile(str(state.get("profile", "")))
        device = state.get("device")
        if not isinstance(device, dict):
            raise RecoveryError("backup state has no device identity")
        mac = canonical_mac(str(device.get("mac", "")))
        role_name = str(device.get("usbRole", ""))
        role = load_usb_role(args.usb_registry, role_name, profile)
        append_event(directory, "VERIFY", "START", profile=profile)
        port, live = live_identity(profile, role, mac, directory, "verify-live")
        if not identity_matches(device, live):
            raise RecoveryError("live identity differs from BACKUP identity")
        first = directory / str(state["imageA"]["file"])
        first = first.resolve(strict=True)
        if first.parent != directory:
            raise RecoveryError("first backup image path escapes its private directory")
        require_private_file(first)
        if first.stat().st_size != state["imageA"]["size"] or sha256_file(first) != state["imageA"]["sha256"]:
            raise RecoveryError("first backup read changed")
        second = directory / "full-flash-b.bin"
        read_flash(
            profile, port, 0, PROFILE_SPECS[profile].flash_size,
            second, directory, "verify-full-b",
        )
        if not files_equal(first, second):
            raise RecoveryError("two independent full-flash reads disagree")
        inspection = inspect_backup_image(profile, first)
        ota = inspection.get("otadata", {})
        if (
            ota.get("activeState") != "VALID"
            or ota.get("selectionConsistent") is not True
            or ota.get("issues")
        ):
            raise RecoveryError("backup otadata is not clean terminal VALID")
        bench_storage = storage_identity(directory)
        offsite_parent = args.offsite_output.parent.resolve(strict=True)
        offsite_storage = storage_identity(offsite_parent)
        attestation = validate_offsite_attestation(
            args.offsite_attestation, bench_storage, offsite_storage
        )
        attestation_copy = directory / "offsite-attestation.json"
        copy_private_file(args.offsite_attestation, attestation_copy)
        sources = {
            "backupState": directory / "backup-state.json",
            "identityManifest": directory / "device-identity.json",
            "image": first,
            "verificationRead": second,
            "offsiteAttestation": attestation_copy,
        }
        offsite = copy_verified_offsite(
            sources,
            args.offsite_output,
            bench_storage,
            offsite_storage,
        )
        core = {
            "schema": VERIFIED_BACKUP_SCHEMA,
            "createdAt": now_iso(),
            "profile": profile,
            "device": live,
            "benchDirectory": str(directory),
            "backupState": file_record(sources["backupState"]),
            "identityManifest": file_record(sources["identityManifest"]),
            "image": file_record(first),
            "verificationRead": file_record(second),
            "offsiteAttestation": file_record(attestation_copy),
            "twoIndependentReadsMatch": True,
            "inspection": inspection,
            "logicalFingerprints": state.get("logicalFingerprints", {}),
            "offsiteCopy": offsite,
            "storage": {"bench": bench_storage, "offsite": offsite_storage},
            "offsiteAttestationSummary": {
                "operator": attestation["operator"],
                "approvalId": attestation["approvalId"],
                "confirmedAt": attestation["confirmedAt"],
            },
            "offsiteCopyVerified": True,
            "resetLinesUsed": "UNVERIFIED",
            "automaticResetRequested": False,
            "portOpenMayAssertDtrRts": True,
            "operatorAcceptedPortOpenResetRisk": True,
            "flasherStubUsed": False,
            "restoreAuthorized": False,
            "restorePhysicallyQualified": False,
        }
        manifest = finalize_verified_manifest(core)
        verified_path = directory / "verified-backup.json"
        write_private_json(verified_path, manifest)
        write_private_json(
            Path(offsite["directory"]) / "verified-backup.json", manifest
        )
        validate_verified_backup(verified_path)
        append_event(directory, "VERIFY", "PASS", sha256=manifest["image"]["sha256"])
    except (OperationInterrupted, KeyboardInterrupt) as error:
        if "directory" in locals():
            append_event(directory, "VERIFY", "INTERRUPTED", reason=type(error).__name__)
        print("RESULT BLOCKED: VERIFY interrupted")
        return 130
    except (RecoveryError, OSError, subprocess.SubprocessError) as error:
        if "directory" in locals():
            append_event(directory, "VERIFY", "STOP", reason=str(error))
        print(f"RESULT BLOCKED: {error}")
        return 1
    print(f"PASS two full reads match sha256={manifest['image']['sha256']}")
    print("PASS offsite copy verified on a different filesystem")
    print("INFO restore remains PHYSICAL PENDING")
    print("RESULT PASS")
    return 0


def command_restore_preflight(args: argparse.Namespace) -> int:
    if not args.confirm_rom_loader:
        print("RESULT BLOCKED: restore-preflight requires --confirm-rom-loader")
        return 2
    if not require_port_open_confirmation(args, "restore-preflight"):
        return 2
    configure_runtime_from_args(args)
    output: Path | None = None
    try:
        manifest, image, inspection = validate_verified_backup(args.verified_manifest)
        profile = canonical_profile(str(manifest["profile"]))
        device = manifest.get("device")
        if not isinstance(device, dict):
            raise RecoveryError("verified manifest has no device identity")
        ota = inspection.get("otadata")
        if not isinstance(ota, dict) or ota.get("activeState") != "VALID" or ota.get("selectionConsistent") is not True:
            raise RecoveryError("restore source does not have a consistent active VALID image")
        role = load_usb_role(args.usb_registry, str(device["usbRole"]), profile)
        output = require_new_private_dir(args.output)
        append_event(output, "RESTORE-PREFLIGHT", "START", profile=profile)
        port, live = live_identity(
            profile, role, str(device["mac"]), output, "preflight-live"
        )
        if not identity_matches(device, live):
            raise RecoveryError("live identity differs from verified backup")
        table = read_table_twice(profile, port, output, "preflight")
        if table != table_region_from_image(image):
            raise RecoveryError("live partition-table region differs from backup")
        core = restore_plan_core(args.verified_manifest, image, live, table)
        plan = finalize_restore_plan(core)
        write_private_json(output / "restore-plan.json", plan)
        append_event(output, "RESTORE-PREFLIGHT", "PASS", planSha256=plan["planSha256"])
    except (OperationInterrupted, KeyboardInterrupt) as error:
        if output is not None:
            append_event(
                output, "RESTORE-PREFLIGHT", "INTERRUPTED", reason=type(error).__name__
            )
        print("RESULT BLOCKED: RESTORE-PREFLIGHT interrupted")
        return 130
    except (RecoveryError, OSError, subprocess.SubprocessError) as error:
        if output is not None:
            append_event(output, "RESTORE-PREFLIGHT", "STOP", reason=str(error))
        print(f"RESULT BLOCKED: {error}")
        return 1
    print(f"PASS restore plan prepared for {profile} MAC={live['mac']}")
    print(f"AUTHORIZATION TOKEN {plan['authorizationToken']}")
    print("INFO writeAuthorized=false; restore needs separate explicit approval")
    print("RESULT PASS")
    return 0


def command_restore(args: argparse.Namespace) -> int:
    configure_runtime_from_args(args)
    reject_final_symlink(args.plan)
    plan_path = args.plan.resolve(strict=True)
    require_outside_repo(plan_path)
    directory = plan_path.parent
    write_started = False
    try:
        require_private_dir(directory)
        require_private_file(plan_path)
        plan = load_json(plan_path, RESTORE_PLAN_SCHEMA)
        validate_restore_plan(plan)
        approval = require_restore_authorization(args, plan)
        profile = canonical_profile(str(plan["profile"]))
        device = plan.get("device")
        source = plan.get("source")
        if not isinstance(device, dict) or not isinstance(source, dict):
            raise RecoveryError("restore plan is incomplete")
        image = Path(str(source["image"])).resolve(strict=True)
        require_private_file(image)
        if image.stat().st_size != source.get("size") or sha256_file(image) != source.get("sha256"):
            raise RecoveryError("restore source changed after preflight")
        manifest, validated_image, _ = validate_verified_backup(
            Path(str(source["verifiedManifest"]))
        )
        if validated_image != image:
            raise RecoveryError("restore source path mismatch")
        manifest_device = manifest.get("device")
        if manifest.get("profile") != profile or not isinstance(manifest_device, dict):
            raise RecoveryError("verified backup profile/device differs from restore plan")
        if not identity_matches(device, manifest_device):
            raise RecoveryError("verified backup identity differs from restore plan")
        role = load_usb_role(args.usb_registry, str(device["usbRole"]), profile)
        append_event(directory, "RESTORE", "START", profile=profile, mac=device["mac"])
        port, live = live_identity(
            profile, role, str(device["mac"]), directory, "restore-identity"
        )
        if not identity_matches(device, live):
            raise RecoveryError("live identity changed after restore preflight")
        table = read_table_twice(profile, port, directory, "restore-live")
        if sha256_bytes(table) != plan["livePartitionTable"]["sha256"] or table != table_region_from_image(image):
            raise RecoveryError("live partition table changed after restore preflight")
        write_operation = restore_write_operation(image)
        append_event(
            directory,
            "RESTORE",
            "WRITE_START",
            command=write_operation,
            automaticEraseFlash=False,
            automaticReset=False,
        )
        write_started = True
        run_esptool(
            profile, port, write_operation, directory, "restore-write"
        )
        run_esptool(
            profile,
            port,
            ["verify_flash", "0x0", str(image)],
            directory,
            "restore-verify-flash",
        )
        readback = directory / "restore-readback.bin"
        read_flash(
            profile, port, 0, PROFILE_SPECS[profile].flash_size,
            readback, directory, "restore-full-readback",
        )
        source_sha = sha256_file(image)
        readback_sha = sha256_file(readback)
        if source_sha != readback_sha or image.read_bytes() != readback.read_bytes():
            raise RecoveryError("full restore readback differs from source")
        inspection = inspect_backup_image(profile, readback)
        result = {
            "schema": RESTORE_RESULT_SCHEMA,
            "createdAt": now_iso(),
            "profile": profile,
            "device": live,
            "planSha256": plan["planSha256"],
            "sourceSha256": source_sha,
            "readback": {
                "file": readback.name,
                "size": readback.stat().st_size,
                "sha256": readback_sha,
                "matchesSource": True,
            },
            "inspection": inspection,
            "verifyFlashPassed": True,
            "automaticEraseFlash": False,
            "automaticReset": False,
            "automaticRetry": False,
            "automaticWriteRetry": False,
            "romSyncRetriesMayOccurBeforeWrite": True,
            "portOpenMayAssertDtrRts": True,
            "operatorAcceptedPortOpenResetRisk": True,
            "deviceLeftInRomLoader": True,
            "applicationBooted": False,
            "approval": {
                "operator": approval["operator"],
                "approvalId": approval["approvalId"],
                "approvalFileSha256": sha256_file(args.approval_file),
            },
        }
        write_private_json(directory / "restore-result.json", result)
        append_event(directory, "RESTORE", "PASS", sha256=source_sha)
    except (OperationInterrupted, KeyboardInterrupt) as error:
        status = (
            restore_failure_status(locals().get("write_started", False))
            if locals().get("write_started", False)
            else "INTERRUPTED"
        )
        append_event(directory, "RESTORE", status, reason=type(error).__name__)
        print(f"RESULT BLOCKED: {status}; no retry is allowed")
        return 130
    except (RecoveryError, OSError, subprocess.SubprocessError) as error:
        status = restore_failure_status(locals().get("write_started", False))
        append_event(directory, "RESTORE", status, reason=str(error))
        print(f"RESULT BLOCKED: {status}: {error}")
        return 1
    print(f"PASS full restore verify/readback sha256={source_sha}")
    print("INFO device remains in ROM loader; application boot needs separate manual action")
    print("RESULT PASS")
    return 0


def command_postcheck(args: argparse.Namespace) -> int:
    if not args.confirm_manual_app_boot:
        print("RESULT BLOCKED: postcheck requires --confirm-manual-app-boot")
        return 2
    try:
        restore_result_path = args.restore_result.resolve(strict=True)
        require_private_dir(restore_result_path.parent)
        require_private_file(restore_result_path)
        result = load_json(restore_result_path, RESTORE_RESULT_SCHEMA)
        profile = canonical_profile(str(result.get("profile", "")))
        readback_info = result.get("readback")
        if not isinstance(readback_info, dict) or readback_info.get("matchesSource") is not True:
            raise RecoveryError("restore result has no matching full readback")
        readback = args.restore_result.parent / str(readback_info.get("file", ""))
        readback = readback.resolve(strict=True)
        if readback.parent != restore_result_path.parent:
            raise RecoveryError("restore readback path escapes its private directory")
        require_private_file(readback)
        if readback.stat().st_size != readback_info.get("size") or sha256_file(readback) != readback_info.get("sha256"):
            raise RecoveryError("restore readback changed before POSTCHECK")
        inspection = inspect_backup_image(profile, readback)
        ready = validate_ready_document(load_json(args.ready_json))
        system = load_json(args.system_json)
        profiles = {str(value).upper() for value in recursive_values(system, "profile")}
        if profile not in profiles:
            raise RecoveryError(f"system snapshot does not identify profile {profile}")
        expected_slot = inspection.get("selectedBootSlot")
        if ready["ota"].get("runningSlot") != expected_slot:
            raise RecoveryError(
                f"running slot differs from restored otadata: expected {expected_slot}"
            )
        if bool(ready["ota"].get("rollbackAvailable")) != bool(inspection.get("rollbackAvailable")):
            raise RecoveryError("runtime rollback availability differs from restored flash")
        verified_manifest_path = Path(str(args.verified_manifest)).resolve(strict=True)
        manifest, source, verified_inspection = validate_verified_backup(verified_manifest_path)
        if manifest["profile"] != profile or sha256_file(source) != result.get("sourceSha256"):
            raise RecoveryError("POSTCHECK backup/restore lineage mismatch")
        actual_fingerprints = parse_fingerprints(args.fingerprint)
        expected_fingerprints = manifest.get("logicalFingerprints", {})
        if actual_fingerprints != expected_fingerprints:
            raise RecoveryError("post-boot logical fingerprints differ from pre-backup values")
        output = {
            "schema": POSTCHECK_SCHEMA,
            "createdAt": now_iso(),
            "profile": profile,
            "device": result["device"],
            "sourceSha256": result["sourceSha256"],
            "readbackSha256": readback_info["sha256"],
            "bootId": ready["bootId"],
            "ota": ready["ota"],
            "logicalFingerprints": actual_fingerprints,
            "configPartitionsMatchRawBackup": (
                inspection["partitionHashes"]["nvs"] == verified_inspection["partitionHashes"]["nvs"]
                and inspection["partitionHashes"]["spiffs"] == verified_inspection["partitionHashes"]["spiffs"]
            ),
            "physicalRestoreQualified": True,
        }
        if output["configPartitionsMatchRawBackup"] is not True:
            raise RecoveryError("restored NVS/SPIFFS hashes differ from verified backup")
        require_outside_repo(args.output)
        output_parent = args.output.parent.resolve(strict=True)
        require_private_dir(output_parent)
        write_private_json(args.output, output)
    except (OperationInterrupted, KeyboardInterrupt):
        print("RESULT BLOCKED: POSTCHECK interrupted")
        return 130
    except (RecoveryError, OSError, subprocess.SubprocessError) as error:
        print(f"RESULT BLOCKED: {error}")
        return 1
    print(f"PASS {profile} restore POSTCHECK bootId={output['bootId']}")
    print("RESULT PASS")
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    subparsers = result.add_subparsers(dest="command", required=True)

    def add_runtime_arguments(command: argparse.ArgumentParser) -> None:
        command.add_argument(
            "--runtime-profile",
            help="hash-pinned profile from ROM_FLASH_TOOLCHAIN.json; defaults by host architecture",
        )
        command.add_argument(
            "--runtime-root",
            type=Path,
            help="explicit root containing the profile's relative runtime paths",
        )
        command.add_argument(
            "--confirm-port-open-may-reset",
            action="store_true",
            help="accept that pyserial open may assert DTR/RTS even with no_reset",
        )

    identify = subparsers.add_parser("identify", help="identify a MCU already in ROM loader")
    add_runtime_arguments(identify)
    identify.add_argument("--profile", required=True, choices=("S0", "L0"))
    identify.add_argument("--port-role", required=True)
    identify.add_argument("--expected-mac", required=True)
    identify.add_argument("--usb-registry", type=Path, default=DEFAULT_USB_REGISTRY)
    identify.add_argument("--output", type=Path, required=True)
    identify.add_argument("--confirm-rom-loader", action="store_true")
    identify.set_defaults(function=command_identify)

    backup = subparsers.add_parser("backup", help="perform the first full read")
    add_runtime_arguments(backup)
    backup.add_argument("--identity", type=Path, required=True)
    backup.add_argument("--usb-registry", type=Path, default=DEFAULT_USB_REGISTRY)
    backup.add_argument("--output", type=Path, required=True)
    backup.add_argument("--fingerprint", action="append", default=[])
    backup.add_argument("--confirm-rom-loader", action="store_true")
    backup.set_defaults(function=command_backup)

    verify = subparsers.add_parser("verify", help="perform and compare the second full read")
    add_runtime_arguments(verify)
    verify.add_argument("--backup-dir", type=Path, required=True)
    verify.add_argument("--usb-registry", type=Path, default=DEFAULT_USB_REGISTRY)
    verify.add_argument("--offsite-output", type=Path, required=True)
    verify.add_argument("--offsite-attestation", type=Path, required=True)
    verify.add_argument("--confirm-rom-loader", action="store_true")
    verify.set_defaults(function=command_verify)

    preflight = subparsers.add_parser(
        "restore-preflight", help="prepare a write-disabled full restore plan"
    )
    add_runtime_arguments(preflight)
    preflight.add_argument("--verified-manifest", type=Path, required=True)
    preflight.add_argument("--usb-registry", type=Path, default=DEFAULT_USB_REGISTRY)
    preflight.add_argument("--output", type=Path, required=True)
    preflight.add_argument("--confirm-rom-loader", action="store_true")
    preflight.set_defaults(function=command_restore_preflight)

    restore = subparsers.add_parser("restore", help="write one exact full image once")
    add_runtime_arguments(restore)
    restore.add_argument("--plan", type=Path, required=True)
    restore.add_argument("--usb-registry", type=Path, default=DEFAULT_USB_REGISTRY)
    restore.add_argument("--authorization-token", required=True)
    restore.add_argument("--approval-file", type=Path, required=True)
    restore.add_argument("--confirm-rom-loader", action="store_true")
    restore.add_argument("--confirm-separate-approval", action="store_true")
    restore.add_argument("--confirm-full-flash-overwrite", action="store_true")
    restore.set_defaults(function=command_restore)

    postcheck = subparsers.add_parser("postcheck", help="verify manual boot and restored state")
    postcheck.add_argument("--restore-result", type=Path, required=True)
    postcheck.add_argument("--verified-manifest", type=Path, required=True)
    postcheck.add_argument("--ready-json", type=Path, required=True)
    postcheck.add_argument("--system-json", type=Path, required=True)
    postcheck.add_argument("--fingerprint", action="append", default=[])
    postcheck.add_argument("--output", type=Path, required=True)
    postcheck.add_argument("--confirm-manual-app-boot", action="store_true")
    postcheck.set_defaults(function=command_postcheck)
    return result


def main() -> int:
    def interrupted(signum: int, _frame: Any) -> None:
        raise OperationInterrupted(f"signal {signum}")

    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGHUP, interrupted)
    args = parser().parse_args()
    return int(args.function(args))


if __name__ == "__main__":
    sys.exit(main())
