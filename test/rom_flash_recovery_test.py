#!/usr/bin/env python3

import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "rom_flash_recovery", ROOT / "scripts" / "rom_flash_recovery.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

GUARD_SPEC = importlib.util.spec_from_file_location(
    "esptool_write_once", ROOT / "scripts" / "esptool_write_once.py"
)
WRITE_ONCE = importlib.util.module_from_spec(GUARD_SPEC)
assert GUARD_SPEC.loader is not None
sys.modules[GUARD_SPEC.name] = WRITE_ONCE
GUARD_SPEC.loader.exec_module(WRITE_ONCE)


class RomFlashRecoveryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        tool = MODULE.flash_tools.find_mkspiffs()
        if tool is None:
            cls.spiffs_image = None
            return
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "empty"
            source.mkdir()
            image = root / "spiffs.bin"
            subprocess.run(
                [str(tool), "-c", str(source), "-b", "4096", "-p", "256",
                 "-s", str(0x30000), str(image)],
                check=True, capture_output=True,
            )
            cls.spiffs_image = image.read_bytes()

    def setUp(self):
        runtime_root = os.environ.get("VOXONE_TEST_RUNTIME_ROOT")
        MODULE.configure_runtime_selection(
            profile=os.environ.get("VOXONE_TEST_RUNTIME_PROFILE"),
            root=Path(runtime_root) if runtime_root else None,
            confirm_port_open_may_reset=True,
        )

    @staticmethod
    def partition(label, part_type, subtype, offset, size):
        return struct.pack(
            "<HBBII16sI", 0x50AA, part_type, subtype, offset, size,
            label.encode("ascii").ljust(16, b"\0"), 0,
        )

    @staticmethod
    def ota_entry(sequence, state=2):
        return struct.pack(
            "<I20sII", sequence, b"\xff" * 20, state,
            MODULE.flash_tools.esp_ota_crc32(sequence),
        )

    @staticmethod
    def esp_image(profile, size=0x1E0000):
        data = bytearray(
            b"\xe9\x01\x02\x2f\x00\x00\x00\x00"
            b"\xee\x00\x00\x00\x09\x00\x00\x00"
            b"\x00\xff\xff\x00\x00\x00\x00\x01"
        )
        payload = bytes(range(1, 34)) + b"\0" + profile.encode() + b"\0"
        data += struct.pack("<II", 0x3C110020, len(payload)) + payload
        checksum = MODULE.flash_tools.ESP_IMAGE_CHECKSUM_SEED
        for value in payload:
            checksum ^= value
        checksum_offset = len(data) | 0x0F
        data += b"\0" * (checksum_offset - len(data)) + bytes([checksum])
        data += hashlib.sha256(data).digest()
        return bytes(data).ljust(size, b"\xff")

    def synthetic_image(
        self, profile="S0", bad_partition=False, app_profile=None,
        app1_ota_state=2,
    ):
        if self.spiffs_image is None:
            raise RuntimeError("mkspiffs is required for synthetic image tests")
        image = bytearray(b"\xff" * MODULE.PROFILE_SPECS[profile].flash_size)
        image[:0x8000] = self.esp_image(profile, 0x8000)
        app_profile = app_profile or profile
        entries = b"".join([
            self.partition("nvs", 1, 2, 0x9000, 0x5000),
            self.partition("otadata", 1, 0, 0xE000, 0x2000),
            self.partition("app0", 0, 0x10, 0x10000, 0x1E0000),
            self.partition(
                "app1", 0, 0x11, 0x1F0000,
                0x1D0000 if bad_partition else 0x1E0000,
            ),
            self.partition("spiffs", 1, 0x82, 0x3D0000, 0x30000),
        ])
        table = (
            entries + struct.pack("<H", MODULE.flash_tools.PARTITION_MD5_MAGIC)
            + b"\xff" * 14 + hashlib.md5(entries).digest()
        )
        image[0x8000:0x8000 + len(table)] = table
        image[0xE000:0xE020] = self.ota_entry(1)
        image[0xF000:0xF020] = self.ota_entry(2, app1_ota_state)
        image[0x10000:0x1F0000] = self.esp_image(app_profile)
        image[0x1F0000:0x3D0000] = self.esp_image(app_profile)
        image[0x3D0000:0x400000] = self.spiffs_image
        return bytes(image)

    def write_image(self, directory, profile="S0", name="flash.bin", **kwargs):
        path = Path(directory) / name
        path.write_bytes(self.synthetic_image(profile, **kwargs))
        path.chmod(0o600)
        return path

    @staticmethod
    def identity(profile="S0", mac="02:00:00:00:00:01"):
        return {
            "profile": profile, "chip": "esp32s3", "mac": mac,
            "flashSize": MODULE.PROFILE_SPECS[profile].flash_size,
            "usbRole": f"{profile}_NATIVE", "usbVid": "303a",
            "usbPid": "1001", "usbSerial": f"SERIAL-{profile}",
            "usbById": f"/dev/serial/by-id/usb-VoxOne_{profile}",
            "usbRegistrySha256": "1" * 64,
            "securityInfoSha256": "2" * 64,
            "toolchainManifestSha256": MODULE.sha256_file(
                MODULE.DEFAULT_TOOLCHAIN_MANIFEST
            ),
            "resetLinesUsed": "UNVERIFIED",
            "automaticResetRequested": False,
            "portOpenMayAssertDtrRts": True,
            "operatorAcceptedPortOpenResetRisk": True,
            "flasherStubUsed": False,
        }

    @staticmethod
    def storage(name):
        return {
            "host": "test-host", "target": f"/mnt/{name}",
            "source": f"/dev/{name}1", "fstype": "ext4",
            "uuid": f"uuid-{name}",
            "majorMinor": "8:1" if name == "bench" else "8:17",
            "fsRoot": "/", "deviceClass": "block", "durable": True,
            "failureDomain": f"block:/dev/{name}:{name}-serial",
            "physicalDevice": f"/dev/{name}", "mediaId": f"{name}-serial",
        }

    @staticmethod
    def write_json(path, value):
        path.write_text(json.dumps(value, indent=2), encoding="utf-8")
        path.chmod(0o600)

    def build_bundle(self, root, pending=False, forged_inspection=None):
        root = Path(root)
        bench, offsite = root / "bench", root / "offsite"
        bench.mkdir(mode=0o700)
        offsite.mkdir(mode=0o700)
        image = self.write_image(
            bench, name="full-flash-a.bin",
            app1_ota_state=1 if pending else 2,
        )
        second = bench / "full-flash-b.bin"
        shutil.copyfile(image, second)
        second.chmod(0o600)
        device = self.identity()
        identity_path = bench / "device-identity.json"
        self.write_json(identity_path, {
            "schema": MODULE.DEVICE_IDENTITY_SCHEMA,
            "createdAt": MODULE.now_iso(), "device": device,
        })
        state_path = bench / "backup-state.json"
        self.write_json(state_path, {
            "schema": MODULE.BACKUP_STATE_SCHEMA,
            "createdAt": MODULE.now_iso(), "profile": "S0", "device": device,
            "imageA": MODULE.file_record(image),
            "inspectionA": MODULE.inspect_backup_image("S0", image),
            "identityManifestSha256": MODULE.sha256_file(identity_path),
        })
        bench_storage = self.storage("bench")
        offsite_storage = self.storage("offsite")
        attestation_path = bench / "offsite-attestation.json"
        self.write_json(attestation_path, {
            "schema": MODULE.OFFSITE_ATTESTATION_SCHEMA,
            "operator": "operator", "approvalId": "OFFSITE-1",
            "confirmedAt": MODULE.now_iso(),
            "independentPhysicalMediaConfirmed": True,
            "offBenchCopyConfirmed": True,
            "benchFailureDomain": bench_storage["failureDomain"],
            "offsiteFailureDomain": offsite_storage["failureDomain"],
        })
        sources = {
            "backupState": state_path, "identityManifest": identity_path,
            "image": image, "verificationRead": second,
            "offsiteAttestation": attestation_path,
        }
        offsite_records = {}
        for key, source in sources.items():
            target = offsite / source.name
            shutil.copyfile(source, target)
            target.chmod(0o600)
            offsite_records[key] = MODULE.file_record(target)
        inspection = MODULE.inspect_backup_image("S0", image)
        core = {
            "schema": MODULE.VERIFIED_BACKUP_SCHEMA,
            "createdAt": MODULE.now_iso(), "profile": "S0", "device": device,
            "benchDirectory": str(bench.resolve()),
            **{key: MODULE.file_record(path) for key, path in sources.items()},
            "twoIndependentReadsMatch": True,
            "inspection": inspection if forged_inspection is None else forged_inspection,
            "logicalFingerprints": {},
            "offsiteCopy": {
                "directory": str(offsite.resolve()), "files": offsite_records,
                "differentFilesystem": True, "independentFailureDomain": True,
            },
            "storage": {"bench": bench_storage, "offsite": offsite_storage},
            "offsiteCopyVerified": True, "restoreAuthorized": False,
            "restorePhysicallyQualified": False,
        }
        manifest_path = bench / "verified-backup.json"
        manifest = MODULE.finalize_verified_manifest(core)
        self.write_json(manifest_path, manifest)
        self.write_json(offsite / "verified-backup.json", manifest)
        return {
            "manifest": manifest_path, "bench": bench, "offsite": offsite,
            "image": image, "second": second,
            "benchStorage": bench_storage, "offsiteStorage": offsite_storage,
        }

    @staticmethod
    def storage_lookup(bundle):
        def lookup(path):
            resolved = Path(path).resolve()
            if resolved == bundle["bench"].resolve():
                return dict(bundle["benchStorage"])
            if resolved == bundle["offsite"].resolve():
                return dict(bundle["offsiteStorage"])
            raise AssertionError(f"unexpected storage lookup: {resolved}")
        return lookup

    def validate_bundle(self, bundle):
        with mock.patch.object(
            MODULE, "storage_identity", side_effect=self.storage_lookup(bundle)
        ):
            return MODULE.validate_verified_backup(bundle["manifest"])

    def test_s0_and_l0_images_pass_full_inspection(self):
        with tempfile.TemporaryDirectory() as temporary:
            s0 = MODULE.inspect_backup_image(
                "S0", self.write_image(temporary, "S0", "s0.bin")
            )
            l0 = MODULE.inspect_backup_image(
                "L0", self.write_image(temporary, "L0", "l0.bin")
            )
        self.assertEqual(s0["size"], 4 * 1024 * 1024)
        self.assertEqual(l0["size"], 16 * 1024 * 1024)
        self.assertEqual(s0["bootloader"]["chipId"], 9)
        self.assertEqual(s0["selectedBootSlot"], "app1")
        self.assertTrue(l0["rollbackAvailable"])

    def test_erased_truncated_and_corrupt_bootloaders_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            cases = {}
            erased = bytearray(self.synthetic_image())
            erased[:0x8000] = b"\xff" * 0x8000
            cases["erased"] = erased
            truncated = bytearray(self.synthetic_image())
            truncated[40:0x8000] = b"\xff" * (0x8000 - 40)
            cases["truncated"] = truncated
            corrupt = bytearray(self.synthetic_image())
            corrupt[40] ^= 0x55
            cases["corrupt"] = corrupt
            for name, data in cases.items():
                path = Path(temporary) / f"{name}.bin"
                path.write_bytes(data)
                path.chmod(0o600)
                with self.subTest(name=name), self.assertRaisesRegex(
                    MODULE.RecoveryError, "bootloader"
                ):
                    MODULE.inspect_backup_image("S0", path)

    def test_wrong_size_profile_and_partition_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            s0 = self.write_image(temporary)
            with self.assertRaisesRegex(MODULE.RecoveryError, "size mismatch"):
                MODULE.inspect_backup_image("L0", s0)
            wrong = self.write_image(
                temporary, name="wrong.bin", app_profile="M0"
            )
            with self.assertRaisesRegex(MODULE.RecoveryError, "profile identity"):
                MODULE.inspect_backup_image("S0", wrong)
            bad = self.write_image(
                temporary, name="bad.bin", bad_partition=True
            )
            with self.assertRaisesRegex(MODULE.RecoveryError, "partition table"):
                MODULE.inspect_backup_image("S0", bad)

    def test_incomplete_backup_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "short.bin"
            path.write_bytes(b"\xff" * (4 * 1024 * 1024 - 1))
            with self.assertRaisesRegex(MODULE.RecoveryError, "size mismatch"):
                MODULE.inspect_backup_image("S0", path)

    def test_identity_binds_device_port_security_and_toolchain(self):
        valid = {"schema": MODULE.DEVICE_IDENTITY_SCHEMA, "device": self.identity()}
        self.assertEqual(
            MODULE.validate_identity_manifest(valid),
            ("S0", "02:00:00:00:00:01", "S0_NATIVE"),
        )
        for key, value in (
            ("mac", "02:00:00:00:00:02"), ("usbById", "/dev/serial/by-id/x"),
            ("securityInfoSha256", "3" * 64), ("usbRegistrySha256", "4" * 64),
        ):
            other = self.identity()
            other[key] = value
            self.assertFalse(MODULE.identity_matches(valid["device"], other), key)
        invalid = json.loads(json.dumps(valid))
        invalid["device"]["toolchainManifestSha256"] = "0" * 64
        with self.assertRaisesRegex(MODULE.RecoveryError, "different toolchain"):
            MODULE.validate_identity_manifest(invalid)

    def test_complete_bundle_is_freshly_inspected_and_accepted(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = self.build_bundle(temporary)
            manifest, image, inspection = self.validate_bundle(bundle)
            self.assertEqual(image, bundle["image"])
            self.assertEqual(inspection["otadata"]["activeState"], "VALID")
            self.assertEqual(manifest["device"]["mac"], self.identity()["mac"])

    def test_manifest_tamper_and_pending_verify_forgery_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = self.build_bundle(temporary)
            manifest = MODULE.load_json(bundle["manifest"])
            manifest["inspection"]["size"] = 1
            self.write_json(bundle["manifest"], manifest)
            with self.assertRaisesRegex(MODULE.RecoveryError, "content changed"):
                self.validate_bundle(bundle)
        with tempfile.TemporaryDirectory() as temporary:
            valid = self.write_image(temporary, name="valid.bin")
            forged = MODULE.inspect_backup_image("S0", valid)
            valid.unlink()
            bundle = self.build_bundle(
                temporary, pending=True, forged_inspection=forged
            )
            with self.assertRaisesRegex(
                MODULE.RecoveryError, "fresh byte inspection|terminal VALID"
            ):
                self.validate_bundle(bundle)

    def test_two_reads_and_offsite_files_are_mandatory(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = self.build_bundle(temporary)
            bundle["second"].unlink()
            with self.assertRaisesRegex(MODULE.RecoveryError, "missing"):
                self.validate_bundle(bundle)

    def test_backup_state_lineage_and_offsite_manifest_are_mandatory(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = self.build_bundle(temporary)
            state_path = bundle["bench"] / "backup-state.json"
            state = MODULE.load_json(state_path)
            state["identityManifestSha256"] = "0" * 64
            self.write_json(state_path, state)
            manifest = MODULE.load_json(bundle["manifest"])
            manifest["backupState"] = MODULE.file_record(state_path)
            offsite_state = bundle["offsite"] / state_path.name
            shutil.copyfile(state_path, offsite_state)
            offsite_state.chmod(0o600)
            manifest["offsiteCopy"]["files"]["backupState"] = MODULE.file_record(
                offsite_state
            )
            core = {k: v for k, v in manifest.items() if k != "manifestSha256"}
            updated = MODULE.finalize_verified_manifest(core)
            self.write_json(bundle["manifest"], updated)
            self.write_json(bundle["offsite"] / "verified-backup.json", updated)
            with self.assertRaisesRegex(MODULE.RecoveryError, "lineage mismatch"):
                self.validate_bundle(bundle)
        with tempfile.TemporaryDirectory() as temporary:
            bundle = self.build_bundle(temporary)
            (bundle["offsite"] / "verified-backup.json").unlink()
            with self.assertRaisesRegex(MODULE.RecoveryError, "missing"):
                self.validate_bundle(bundle)
        with tempfile.TemporaryDirectory() as temporary:
            bundle = self.build_bundle(temporary)
            (bundle["offsite"] / bundle["image"].name).unlink()
            with self.assertRaisesRegex(MODULE.RecoveryError, "missing"):
                self.validate_bundle(bundle)

    def test_other_device_and_symlink_substitution_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = self.build_bundle(temporary)
            manifest = MODULE.load_json(bundle["manifest"])
            manifest["device"]["mac"] = "02:00:00:00:00:02"
            core = {k: v for k, v in manifest.items() if k != "manifestSha256"}
            self.write_json(bundle["manifest"], MODULE.finalize_verified_manifest(core))
            with self.assertRaisesRegex(MODULE.RecoveryError, "different device"):
                self.validate_bundle(bundle)
        with tempfile.TemporaryDirectory() as temporary:
            bundle = self.build_bundle(temporary)
            real = bundle["bench"] / "real.bin"
            bundle["image"].rename(real)
            bundle["image"].symlink_to(real)
            with self.assertRaisesRegex(MODULE.RecoveryError, "symlink"):
                self.validate_bundle(bundle)

    def test_offsite_must_be_distinct_durable_failure_domain(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = self.build_bundle(temporary)
            bundle["offsiteStorage"]["failureDomain"] = (
                bundle["benchStorage"]["failureDomain"]
            )
            with self.assertRaisesRegex(
                MODULE.RecoveryError, "storage identity changed|failure domain"
            ):
                self.validate_bundle(bundle)

    def make_plan(self, root):
        image = self.write_image(root)
        manifest = Path(root) / "verified.json"
        manifest.write_text("{}", encoding="utf-8")
        return MODULE.finalize_restore_plan(MODULE.restore_plan_core(
            manifest, image, self.identity(), MODULE.table_region_from_image(image)
        ))

    def test_restore_plan_is_disabled_tamper_evident_and_expires(self):
        with tempfile.TemporaryDirectory() as temporary:
            plan = self.make_plan(temporary)
            MODULE.validate_restore_plan(plan)
            self.assertFalse(plan["write"]["writeAuthorized"])
            self.assertFalse(plan["write"]["automaticWriteRetry"])
            self.assertTrue(plan["write"]["romSyncRetriesMayOccurBeforeWrite"])
            tampered = json.loads(json.dumps(plan))
            tampered["source"]["sha256"] = "0" * 64
            with self.assertRaisesRegex(MODULE.RecoveryError, "content changed"):
                MODULE.validate_restore_plan(tampered)
            core = {
                key: value for key, value in plan.items()
                if key not in {"planSha256", "authorizationToken"}
            }
            old = datetime.now(timezone.utc) - timedelta(hours=3)
            core["createdAt"] = old.isoformat().replace("+00:00", "Z")
            core["expiresAt"] = (
                old + timedelta(hours=2)
            ).isoformat().replace("+00:00", "Z")
            with self.assertRaisesRegex(MODULE.RecoveryError, "not currently valid"):
                MODULE.validate_restore_plan(MODULE.finalize_restore_plan(core))

    def test_restore_requires_separate_bound_operator_approval(self):
        with tempfile.TemporaryDirectory() as temporary:
            plan = self.make_plan(temporary)
            now = datetime.now(timezone.utc)
            approval = {
                "schema": MODULE.RESTORE_APPROVAL_SCHEMA,
                "decision": "APPROVED", "separateOperatorApproval": True,
                "operator": "operator-2", "approvalId": "APPROVAL-1",
                "planSha256": plan["planSha256"], "profile": "S0",
                "mac": self.identity()["mac"],
                "imageSha256": plan["source"]["sha256"],
                "approvedAt": now.isoformat().replace("+00:00", "Z"),
                "expiresAt": (
                    now + timedelta(minutes=30)
                ).isoformat().replace("+00:00", "Z"),
            }
            path = Path(temporary) / "approval.json"
            self.write_json(path, approval)
            args = argparse.Namespace(
                confirm_rom_loader=True, confirm_port_open_may_reset=True,
                confirm_separate_approval=True,
                confirm_full_flash_overwrite=True,
                authorization_token=plan["authorizationToken"],
                approval_file=path,
            )
            self.assertEqual(MODULE.require_restore_authorization(args, plan), approval)
            approval["mac"] = "02:00:00:00:00:02"
            self.write_json(path, approval)
            with self.assertRaisesRegex(MODULE.RecoveryError, "not bound"):
                MODULE.require_restore_authorization(args, plan)

    def test_restore_write_is_blocked_by_default_and_by_wrong_checksum(self):
        plan = {"authorizationToken": "WRITE-FULL-FLASH-EXPECTED"}
        args = argparse.Namespace(
            confirm_rom_loader=False,
            confirm_port_open_may_reset=False,
            confirm_separate_approval=False,
            confirm_full_flash_overwrite=False,
            authorization_token="",
            approval_file=Path("/not/reached"),
        )
        with self.assertRaisesRegex(MODULE.RecoveryError, "confirm-rom-loader"):
            MODULE.require_restore_authorization(args, plan)
        args.confirm_rom_loader = True
        with self.assertRaisesRegex(MODULE.RecoveryError, "port-open-may-reset"):
            MODULE.require_restore_authorization(args, plan)
        args.confirm_port_open_may_reset = True
        with self.assertRaisesRegex(MODULE.RecoveryError, "confirm-separate-approval"):
            MODULE.require_restore_authorization(args, plan)
        args.confirm_separate_approval = True
        with self.assertRaisesRegex(MODULE.RecoveryError, "confirm-full-flash-overwrite"):
            MODULE.require_restore_authorization(args, plan)
        args.confirm_full_flash_overwrite = True
        with self.assertRaisesRegex(MODULE.RecoveryError, "checksum mismatch"):
            MODULE.require_restore_authorization(args, plan)

    def test_runtime_rejects_environment_override_and_unpinned_hash(self):
        with mock.patch.dict(os.environ, {"VOXONE_ESPTOOL": "/tmp/evil"}):
            with self.assertRaisesRegex(MODULE.RecoveryError, "rejects"):
                MODULE.resolve_esptool_runtime()
        manifest = MODULE.load_json(MODULE.DEFAULT_TOOLCHAIN_MANIFEST)
        profile_name, _ = MODULE.select_toolchain_profile(manifest)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "toolchain.json"
            changed = json.loads(json.dumps(manifest))
            changed["profiles"][profile_name]["esptool"]["sha256"] = "0" * 64
            self.write_json(path, changed)
            with self.assertRaisesRegex(MODULE.RecoveryError, "esptool SHA256"):
                MODULE.resolve_esptool_runtime(path)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "toolchain-tree.json"
            changed = json.loads(json.dumps(manifest))
            changed["profiles"][profile_name]["esptool"]["packageTreeSha256"] = "0" * 64
            self.write_json(path, changed)
            with self.assertRaisesRegex(MODULE.RecoveryError, "package tree"):
                MODULE.resolve_esptool_runtime(path)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "toolchain-dependency.json"
            changed = json.loads(json.dumps(manifest))
            changed["profiles"][profile_name]["dependencies"]["pyserial"][
                "treeSha256"
            ] = "0" * 64
            self.write_json(path, changed)
            with self.assertRaisesRegex(MODULE.RecoveryError, "dependency tree"):
                MODULE.resolve_esptool_runtime(path)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "toolchain-retry-guard.json"
            changed = json.loads(json.dumps(manifest))
            changed["writeRetryGuard"]["sha256"] = "0" * 64
            self.write_json(path, changed)
            with self.assertRaisesRegex(MODULE.RecoveryError, "retry guard SHA256"):
                MODULE.resolve_esptool_runtime(path)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "toolchain-retry-policy.json"
            changed = json.loads(json.dumps(manifest))
            changed["writeRetryGuard"]["maxWriteFlashAttempts"] = 2
            self.write_json(path, changed)
            with self.assertRaisesRegex(MODULE.RecoveryError, "single-attempt"):
                MODULE.resolve_esptool_runtime(path)

    def test_runtime_profile_must_match_host_architecture(self):
        with mock.patch.object(MODULE.platform, "machine", return_value="mips64"):
            with self.assertRaisesRegex(
                MODULE.RecoveryError, "no unique runtime profile|does not match"
            ):
                MODULE.resolve_esptool_runtime()

    def test_esptool_runtime_and_write_command_are_pinned_and_non_resetting(self):
        python_path, esptool_path, retry_guard_path = MODULE.resolve_esptool_runtime()
        manifest = MODULE.load_json(MODULE.DEFAULT_TOOLCHAIN_MANIFEST)
        _, profile = MODULE.select_toolchain_profile(manifest)
        self.assertEqual(MODULE.sha256_file(python_path), profile["python"]["sha256"])
        self.assertEqual(MODULE.sha256_file(esptool_path), profile["esptool"]["sha256"])
        self.assertEqual(
            MODULE.esptool_package_tree_sha256(esptool_path.parent),
            profile["esptool"]["packageTreeSha256"],
        )
        self.assertEqual(
            MODULE.sha256_file(retry_guard_path),
            manifest["writeRetryGuard"]["sha256"],
        )
        image = Path("/private/full-flash.bin")
        operation = MODULE.restore_write_operation(image)
        command = MODULE.esptool_command("S0", Path("/dev/test-s0"), operation)
        self.assertIn("no_reset", command)
        self.assertIn("--no-stub", command)
        self.assertIn(str(retry_guard_path), command)
        self.assertIn("--pinned-esptool-root", command)
        self.assertEqual(command[1:3], ["-E", "-s"])
        self.assertNotIn("erase_flash", command)
        self.assertNotIn("erase_region", command)
        self.assertEqual(
            operation, ["write_flash", "--flash_size", "keep", "0x0", str(image)]
        )

    def test_write_block_failure_has_one_attempt_and_unknown_flash_state(self):
        _, esptool_path, _ = MODULE.resolve_esptool_runtime()
        esptool = WRITE_ONCE.activate_write_once_policy(esptool_path.parent)
        loader = esptool.loader

        class FailedBlock:
            ESP_FLASH_DATA = 0x03
            calls = 0

            @staticmethod
            def checksum(_data):
                return 0

            def check_command(self, *_args, **_kwargs):
                self.calls += 1
                raise loader.FatalError("simulated block failure")

        target = FailedBlock()
        with self.assertRaisesRegex(loader.FatalError, "simulated block failure"):
            loader.ESPLoader.flash_block(target, b"block", 0)
        self.assertEqual(loader.WRITE_BLOCK_ATTEMPTS, 1)
        self.assertEqual(target.calls, 1)
        self.assertEqual(MODULE.restore_failure_status(True), "FLASH_STATE_UNKNOWN")

    def test_write_flash_connection_loss_has_one_attempt_and_unknown_flash_state(self):
        _, esptool_path, _ = MODULE.resolve_esptool_runtime()
        esptool = WRITE_ONCE.activate_write_once_policy(esptool_path.parent)
        from serial import SerialException

        class FakePort:
            def __init__(self):
                self.close_calls = 0
                self.open_calls = 0

            def close(self):
                self.close_calls += 1

            def open(self):
                self.open_calls += 1

        class FailedWrite:
            CHIP_NAME = "ESP32-S3"
            IMAGE_CHIP_ID = 9
            FLASH_SECTOR_SIZE = 0x1000
            FLASH_ENCRYPTED_WRITE_ALIGN = 16
            FLASH_WRITE_SIZE = 0x400
            WRITE_FLASH_ATTEMPTS = esptool.loader.ESPLoader.WRITE_FLASH_ATTEMPTS
            IS_STUB = False
            secure_download_mode = True

            def __init__(self):
                self.begin_calls = 0
                self.block_calls = 0
                self._port = FakePort()

            def flash_begin(self, *_args, **_kwargs):
                self.begin_calls += 1
                return 1

            def flash_block(self, *_args, **_kwargs):
                self.block_calls += 1
                raise SerialException("simulated connection loss")

        image = io.BytesIO(b"physical write attempt")
        image.name = "simulated.bin"
        args = argparse.Namespace(
            compress=False,
            no_compress=True,
            force=True,
            addr_filename=[(0, image)],
            encrypt=False,
            encrypt_files=None,
            ignore_flash_encryption_efuse_setting=False,
            flash_size="keep",
            erase_all=False,
            no_stub=True,
        )
        target = FailedWrite()
        with self.assertRaisesRegex(SerialException, "simulated connection loss"):
            esptool.cmds.write_flash(target, args)
        self.assertEqual(target.WRITE_FLASH_ATTEMPTS, 1)
        self.assertEqual(target.begin_calls, 1)
        self.assertEqual(target.block_calls, 1)
        self.assertEqual(target._port.close_calls, 0)
        self.assertEqual(target._port.open_calls, 0)
        self.assertEqual(MODULE.restore_failure_status(True), "FLASH_STATE_UNKNOWN")

    def test_partial_read_remains_private_on_error_and_interrupt(self):
        for raised in (MODULE.RecoveryError("mock failure"), KeyboardInterrupt()):
            with self.subTest(error=type(raised).__name__):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    root.chmod(0o700)
                    target = root / "partial.bin"

                    def fail(_profile, _port, operation, _logs, _phase):
                        output = Path(operation[-1])
                        output.write_bytes(b"partial")
                        output.chmod(0o644)
                        raise raised

                    with mock.patch.object(MODULE, "run_esptool", side_effect=fail):
                        with self.assertRaises(type(raised)):
                            MODULE.read_flash(
                                "S0", Path("/dev/null"), 0, 10,
                                target, root, "read",
                            )
                    self.assertEqual(stat.S_IMODE(target.stat().st_mode), 0o600)

    def test_esptool_port_open_requires_explicit_reset_risk_confirmation(self):
        MODULE.configure_runtime_selection(
            profile=os.environ.get("VOXONE_TEST_RUNTIME_PROFILE"),
            root=(
                Path(os.environ["VOXONE_TEST_RUNTIME_ROOT"])
                if os.environ.get("VOXONE_TEST_RUNTIME_ROOT")
                else None
            ),
            confirm_port_open_may_reset=False,
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            root.chmod(0o700)
            with mock.patch.object(
                MODULE, "esptool_command", return_value=["trusted"]
            ), mock.patch.object(MODULE.subprocess, "Popen") as popen:
                with self.assertRaisesRegex(MODULE.RecoveryError, "may assert DTR/RTS"):
                    MODULE.run_esptool(
                        "L0", Path("/dev/not-opened"), ["chip_id"], root, "blocked"
                    )
                popen.assert_not_called()

    def test_esptool_interrupt_preserves_private_log_and_terminates_child(self):
        class FakeProcess:
            returncode = None

            def __init__(self):
                self.calls = 0
                self.terminated = False

            def communicate(self, timeout=None):
                self.calls += 1
                if self.calls == 1:
                    raise KeyboardInterrupt()
                return "private mock output", None

            def terminate(self):
                self.terminated = True

            def kill(self):
                raise AssertionError("kill should not be needed")

        process = FakeProcess()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            root.chmod(0o700)
            with mock.patch.object(
                MODULE, "esptool_command", return_value=["trusted"]
            ), mock.patch.object(MODULE.subprocess, "Popen", return_value=process):
                with self.assertRaises(KeyboardInterrupt):
                    MODULE.run_esptool(
                        "S0", Path("/dev/null"), ["read_flash"], root, "interrupt"
                    )
            log = root / "esptool-interrupt.log"
            self.assertTrue(process.terminated)
            self.assertEqual(stat.S_IMODE(log.stat().st_mode), 0o600)
            self.assertIn("private mock output", log.read_text(encoding="utf-8"))

    def test_restore_error_after_write_start_has_unknown_flash_state(self):
        self.assertEqual(MODULE.restore_failure_status(False), "STOP")
        self.assertEqual(MODULE.restore_failure_status(True), "FLASH_STATE_UNKNOWN")

    def test_sensitive_paths_permissions_and_symlinks_are_fail_closed(self):
        with self.assertRaisesRegex(MODULE.RecoveryError, "outside repository"):
            MODULE.require_new_private_dir(ROOT / "must-not-exist")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            private = root / "private"
            private.write_bytes(b"private")
            private.chmod(0o600)
            link = root / "link"
            link.symlink_to(private)
            with self.assertRaisesRegex(MODULE.RecoveryError, "symlink"):
                MODULE.require_private_file(link)
            private.chmod(0o644)
            with self.assertRaisesRegex(MODULE.RecoveryError, "permissions"):
                MODULE.require_private_file(private)

    def test_ready_requires_terminal_valid_state(self):
        ready = {
            "ready": True, "bootId": "1234abcd",
            "ota": {
                "runningSlot": "app1", "bootSlot": "app1",
                "imageState": "valid", "pending": False,
                "rollbackAvailable": True, "firmwareUpdateReady": True,
            },
        }
        self.assertEqual(MODULE.validate_ready_document(ready)["bootId"], "1234abcd")
        ready["ota"]["pending"] = True
        with self.assertRaisesRegex(MODULE.RecoveryError, "terminal VALID"):
            MODULE.validate_ready_document(ready)

    def test_all_physical_read_commands_block_without_confirmation(self):
        parser = MODULE.parser()
        cases = (
            ("identify", ["--profile", "S0", "--port-role", "S0_NATIVE",
                          "--expected-mac", "02:00:00:00:00:01",
                          "--output", "/never"]),
            ("backup", ["--identity", "/never", "--output", "/never"]),
            ("verify", ["--backup-dir", "/never", "--offsite-output", "/never",
                        "--offsite-attestation", "/never"]),
            ("restore-preflight", ["--verified-manifest", "/never",
                                   "--output", "/never"]),
        )
        for command, arguments in cases:
            with self.subTest(command=command):
                args = parser.parse_args([command, *arguments])
                self.assertEqual(args.function(args), 2)
                args = parser.parse_args(
                    [command, *arguments, "--confirm-rom-loader"]
                )
                self.assertEqual(args.function(args), 2)


if __name__ == "__main__":
    unittest.main()
