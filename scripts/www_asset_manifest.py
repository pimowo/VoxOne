#!/usr/bin/env python3
"""Deterministic VoxOne WWW asset manifest generation and inspection."""

from __future__ import annotations

import argparse
import gzip
import hashlib
from pathlib import Path
import re
import struct
import sys
import zlib


MAGIC = b"VXWM"
SCHEMA = 1
HEADER = struct.Struct("<4s8HI32sII")
ENTRY = struct.Struct("<64sI32sB11s")
HEADER_SIZE = HEADER.size
ENTRY_SIZE = ENTRY.size
COMPRESSION_GZIP = 1
ASSET_SET_PREFIX = b"VXASSET\0"
MANIFEST_NAME = "assets.vxwm"


class ManifestError(RuntimeError):
    pass


def _constant(header: str, name: str) -> int:
    match = re.search(
        rf"constexpr\s+uint16_t\s+{re.escape(name)}\s*=\s*(\d+)\s*;",
        header,
    )
    if not match:
        raise ManifestError(f"missing uint16_t contract constant: {name}")
    return int(match.group(1))


def contract_from_header(header_path: Path) -> dict[str, object]:
    text = header_path.read_text(encoding="utf-8")
    paths = re.findall(r'"(/www/[^"\n]+\.gz)"', text)
    if not paths or len(paths) != len(set(paths)):
        raise ManifestError("WWW readiness header has no unique gzip asset list")
    return {
        "schema": _constant(text, "kWwwManifestSchema"),
        "wwwContract": _constant(text, "kCurrentBundleWwwContract"),
        "firmwareContractMin": _constant(
            text, "kCurrentBundleFirmwareContractMin"
        ),
        "firmwareContractMax": _constant(
            text, "kCurrentBundleFirmwareContractMax"
        ),
        "paths": paths,
    }


def _asset_set_id(schema: int, www_contract: int, firmware_min: int,
                  firmware_max: int, records: bytes, count: int) -> bytes:
    canonical = struct.pack(
        "<6H", schema, www_contract, firmware_min, firmware_max,
        count, ENTRY_SIZE,
    )
    return hashlib.sha256(ASSET_SET_PREFIX + canonical + records).digest()


def build_manifest(asset_dir: Path, contract: dict[str, object]) -> tuple[bytes, dict[str, object]]:
    schema = int(contract["schema"])
    if schema != SCHEMA:
        raise ManifestError(f"unsupported generated manifest schema: {schema}")
    www_contract = int(contract["wwwContract"])
    firmware_min = int(contract["firmwareContractMin"])
    firmware_max = int(contract["firmwareContractMax"])
    if not (0 <= www_contract <= 0xFFFF and
            0 <= firmware_min <= firmware_max <= 0xFFFF):
        raise ManifestError("invalid WWW compatibility contract")
    paths = list(contract["paths"])
    if not paths or len(paths) > 0xFFFF or len(paths) != len(set(paths)):
        raise ManifestError("invalid required WWW asset list")

    records = bytearray()
    assets: list[dict[str, object]] = []
    for path in paths:
        if not isinstance(path, str) or not path.startswith("/www/") or not path.endswith(".gz"):
            raise ManifestError(f"unsafe WWW asset path: {path!r}")
        encoded_path = path.encode("utf-8")
        if len(encoded_path) >= 64:
            raise ManifestError(f"WWW asset path is too long: {path}")
        disk_path = asset_dir / path.removeprefix("/www/")
        try:
            compressed = disk_path.read_bytes()
        except OSError as error:
            raise ManifestError(f"cannot read required WWW asset: {path}") from error
        try:
            decompressed = gzip.decompress(compressed)
        except (EOFError, OSError, zlib.error) as error:
            raise ManifestError(f"invalid gzip asset: {path}") from error
        digest = hashlib.sha256(compressed).digest()
        records.extend(
            ENTRY.pack(
                encoded_path + b"\0" * (64 - len(encoded_path)),
                len(compressed), digest, COMPRESSION_GZIP, b"\0" * 11,
            )
        )
        assets.append(
            {
                "path": path,
                "size": len(compressed),
                "sha256": digest.hex(),
                "compression": "gzip",
                "decompressedSize": len(decompressed),
            }
        )

    asset_set = _asset_set_id(
        schema, www_contract, firmware_min, firmware_max,
        bytes(records), len(paths),
    )
    total_size = HEADER_SIZE + len(records)
    records_crc = zlib.crc32(records) & 0xFFFFFFFF
    header_without_crc = HEADER.pack(
        MAGIC, schema, HEADER_SIZE, www_contract, firmware_min, firmware_max,
        len(paths), ENTRY_SIZE, 0, total_size, asset_set, records_crc, 0,
    )
    header_crc = zlib.crc32(header_without_crc[:-4]) & 0xFFFFFFFF
    header = header_without_crc[:-4] + struct.pack("<I", header_crc)
    manifest = header + records
    return manifest, {
        "schema": schema,
        "wwwContract": www_contract,
        "firmwareContractMin": firmware_min,
        "firmwareContractMax": firmware_max,
        "assetSetId": asset_set.hex(),
        "manifestSize": len(manifest),
        "manifestSha256": hashlib.sha256(manifest).hexdigest(),
        "assets": assets,
        "gzipValidation": "full-decompression-and-trailer-crc",
        "trust": "integrity metadata generated with the assets; not a signature",
    }


def parse_manifest(data: bytes) -> dict[str, object]:
    if len(data) < HEADER_SIZE:
        raise ManifestError("manifest is truncated")
    fields = HEADER.unpack(data[:HEADER_SIZE])
    (magic, schema, header_size, www_contract, firmware_min, firmware_max,
     count, entry_size, flags, total_size, asset_set, records_crc,
     header_crc) = fields
    if magic != MAGIC or schema != SCHEMA or header_size != HEADER_SIZE:
        raise ManifestError("manifest magic, schema or header size is invalid")
    if entry_size != ENTRY_SIZE or flags != 0 or not count:
        raise ManifestError("manifest record contract is invalid")
    if total_size != len(data) or total_size != HEADER_SIZE + count * ENTRY_SIZE:
        raise ManifestError("manifest size is inconsistent")
    if firmware_min > firmware_max:
        raise ManifestError("manifest compatibility range is invalid")
    if zlib.crc32(data[:HEADER_SIZE - 4]) & 0xFFFFFFFF != header_crc:
        raise ManifestError("manifest header CRC mismatch")
    records = data[HEADER_SIZE:]
    if zlib.crc32(records) & 0xFFFFFFFF != records_crc:
        raise ManifestError("manifest records CRC mismatch")
    expected_set = _asset_set_id(
        schema, www_contract, firmware_min, firmware_max, records, count
    )
    if expected_set != asset_set:
        raise ManifestError("manifest asset-set identifier mismatch")

    assets: list[dict[str, object]] = []
    seen: set[str] = set()
    for index in range(count):
        start = index * ENTRY_SIZE
        raw_path, size, digest, compression, reserved = ENTRY.unpack(
            records[start:start + ENTRY_SIZE]
        )
        if b"\0" not in raw_path:
            raise ManifestError("manifest asset path is not terminated")
        try:
            path = raw_path.split(b"\0", 1)[0].decode("utf-8")
        except UnicodeDecodeError as error:
            raise ManifestError("manifest asset path is not UTF-8") from error
        if (not path.startswith("/www/") or not path.endswith(".gz") or
                path in seen or compression != COMPRESSION_GZIP or
                reserved != b"\0" * len(reserved)):
            raise ManifestError("manifest asset record is invalid")
        seen.add(path)
        assets.append(
            {"path": path, "size": size, "sha256": digest.hex(),
             "compression": "gzip"}
        )
    return {
        "schema": schema,
        "wwwContract": www_contract,
        "firmwareContractMin": firmware_min,
        "firmwareContractMax": firmware_max,
        "assetSetId": asset_set.hex(),
        "manifestSize": len(data),
        "manifestSha256": hashlib.sha256(data).hexdigest(),
        "assets": assets,
        "runtimeGzipValidation": "framing-and-compressed-sha256",
        "buildGzipValidation": "full-decompression-and-trailer-crc",
        "trust": "integrity metadata generated with the assets; not a signature",
    }


def verify_assets(asset_dir: Path, parsed: dict[str, object]) -> None:
    for record in parsed["assets"]:
        path = asset_dir / str(record["path"]).removeprefix("/www/")
        try:
            data = path.read_bytes()
        except OSError as error:
            raise ManifestError(f"missing manifest asset: {record['path']}") from error
        if len(data) != record["size"]:
            raise ManifestError(f"asset size mismatch: {record['path']}")
        if hashlib.sha256(data).hexdigest() != record["sha256"]:
            raise ManifestError(f"asset SHA256 mismatch: {record['path']}")
        try:
            gzip.decompress(data)
        except (EOFError, OSError, zlib.error) as error:
            raise ManifestError(f"invalid gzip asset: {record['path']}") from error


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--asset-dir", type=Path)
    parser.add_argument("--verify-assets", action="store_true")
    args = parser.parse_args()
    try:
        parsed = parse_manifest(args.manifest.read_bytes())
        if args.verify_assets:
            if args.asset_dir is None:
                raise ManifestError("--asset-dir is required with --verify-assets")
            verify_assets(args.asset_dir, parsed)
    except (OSError, ManifestError) as error:
        print(f"FAIL: {error}")
        return 1
    print(
        f"PASS schema={parsed['schema']} contract={parsed['wwwContract']} "
        f"assets={len(parsed['assets'])} assetSet={parsed['assetSetId']}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
