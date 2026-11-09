"""Build deterministic gzip assets for the VoxOne preview page."""

Import("env")

import gzip
import hashlib
from io import BytesIO
from pathlib import Path
import re
import shutil

from www_asset_manifest import (
    MANIFEST_NAME,
    build_manifest,
    contract_from_header,
    profile_includes_dsp,
)


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
SOURCE_DIR = PROJECT_DIR / "web-src"
CANONICAL_DATA_DIR = PROJECT_DIR / "data"
CANONICAL_TARGET_DIR = CANONICAL_DATA_DIR / "www"
STAGING_DATA_DIR = Path(env.subst("$BUILD_DIR")) / "spiffs-data"
CONTRACT_HEADER = PROJECT_DIR / "src" / "core" / "www_readiness.h"
DSP_HTML_BLOCK = re.compile(
    r"[ \t]*<!-- VOXONE_DSP_BEGIN -->\s*(.*?)\s*"
    r"<!-- VOXONE_DSP_END -->[ \t]*",
    re.DOTALL,
)


def compress(data):
    buffer = BytesIO()
    with gzip.GzipFile(fileobj=buffer, mode="wb", filename="", mtime=0, compresslevel=9) as stream:
        stream.write(data)
    return buffer.getvalue()


def source_bytes(name):
    return (SOURCE_DIR / name).read_bytes()


def selected_profile():
    parsed = env.ParseFlags(env.get("BUILD_FLAGS", []))
    matches = []
    for define in parsed.get("CPPDEFINES", []):
        name = define[0] if isinstance(define, (list, tuple)) else define
        if str(name).startswith("VOXONE_PROFILE_"):
            matches.append(str(name).removeprefix("VOXONE_PROFILE_"))
    if len(matches) != 1:
        raise RuntimeError(
            "build must select exactly one VOXONE_PROFILE_* for WWW packaging"
        )
    return matches[0]


def render_html(include_dsp):
    html = source_bytes("voxone.html").decode("utf-8")
    return DSP_HTML_BLOCK.sub(
        lambda match: match.group(1) if include_dsp else "", html
    )


def build_assets(target_dir, include_dsp):
    target_dir.mkdir(parents=True, exist_ok=True)
    contract = contract_from_header(CONTRACT_HEADER, include_dsp=include_dsp)
    assets = tuple(path.removeprefix("/www/").removesuffix(".gz")
                   for path in contract["paths"])
    hashes = {
        name: hashlib.sha256(source_bytes(name)).hexdigest()[:12]
        for name in assets if name != "voxone.html"
    }
    html = render_html(include_dsp)
    for name, digest in hashes.items():
        html = html.replace("{" + name + "-hash}", digest)

    for name in assets:
        data = html.encode("utf-8") if name == "voxone.html" else source_bytes(name)
        target = target_dir / (name + ".gz")
        compressed = compress(data)
        if not target.exists() or target.read_bytes() != compressed:
            target.write_bytes(compressed)
        print(f"WEB-1A asset: {name} {len(data)} B -> {len(compressed)} B")

    all_contract = contract_from_header(CONTRACT_HEADER, include_dsp=True)
    excluded = set(all_contract["paths"]) - set(contract["paths"])
    for path in excluded:
        stale = target_dir / path.removeprefix("/www/")
        if stale.exists():
            stale.unlink()

    manifest, report = build_manifest(target_dir, contract)
    manifest_path = target_dir / MANIFEST_NAME
    if not manifest_path.exists() or manifest_path.read_bytes() != manifest:
        manifest_path.write_bytes(manifest)
    print(
        f"WWW manifest: {len(manifest)} B assetSet={report['assetSetId']} "
        f"contract={report['wwwContract']}"
    )


profile = selected_profile()
include_dsp = profile_includes_dsp(PROJECT_DIR, profile)

# Keep the checked-in canonical bundle deterministic for offline tests and
# recovery tooling, then stage the profile-specific filesystem without ever
# deleting canonical assets from the working tree.
build_assets(CANONICAL_TARGET_DIR, include_dsp=True)
if STAGING_DATA_DIR.exists():
    shutil.rmtree(STAGING_DATA_DIR)
shutil.copytree(CANONICAL_DATA_DIR, STAGING_DATA_DIR)
build_assets(STAGING_DATA_DIR / "www", include_dsp=include_dsp)
env.Replace(PROJECT_DATA_DIR=str(STAGING_DATA_DIR))
print(
    f"WWW profile bundle: profile={profile} dsp={int(include_dsp)} "
    f"data={STAGING_DATA_DIR}"
)
