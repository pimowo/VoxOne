"""Build deterministic gzip assets for the VoxOne preview page."""

Import("env")

import gzip
import hashlib
from io import BytesIO
from pathlib import Path

from www_asset_manifest import build_manifest, contract_from_header, MANIFEST_NAME


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
SOURCE_DIR = PROJECT_DIR / "web-src"
TARGET_DIR = PROJECT_DIR / "data" / "www"
CONTRACT_HEADER = PROJECT_DIR / "src" / "core" / "www_readiness.h"


def compress(data):
    buffer = BytesIO()
    with gzip.GzipFile(fileobj=buffer, mode="wb", filename="", mtime=0, compresslevel=9) as stream:
        stream.write(data)
    return buffer.getvalue()


def source_bytes(name):
    return (SOURCE_DIR / name).read_bytes()


def build_assets():
    contract = contract_from_header(CONTRACT_HEADER)
    assets = tuple(path.removeprefix("/www/").removesuffix(".gz")
                   for path in contract["paths"])
    hashes = {
        name: hashlib.sha256(source_bytes(name)).hexdigest()[:12]
        for name in assets if name != "voxone.html"
    }
    html = source_bytes("voxone.html").decode("utf-8")
    for name, digest in hashes.items():
        html = html.replace("{" + name + "-hash}", digest)

    for name in assets:
        data = html.encode("utf-8") if name == "voxone.html" else source_bytes(name)
        target = TARGET_DIR / (name + ".gz")
        compressed = compress(data)
        if not target.exists() or target.read_bytes() != compressed:
            target.write_bytes(compressed)
        print(f"WEB-1A asset: {name} {len(data)} B -> {len(compressed)} B")

    manifest, report = build_manifest(TARGET_DIR, contract)
    manifest_path = TARGET_DIR / MANIFEST_NAME
    if not manifest_path.exists() or manifest_path.read_bytes() != manifest:
        manifest_path.write_bytes(manifest)
    print(
        f"WWW manifest: {len(manifest)} B assetSet={report['assetSetId']} "
        f"contract={report['wwwContract']}"
    )


build_assets()
