#!/usr/bin/env python3
"""Run pinned esptool with all write retry limits forced to one attempt."""

from __future__ import annotations

from pathlib import Path
import sys
from typing import Any


MAX_WRITE_BLOCK_ATTEMPTS = 1
MAX_WRITE_FLASH_ATTEMPTS = 1
ROOT_ARGUMENT = "--pinned-esptool-root"


class RetryPolicyError(RuntimeError):
    """The pinned esptool runtime cannot enforce the write-once policy."""


def activate_write_once_policy(esptool_root: Path) -> Any:
    """Import one exact esptool tree and disable its two write retry loops."""
    root = esptool_root.resolve(strict=True)
    package = root / "esptool" / "__init__.py"
    contrib = root / "_contrib"
    if not package.is_file() or not contrib.is_dir():
        raise RetryPolicyError("pinned esptool tree is incomplete")

    for path in (str(contrib), str(root)):
        if path in sys.path:
            sys.path.remove(path)
        sys.path.insert(0, path)

    import esptool  # type: ignore[import-not-found]
    from esptool import loader  # type: ignore[import-not-found]

    loaded_package = Path(esptool.__file__).resolve()
    if loaded_package != package.resolve():
        raise RetryPolicyError("imported esptool does not match the pinned tree")
    if not hasattr(loader, "WRITE_BLOCK_ATTEMPTS") or not hasattr(
        loader.ESPLoader, "WRITE_FLASH_ATTEMPTS"
    ):
        raise RetryPolicyError("esptool write retry controls are unavailable")

    loader.WRITE_BLOCK_ATTEMPTS = MAX_WRITE_BLOCK_ATTEMPTS
    loader.ESPLoader.WRITE_FLASH_ATTEMPTS = MAX_WRITE_FLASH_ATTEMPTS
    if (
        loader.WRITE_BLOCK_ATTEMPTS != MAX_WRITE_BLOCK_ATTEMPTS
        or loader.ESPLoader.WRITE_FLASH_ATTEMPTS != MAX_WRITE_FLASH_ATTEMPTS
    ):
        raise RetryPolicyError("esptool write retry policy did not apply")
    return esptool


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[1] != ROOT_ARGUMENT:
        raise RetryPolicyError(
            f"usage: {Path(sys.argv[0]).name} {ROOT_ARGUMENT} PATH [esptool arguments]"
        )
    esptool_root = Path(sys.argv[2])
    del sys.argv[1:3]
    esptool = activate_write_once_policy(esptool_root)
    esptool._main()
    return 0


if __name__ == "__main__":
    sys.exit(main())
