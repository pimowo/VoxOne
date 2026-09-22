from pathlib import Path
import hashlib
import shutil

Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
PIOENV = env.subst("$PIOENV")
SOURCE = PROJECT_DIR / "assets" / "yoradio_glcdfont.c"
EXPECTED_SHA256 = "37dfc0a4f1bc054a235ab960e309e5455cab032ef747293481de91c9de354f24".lower()


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


if not SOURCE.is_file():
    raise RuntimeError(f"Missing yoRadio font source: {SOURCE}")
if sha256(SOURCE) != EXPECTED_SHA256:
    raise RuntimeError("yoRadio glcdfont.c differs from the audited reference")

libdeps_dir = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / PIOENV
candidates = sorted(libdeps_dir.glob("Adafruit GFX Library*/glcdfont.c"))
if len(candidates) != 1:
    raise RuntimeError(
        "Expected exactly one project-local Adafruit GFX glcdfont.c, "
        f"found {len(candidates)} under {libdeps_dir}"
    )

target = candidates[0]
if not target.is_file() or sha256(target) != EXPECTED_SHA256:
    shutil.copyfile(SOURCE, target)
    print(f"Patched project-local Adafruit GFX font: {target}")