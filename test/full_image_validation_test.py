#!/usr/bin/env python3
"""Regression tests for fail-closed complete image validation."""

from __future__ import annotations

import json
import os
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from full_image_validation import (  # noqa: E402
    PROFILE_CONTRACTS,
    ValidationError,
    validate_component_set,
    validate_full_image,
)


BUILD_ROOT_VALUE = os.environ.get("VOXONE_FULL_IMAGE_BUILD_DIR", "")
BUILD_ROOT = Path(BUILD_ROOT_VALUE).resolve() if BUILD_ROOT_VALUE else None


@unittest.skipUnless(BUILD_ROOT is not None, "integration build directory not provided")
class FullImageValidationIntegrationTest(unittest.TestCase):
    maxDiff = None

    @classmethod
    def setUpClass(cls) -> None:
        assert BUILD_ROOT is not None
        cls.workspace = ROOT / ".pio" / "verify" / "full-image-validation"
        cls.workspace.mkdir(parents=True, exist_ok=True)
        cls.version = "0.8.0"
        for profile, contract in PROFILE_CONTRACTS.items():
            build = BUILD_ROOT / contract["environment"]
            required = [
                build / "bootloader.bin",
                build / "partitions.bin",
                build / "firmware.bin",
                build / "firmware.elf",
                build / "firmware.validation.json",
                build / "spiffs.bin",
                build / f"VoxOne-{cls.version}-full.bin",
                build / f"VoxOne-{cls.version}-full.manifest.json",
            ]
            missing = [str(path) for path in required if not path.is_file()]
            if missing:
                raise AssertionError(f"missing {profile} integration artifacts: {missing}")

    def fixture(self, profile: str, temporary: Path):
        assert BUILD_ROOT is not None
        contract = PROFILE_CONTRACTS[profile]
        build = BUILD_ROOT / contract["environment"]
        full = build / f"VoxOne-{self.version}-full.bin"
        full_data = full.read_bytes()
        otadata = temporary / f"{profile}-otadata.bin"
        otadata.write_bytes(full_data[0xE000:0x10000])
        manifest = build / "firmware.validation.json"
        context = json.loads(manifest.read_text(encoding="utf-8"))["context"]
        components = {
            "bootloader": build / "bootloader.bin",
            "partitions": build / "partitions.bin",
            "otadata": otadata,
            "firmware": build / "firmware.bin",
            "spiffs": build / "spiffs.bin",
        }
        return build, full, context, components

    def validate(self, profile: str, temporary: Path, **overrides):
        build, full, context, components = self.fixture(profile, temporary)
        components.update(overrides.get("components", {}))
        validation = validate_component_set(
            overrides.get("context", context),
            components,
            overrides.get("firmware_elf", build / "firmware.elf"),
            overrides.get("firmware_manifest", build / "firmware.validation.json"),
            ROOT / "partitions_yoradio.csv",
            ROOT / "data",
            temporary,
        )
        return validate_full_image(
            overrides.get("full_image", full), validation, components
        )

    def test_valid_complete_set_and_output_for_all_profiles(self) -> None:
        with tempfile.TemporaryDirectory(dir=self.workspace) as directory:
            temporary = Path(directory)
            for profile, contract in PROFILE_CONTRACTS.items():
                with self.subTest(profile=profile):
                    report = self.validate(profile, temporary)
                    self.assertEqual(report["context"]["profile"], profile)
                    self.assertEqual(report["context"]["security"], "production")
                    self.assertEqual(report["fullImage"]["size"], contract["flashSize"])
                    self.assertEqual(set(report["components"]), {
                        "bootloader", "partitions", "otadata", "firmware", "spiffs"
                    })

    def test_m0_firmware_substituted_as_s0_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(dir=self.workspace) as directory:
            temporary = Path(directory)
            assert BUILD_ROOT is not None
            foreign = BUILD_ROOT / "m0" / "firmware.bin"
            with self.assertRaisesRegex(
                ValidationError, "profile identity|ELF SHA256|manifest"
            ):
                self.validate(
                    "S0", temporary, components={"firmware": foreign}
                )

    def test_every_cross_profile_firmware_set_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(dir=self.workspace) as directory:
            temporary = Path(directory)
            assert BUILD_ROOT is not None
            profiles = tuple(PROFILE_CONTRACTS)
            for target in profiles:
                for foreign in profiles:
                    if target == foreign:
                        continue
                    foreign_build = (
                        BUILD_ROOT / PROFILE_CONTRACTS[foreign]["environment"]
                    )
                    with self.subTest(target=target, foreign=foreign):
                        with self.assertRaises(ValidationError):
                            self.validate(
                                target,
                                temporary,
                                components={
                                    "firmware": foreign_build / "firmware.bin"
                                },
                                firmware_elf=foreign_build / "firmware.elf",
                                firmware_manifest=(
                                    foreign_build / "firmware.validation.json"
                                ),
                            )

    def test_truncated_spiffs_1024_bytes_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(dir=self.workspace) as directory:
            temporary = Path(directory)
            _, _, _, components = self.fixture("S0", temporary)
            truncated = temporary / "spiffs-1024.bin"
            truncated.write_bytes(components["spiffs"].read_bytes()[:1024])
            with self.assertRaisesRegex(ValidationError, "SPIFFS image size mismatch"):
                self.validate(
                    "S0", temporary, components={"spiffs": truncated}
                )

    def test_each_truncated_flash_component_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(dir=self.workspace) as directory:
            temporary = Path(directory)
            _, _, _, components = self.fixture("S0", temporary)
            for label, source in components.items():
                truncated = temporary / f"truncated-{label}.bin"
                data = source.read_bytes()
                truncated.write_bytes(data[:max(1, len(data) // 2)])
                with self.subTest(component=label):
                    with self.assertRaises(ValidationError):
                        self.validate(
                            "S0", temporary, components={label: truncated}
                        )

    def test_identity_flash_and_security_mismatches_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory(dir=self.workspace) as directory:
            temporary = Path(directory)
            _, _, context, _ = self.fixture("S0", temporary)
            changes = {
                "profile": "M0",
                "mcu": "esp32",
                "flashSize": 16 * 1024 * 1024,
                "flashMode": "qio",
                "flashFrequency": "40m",
                "buildIdentity": "0000000",
                "security": "test",
            }
            for field, value in changes.items():
                changed = dict(context)
                changed[field] = value
                with self.subTest(field=field):
                    with self.assertRaises(ValidationError):
                        self.validate("S0", temporary, context=changed)

    def test_nonproduction_security_manifest_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(dir=self.workspace) as directory:
            temporary = Path(directory)
            build, _, _, _ = self.fixture("M0", temporary)
            manifest = json.loads(
                (build / "firmware.validation.json").read_text(encoding="utf-8")
            )
            manifest["context"]["security"] = "test"
            tampered = temporary / "firmware-test-security.validation.json"
            tampered.write_text(
                json.dumps(manifest, sort_keys=True) + "\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(ValidationError, "manifest"):
                self.validate("M0", temporary, firmware_manifest=tampered)

    def test_truncated_or_moved_full_image_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(dir=self.workspace) as directory:
            temporary = Path(directory)
            _, full, _, _ = self.fixture("S0", temporary)
            data = full.read_bytes()
            truncated = temporary / "full-truncated.bin"
            truncated.write_bytes(data[:-1])
            with self.assertRaisesRegex(ValidationError, "full image size mismatch"):
                self.validate("S0", temporary, full_image=truncated)
            moved = temporary / "full-moved.bin"
            changed = bytearray(data)
            changed[0x10000], changed[0x10001] = changed[0x10001], changed[0x10000]
            moved.write_bytes(changed)
            with self.assertRaisesRegex(ValidationError, "differs from firmware"):
                self.validate("S0", temporary, full_image=moved)


if __name__ == "__main__":
    unittest.main(verbosity=2)
