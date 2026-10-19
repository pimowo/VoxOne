#!/usr/bin/env python3

import ast
import copy
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "spiffs_usb_recovery", ROOT / "scripts" / "spiffs_usb_recovery.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class FakeAdapter:
    def __init__(self, table, before, after, identity, fail_at=None):
        self.table = table
        self.before = before
        self.after = after
        self.device_identity = identity
        self.fail_at = fail_at
        self.events = []
        self.written = False

    def identity(self):
        self.events.append(("identity",))
        if self.fail_at == "identity":
            raise MODULE.RecoveryError("simulated communication failure")
        return dict(self.device_identity)

    def read(self, offset, size):
        self.events.append(("read", offset, size))
        if self.fail_at == "read":
            raise MODULE.RecoveryError("simulated communication failure")
        if offset == MODULE.flash_tools.PARTITION_TABLE_OFFSET:
            return self.table
        return self.after if self.written else self.before

    def write(self, offset, image):
        self.events.append(("write", offset, image))
        if self.fail_at == "write":
            raise MODULE.RecoveryError("simulated interrupted write")
        self.written = True

    def verify(self, offset, image):
        self.events.append(("verify", offset, image))
        if self.fail_at == "verify":
            raise MODULE.RecoveryError("simulated verify failure")


class SpiffsUsbRecoveryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mkspiffs = MODULE.flash_tools.find_mkspiffs()
        if cls.mkspiffs is None:
            raise RuntimeError("mkspiffs is required for SPIFFS recovery tests")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.image = self.root / "VoxOne-0.8.0-spiffs.bin"
        self._make_spiffs(self.image)
        self.image_sha = MODULE.flash_tools.sha256(self.image)
        self.manifest = self.root / "image-manifest.json"
        MODULE.write_private_json(
            self.manifest,
            MODULE.manifest_for_image(
                "L0", self.image, self.image_sha, "4c56820b"
            ),
        )
        self.table = self._partition_table_region("L0")
        self.backup = bytes((index * 17) & 0xFF for index in range(0x30000))
        self.port = Path("/dev/serial/by-id/usb-test-voxone")
        self.port_identity = {
            "portById": str(self.port),
            "resolvedPort": "/dev/ttyACM-test",
            "usbSerial": "test-voxone",
            "usbVid": "303a",
            "usbPid": "1001",
        }
        self.device_identity = {
            "chip": "esp32s3",
            "chipDescription": "Chip is ESP32-S3",
            "mac": "02:00:00:00:00:01",
            "flashSize": 16 * 1024 * 1024,
        }

    def tearDown(self):
        self.temporary.cleanup()

    def _make_spiffs(self, target, omit=None):
        source = self.root / ("assets-" + target.stem)
        source.mkdir(parents=True)
        for asset in MODULE.required_asset_paths():
            if asset == omit:
                continue
            path = source / asset.lstrip("/")
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(
                gzip.compress(
                    ("payload:" + asset).encode("utf-8"),
                    compresslevel=9,
                    mtime=0,
                )
            )
        subprocess.run(
            [
                str(self.mkspiffs), "-c", str(source), "-b", "4096",
                "-p", "256", "-s", str(0x30000), str(target),
            ],
            check=True,
            capture_output=True,
        )

    @staticmethod
    def _entry(item):
        return struct.pack(
            "<HBBII16sI",
            MODULE.flash_tools.PARTITION_MAGIC,
            int(item["type"]),
            int(item["subtype"]),
            int(item["offset"]),
            int(item["size"]),
            str(item["label"]).encode("ascii").ljust(16, b"\0"),
            int(item.get("flags", 0)),
        )

    def _partition_table_region(self, profile):
        entries = b"".join(
            self._entry(item) for item in MODULE.expected_partitions(profile)
        )
        trailer = (
            struct.pack("<H", MODULE.flash_tools.PARTITION_MD5_MAGIC)
            + b"\xff" * 14
            + hashlib.md5(entries).digest()
        )
        return (entries + trailer).ljust(MODULE.PARTITION_TABLE_READ_SIZE, b"\xff")

    def _plan(self):
        output = self.root / "plan"
        plan = MODULE.build_restore_plan(
            "L0",
            self.port_identity,
            self.device_identity,
            self.device_identity["mac"],
            self.table,
            self.table,
            self.backup,
            self.backup,
            self.image,
            self.manifest,
            self.image_sha,
            "4c56820b",
            output,
        )
        path = MODULE.write_plan_files(
            output,
            plan,
            self.table,
            self.table,
            self.backup,
            self.backup,
        )
        MODULE.validate_restore_plan(plan)
        return path, plan

    def _execute(self, adapter, plan_path, plan, **overrides):
        options = {
            "plan_path": plan_path,
            "port": self.port,
            "expected_mac": self.device_identity["mac"],
            "authorization_token": plan["authorizationToken"],
            "confirm_rom_loader": True,
            "confirm_separate_approval": True,
            "adapter_factory": lambda _port, _spec: adapter,
            "port_verifier": lambda _port: dict(self.port_identity),
        }
        options.update(overrides)
        return MODULE.execute_restore(**options)

    def test_all_profiles_have_explicit_chip_flash_and_layout(self):
        self.assertEqual(set(MODULE.PROFILE_SPECS), {"L0", "M0", "S0", "DIN0", "W0"})
        for name, spec in MODULE.PROFILE_SPECS.items():
            partitions = MODULE.expected_partitions(name)
            spiffs = MODULE.spiffs_partition(partitions)
            self.assertEqual((spiffs["offset"], spiffs["size"]), (0x3D0000, 0x30000))
            self.assertLessEqual(spiffs["offset"] + spiffs["size"], spec.flash_size)
        self.assertEqual(MODULE.PROFILE_SPECS["W0"].chip, "esp32")
        self.assertEqual(MODULE.PROFILE_SPECS["L0"].flash_size, 16 * 1024 * 1024)

    def test_valid_image_manifest_checks_assets_gzip_and_hash(self):
        manifest = MODULE.validate_image_manifest(
            self.manifest,
            self.image,
            "L0",
            self.image_sha,
            "4c56820b",
        )
        self.assertEqual(manifest["image"]["size"], 0x30000)
        self.assertEqual(len(manifest["requiredAssets"]), 7)
        self.assertTrue(all(item["decompressedSize"] for item in manifest["requiredAssets"]))

    def test_image_size_format_hash_and_required_asset_fail_closed(self):
        oversized = self.root / "oversized.bin"
        oversized.write_bytes(self.image.read_bytes() + b"\xff")
        with self.assertRaisesRegex(MODULE.RecoveryError, "size mismatch"):
            MODULE.validate_spiffs_source(oversized, 0x30000)

        bad_header = self.root / "bad-header.bin"
        bad_header.write_bytes(b"\x55" * 0x30000)
        with self.assertRaisesRegex(MODULE.RecoveryError, "integrity validation failed"):
            MODULE.validate_spiffs_source(bad_header, 0x30000)

        with self.assertRaisesRegex(MODULE.RecoveryError, "operator value"):
            MODULE.manifest_for_image("L0", self.image, "0" * 64, "4c56820b")

        missing = self.root / "missing.bin"
        self._make_spiffs(missing, omit=MODULE.required_asset_paths()[0])
        with self.assertRaisesRegex(MODULE.RecoveryError, "missing required"):
            MODULE.validate_spiffs_source(missing, 0x30000)

    def test_manifest_rejects_wrong_profile_build_and_changed_image(self):
        with self.assertRaisesRegex(MODULE.RecoveryError, "profile mismatch"):
            MODULE.validate_image_manifest(
                self.manifest, self.image, "M0", self.image_sha, "4c56820b"
            )
        with self.assertRaisesRegex(MODULE.RecoveryError, "WWW build mismatch"):
            MODULE.validate_image_manifest(
                self.manifest, self.image, "L0", self.image_sha, "wrong-build"
            )
        changed = bytearray(self.image.read_bytes())
        changed[-1] ^= 1
        self.image.write_bytes(changed)
        with self.assertRaisesRegex(MODULE.RecoveryError, "SHA256 mismatch"):
            MODULE.validate_image_manifest(
                self.manifest, self.image, "L0", self.image_sha, "4c56820b"
            )

    def test_partition_table_must_match_selected_profile_exactly(self):
        partitions, report = MODULE.partition_table_from_region(self.table, "L0")
        self.assertTrue(report["md5Valid"])
        self.assertEqual(MODULE.spiffs_partition(partitions)["offset"], 0x3D0000)

        entries = MODULE.expected_partitions("L0")
        entries[-1] = dict(entries[-1], offset=0x3C0000)
        raw = b"".join(self._entry(item) for item in entries)
        changed = (
            raw
            + struct.pack("<H", MODULE.flash_tools.PARTITION_MD5_MAGIC)
            + b"\xff" * 14
            + hashlib.md5(raw).digest()
        ).ljust(MODULE.PARTITION_TABLE_READ_SIZE, b"\xff")
        with self.assertRaisesRegex(MODULE.RecoveryError, "does not exactly match"):
            MODULE.partition_table_from_region(changed, "L0")

    def test_identity_requires_profile_chip_flash_and_exact_mac(self):
        MODULE.verify_device_identity(
            self.device_identity, "L0", self.device_identity["mac"]
        )
        wrong = dict(self.device_identity, chip="esp32")
        with self.assertRaisesRegex(MODULE.RecoveryError, "chip mismatch"):
            MODULE.verify_device_identity(wrong, "L0", self.device_identity["mac"])
        wrong = dict(self.device_identity, flashSize=4 * 1024 * 1024)
        with self.assertRaisesRegex(MODULE.RecoveryError, "flash size mismatch"):
            MODULE.verify_device_identity(wrong, "L0", self.device_identity["mac"])
        with self.assertRaisesRegex(MODULE.RecoveryError, "MAC"):
            MODULE.verify_device_identity(self.device_identity, "L0", "00:11:22:33:44:55")

    def test_plan_is_exactly_spiffs_and_protects_all_other_partitions(self):
        plan_path, plan = self._plan()
        validated = MODULE.validate_restore_plan(plan)
        self.assertEqual(validated["target"]["offset"], 0x3D0000)
        self.assertEqual(validated["target"]["size"], 0x30000)
        self.assertEqual(
            {item["label"] for item in validated["protectedRanges"]},
            {"nvs", "otadata", "app0", "app1"},
        )
        self.assertFalse(validated["writeAuthorized"])
        self.assertIn("erase_flash", validated["forbiddenOperations"])
        MODULE.verify_plan_sidecar(plan_path)

    def test_short_or_disagreeing_backups_block_plan(self):
        with self.assertRaisesRegex(MODULE.RecoveryError, "wrong size"):
            MODULE.build_restore_plan(
                "L0", self.port_identity, self.device_identity,
                self.device_identity["mac"], self.table, self.table,
                self.backup[:-1], self.backup[:-1], self.image, self.manifest,
                self.image_sha, "4c56820b", self.root / "short-plan",
            )
        altered = bytearray(self.backup)
        altered[123] ^= 1
        with self.assertRaisesRegex(MODULE.RecoveryError, "backup reads disagree"):
            MODULE.build_restore_plan(
                "L0", self.port_identity, self.device_identity,
                self.device_identity["mac"], self.table, self.table,
                self.backup, bytes(altered), self.image, self.manifest,
                self.image_sha, "4c56820b", self.root / "bad-plan",
            )

    def test_plan_tampering_breaks_sidecar_or_token(self):
        plan_path, plan = self._plan()
        plan["target"]["offset"] = 0x10000
        plan_path.write_text(json.dumps(plan), encoding="utf-8")
        with self.assertRaisesRegex(MODULE.RecoveryError, "sidecar mismatch"):
            MODULE.verify_plan_sidecar(plan_path)

    def test_plan_semantics_cannot_authorize_other_ranges_or_retry(self):
        _plan_path, plan = self._plan()
        missing_protection = copy.deepcopy(plan)
        missing_protection["protectedRanges"].pop()
        with self.assertRaisesRegex(MODULE.RecoveryError, "protected ranges"):
            MODULE.validate_restore_plan(missing_protection)

        automatic_retry = copy.deepcopy(plan)
        automatic_retry["postWrite"]["automaticRetry"] = True
        with self.assertRaisesRegex(MODULE.RecoveryError, "post-write safety"):
            MODULE.validate_restore_plan(automatic_retry)

    def test_write_requires_both_confirmations_and_exact_token(self):
        plan_path, plan = self._plan()
        adapter = FakeAdapter(
            self.table, self.backup, self.image.read_bytes(), self.device_identity
        )
        with self.assertRaisesRegex(MODULE.RecoveryError, "both"):
            self._execute(adapter, plan_path, plan, confirm_separate_approval=False)
        with self.assertRaisesRegex(MODULE.RecoveryError, "token"):
            self._execute(adapter, plan_path, plan, authorization_token="wrong")
        self.assertFalse(any(event[0] == "write" for event in adapter.events))

    def test_success_executes_one_exact_write_verify_and_readback(self):
        plan_path, plan = self._plan()
        adapter = FakeAdapter(
            self.table, self.backup, self.image.read_bytes(), self.device_identity
        )
        result = self._execute(adapter, plan_path, plan)
        writes = [event for event in adapter.events if event[0] == "write"]
        self.assertEqual(len(writes), 1)
        self.assertEqual(writes[0][1], 0x3D0000)
        self.assertEqual([event[0] for event in adapter.events].count("verify"), 1)
        self.assertEqual(result["writeAttempts"], 1)
        self.assertEqual(result["readbackSha256"], self.image_sha)
        self.assertEqual(
            (plan_path.parent / "spiffs-after-readback.bin").read_bytes(),
            self.image.read_bytes(),
        )

    def test_readback_mismatch_stops_without_retry(self):
        plan_path, plan = self._plan()
        bad_readback = bytearray(self.image.read_bytes())
        bad_readback[100] ^= 1
        adapter = FakeAdapter(
            self.table, self.backup, bytes(bad_readback), self.device_identity
        )
        with self.assertRaisesRegex(MODULE.RecoveryError, "readback SHA256 mismatch"):
            self._execute(adapter, plan_path, plan)
        self.assertEqual([event[0] for event in adapter.events].count("write"), 1)
        self.assertFalse((plan_path.parent / "execution-result.json").exists())

    def test_interrupted_write_or_communication_failure_never_retries(self):
        plan_path, plan = self._plan()
        interrupted = FakeAdapter(
            self.table, self.backup, self.image.read_bytes(), self.device_identity,
            fail_at="write",
        )
        with self.assertRaisesRegex(MODULE.RecoveryError, "interrupted"):
            self._execute(interrupted, plan_path, plan)
        self.assertEqual([event[0] for event in interrupted.events].count("write"), 1)
        self.assertNotIn("verify", [event[0] for event in interrupted.events])

        # A fresh plan is required because a failed attempt is never retried.
        other_path, other_plan = self._plan_from_new_root("communication")
        unavailable = FakeAdapter(
            self.table, self.backup, self.image.read_bytes(), self.device_identity,
            fail_at="identity",
        )
        with self.assertRaisesRegex(MODULE.RecoveryError, "communication failure"):
            self._execute(unavailable, other_path, other_plan)
        self.assertFalse(any(event[0] == "write" for event in unavailable.events))

    def _plan_from_new_root(self, name):
        output = self.root / name
        plan = MODULE.build_restore_plan(
            "L0", self.port_identity, self.device_identity,
            self.device_identity["mac"], self.table, self.table,
            self.backup, self.backup, self.image, self.manifest,
            self.image_sha, "4c56820b", output,
        )
        path = MODULE.write_plan_files(
            output, plan, self.table, self.table, self.backup, self.backup
        )
        return path, plan

    def test_stale_live_table_or_backup_blocks_before_write(self):
        plan_path, plan = self._plan()
        changed_backup = bytearray(self.backup)
        changed_backup[0] ^= 1
        adapter = FakeAdapter(
            self.table, bytes(changed_backup), self.image.read_bytes(),
            self.device_identity,
        )
        with self.assertRaisesRegex(MODULE.RecoveryError, "changed since"):
            self._execute(adapter, plan_path, plan)
        self.assertFalse(any(event[0] == "write" for event in adapter.events))

    def test_only_execute_restore_can_call_device_adapter_write(self):
        tree = ast.parse(
            (ROOT / "scripts" / "spiffs_usb_recovery.py").read_text(encoding="utf-8")
        )
        calls = []
        for node in ast.walk(tree):
            if isinstance(node, ast.FunctionDef):
                for child in ast.walk(node):
                    if (
                        isinstance(child, ast.Call)
                        and isinstance(child.func, ast.Attribute)
                        and child.func.attr == "write"
                        and isinstance(child.func.value, ast.Name)
                        and child.func.value.id == "adapter"
                    ):
                        calls.append(node.name)
        self.assertEqual(calls, ["execute_restore"])

    def test_esptool_never_auto_resets_or_uses_stub(self):
        base = MODULE.esptool_base(self.port, "esp32s3")
        self.assertEqual(base.count("no_reset"), 2)
        self.assertIn("--no-stub", base)
        self.assertNotIn("default_reset", base)
        self.assertNotIn("hard_reset", base)


if __name__ == "__main__":
    unittest.main()
