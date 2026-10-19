#!/usr/bin/env python3
"""Offline regression tests for the deterministic WWW integrity manifest."""

from __future__ import annotations

import gzip
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from www_asset_manifest import (  # noqa: E402
    HEADER_SIZE,
    MANIFEST_NAME,
    ManifestError,
    build_manifest,
    contract_from_header,
    parse_manifest,
    verify_assets,
)


class WwwAssetManifestTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.contract = contract_from_header(ROOT / "src/core/www_readiness.h")

    def make_assets(self, root: Path) -> None:
        for index, path in enumerate(self.contract["paths"]):
            target = root / str(path).removeprefix("/www/")
            target.write_bytes(gzip.compress(
                f"asset-{index}\n".encode(), compresslevel=9, mtime=0))

    def test_manifest_is_deterministic_and_verifies_full_gzip(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.make_assets(root)
            first, report = build_manifest(root, self.contract)
            second, repeated = build_manifest(root, self.contract)
            self.assertEqual(first, second)
            self.assertEqual(report["assetSetId"], repeated["assetSetId"])
            parsed = parse_manifest(first)
            verify_assets(root, parsed)
            self.assertEqual(len(parsed["assets"]), 7)
            self.assertEqual(parsed["trust"],
                             "integrity metadata generated with the assets; not a signature")

    def test_missing_and_invalid_gzip_fail_generation(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.make_assets(root)
            missing = root / str(self.contract["paths"][0]).removeprefix("/www/")
            missing.unlink()
            with self.assertRaises(ManifestError):
                build_manifest(root, self.contract)
            self.make_assets(root)
            missing.write_bytes(b"\x1f\x8b\x08\x00truncated")
            with self.assertRaises(ManifestError):
                build_manifest(root, self.contract)

    def test_manifest_and_asset_corruption_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.make_assets(root)
            manifest, _ = build_manifest(root, self.contract)
            truncated = manifest[:-1]
            with self.assertRaises(ManifestError):
                parse_manifest(truncated)
            corrupt = bytearray(manifest)
            corrupt[HEADER_SIZE + 70] ^= 1
            with self.assertRaises(ManifestError):
                parse_manifest(bytes(corrupt))

            parsed = parse_manifest(manifest)
            asset = root / str(self.contract["paths"][1]).removeprefix("/www/")
            bytes_ = bytearray(asset.read_bytes())
            bytes_[20] ^= 1
            asset.write_bytes(bytes_)
            with self.assertRaisesRegex(ManifestError, "SHA256 mismatch"):
                verify_assets(root, parsed)

    def test_repository_manifest_matches_final_artifacts(self) -> None:
        manifest_path = ROOT / "data/www" / MANIFEST_NAME
        parsed = parse_manifest(manifest_path.read_bytes())
        verify_assets(ROOT / "data/www", parsed)
        self.assertEqual(parsed["schema"], self.contract["schema"])
        self.assertEqual(parsed["wwwContract"], self.contract["wwwContract"])
        self.assertEqual(
            parsed["manifestSha256"],
            hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
