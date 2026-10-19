#!/usr/bin/env python3

import importlib.util
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "m0_flash_backup", ROOT / "scripts" / "m0_flash_backup.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class M0FlashBackupTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        tool = MODULE.find_mkspiffs()
        if tool is None:
            raise RuntimeError("mkspiffs is required for offline inspect tests")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "empty"
            source.mkdir()
            image = root / "spiffs.bin"
            subprocess.run(
                [
                    str(tool), "-c", str(source), "-b", "4096", "-p", "256",
                    "-s", str(0x30000), str(image),
                ],
                check=True, capture_output=True,
            )
            cls.spiffs_image = image.read_bytes()

    @staticmethod
    def partition(label, part_type, subtype, offset, size):
        return struct.pack(
            "<HBBII16sI",
            0x50AA,
            part_type,
            subtype,
            offset,
            size,
            label.encode("ascii").ljust(16, b"\0"),
            0,
        )

    @staticmethod
    def ota_entry(sequence, state):
        return struct.pack(
            "<I20sII",
            sequence,
            b"\xff" * 20,
            state,
            MODULE.esp_ota_crc32(sequence),
        )

    @staticmethod
    def esp_image(size=0x1E0000):
        header = bytearray(
            b"\xe9\x01\x02\x2f\x00\x00\x00\x00"
            b"\xee\x00\x00\x00\x09\x00\x00\x00"
            b"\x00\xff\xff\x00\x00\x00\x00\x01"
        )
        payload = bytes(range(1, 34))
        data = header + struct.pack("<II", 0x3C110020, len(payload)) + payload
        checksum = MODULE.ESP_IMAGE_CHECKSUM_SEED
        for value in payload:
            checksum ^= value
        checksum_offset = len(data) | 0x0F
        data.extend(b"\0" * (checksum_offset - len(data)))
        data.append(checksum)
        data.extend(hashlib.sha256(data).digest())
        return bytes(data).ljust(size, b"\xff")

    def image(self, first_record=None, second_record=None, app1=None):
        image = bytearray(b"\xff" * 0x400000)
        entries = b"".join(
            [
                self.partition("nvs", 1, 2, 0x9000, 0x5000),
                self.partition("otadata", 1, 0, 0xE000, 0x2000),
                self.partition("app0", 0, 0x10, 0x10000, 0x1E0000),
                self.partition("app1", 0, 0x11, 0x1F0000, 0x1E0000),
                self.partition("spiffs", 1, 0x82, 0x3D0000, 0x30000),
            ]
        )
        table = (
            entries + struct.pack("<H", MODULE.PARTITION_MD5_MAGIC) +
            b"\xff" * 14 + hashlib.md5(entries).digest()
        )
        image[0x8000:0x8000 + len(table)] = table
        image[0xE000:0xE020] = (
            self.ota_entry(1, 2) if first_record is None else first_record
        )
        image[0xF000:0xF020] = (
            self.ota_entry(2, 2) if second_record is None else second_record
        )
        image[0x10000:0x1F0000] = self.esp_image()
        image[0x1F0000:0x3D0000] = self.esp_image() if app1 is None else app1
        image[0x3D0000:0x400000] = self.spiffs_image
        return image

    def inspect(self, image):
        with tempfile.TemporaryDirectory() as temporary:
            dump = Path(temporary) / "flash.bin"
            dump.write_bytes(image)
            return MODULE.inspect_image(dump)

    def test_crc_matches_known_esp_idf_algorithm(self):
        self.assertEqual(MODULE.esp_ota_crc32(1), 0x4743989A)
        self.assertEqual(MODULE.esp_ota_crc32(2), 0x55F63774)

    def test_esptool_connection_cannot_toggle_reset_or_load_stub(self):
        command = MODULE.esptool_base(Path("/dev/test-m0"))
        self.assertIn("no_reset", command)
        self.assertIn("--no-stub", command)
        self.assertNotIn("default_reset", command)
        self.assertNotIn("hard_reset", command)

    def test_inspection_selects_newest_slot_and_previous_valid(self):
        report = self.inspect(self.image())
        self.assertEqual(report["selectedBootSlot"], "app1")
        self.assertEqual(report["selectedRecordSector"], 1)
        self.assertEqual(report["conservativePreviousValidCandidates"], ["app0"])
        self.assertTrue(report["partitionTable"]["md5Valid"])
        self.assertTrue(all(app["imageValidation"]["checksumValid"] for app in report["apps"]))
        self.assertTrue(report["nvsIntegrity"]["valid"])
        self.assertTrue(report["spiffsIntegrity"]["valid"])
        self.assertTrue(report["rollbackAvailable"])
        self.assertEqual([app["status"] for app in report["apps"]], ["VALID", "VALID"])

    def test_valid_app0_and_empty_app1_are_reported_separately(self):
        report = self.inspect(
            self.image(
                first_record=self.ota_entry(1, 0xFFFFFFFF),
                second_record=self.ota_entry(0, 0xFFFFFFFF),
                app1=b"\xff" * 0x1E0000,
            )
        )
        self.assertEqual(report["selectedBootSlot"], "app0")
        self.assertEqual(report["otadataSummary"]["activeState"], "UNDEFINED")
        self.assertEqual(report["apps"][0]["status"], "VALID")
        self.assertEqual(report["apps"][1]["status"], "EMPTY")
        self.assertFalse(report["apps"][1]["bootable"])
        self.assertFalse(report["rollbackAvailable"])

    def test_corrupt_and_partially_written_app_are_not_empty(self):
        bad_header = bytearray(self.esp_image())
        bad_header[0] = 0
        report = self.inspect(self.image(app1=bad_header))
        self.assertEqual(report["apps"][1]["status"], "INVALID")
        self.assertIn("magic", report["apps"][1]["validationError"])

        partial = bytearray(b"\xff" * 0x1E0000)
        partial[0x100:0x120] = bytes(range(32))
        report = self.inspect(self.image(app1=partial))
        self.assertEqual(report["apps"][1]["status"], "PARTIAL")
        self.assertGreater(report["apps"][1]["programmedBytes"], 0)
        self.assertFalse(report["apps"][1]["bootable"])

        corrupt_hash = bytearray(self.esp_image())
        validation = MODULE.validate_esp_image(corrupt_hash)
        corrupt_hash[validation["logicalSize"] - 1] ^= 1
        report = MODULE.inspect_app_partition(corrupt_hash)
        self.assertEqual(report["status"], "INVALID")
        self.assertIn("hash mismatch", report["validationError"])

    def test_bad_crc_is_not_selectable(self):
        image = self.image()
        image[0xF01C:0xF020] = b"\0\0\0\0"
        report = self.inspect(image)
        self.assertEqual(report["selectedBootSlot"], "app0")
        self.assertEqual(report["conservativePreviousValidCandidates"], [])

    def test_otadata_states_and_pending_verify_next_boot_model(self):
        for code, name in MODULE.OTA_STATES.items():
            record = MODULE.ota_record(self.ota_entry(7, code), 0, 2)
            self.assertEqual(record["state"], name)
        report = self.inspect(
            self.image(
                first_record=self.ota_entry(7, 1),
                second_record=self.ota_entry(6, 2),
            )
        )
        self.assertEqual(
            report["otadata"][0]["nextBootStateWithoutValidation"], "ABORTED"
        )
        self.assertEqual(
            report["otadata"][1]["nextBootStateWithoutValidation"], "VALID"
        )
        self.assertEqual(
            report["otadataSummary"]["nextBootSlotWithoutValidation"], "app1"
        )
        new_report = self.inspect(
            self.image(
                first_record=self.ota_entry(9, 0),
                second_record=self.ota_entry(8, 2),
            )
        )
        self.assertEqual(
            new_report["otadata"][0]["nextBootStateWithoutValidation"],
            "PENDING_VERIFY",
        )
        self.assertEqual(
            new_report["otadataSummary"]["nextBootSlotWithoutValidation"], "app0"
        )
        aborted = MODULE.ota_record(self.ota_entry(7, 4), 0, 2)
        self.assertFalse(aborted["selectable"])

    def test_fully_erased_otadata_has_no_active_record(self):
        report = self.inspect(
            self.image(
                first_record=b"\xff" * 32,
                second_record=b"\xff" * 32,
            )
        )
        self.assertTrue(report["otadataSummary"]["bothSectorsErased"])
        self.assertTrue(report["otadataSummary"]["initialContents"])
        self.assertEqual(report["otadataSummary"]["initialBootSlot"], "app0")
        self.assertIsNone(report["selectedBootSlot"])
        self.assertFalse(report["rollbackAvailable"])

    def test_one_valid_record_wins_over_one_bad_crc(self):
        bad = bytearray(self.ota_entry(9, 2))
        bad[28:32] = b"\0" * 4
        report = self.inspect(
            self.image(first_record=self.ota_entry(5, 2), second_record=bad)
        )
        self.assertEqual(report["selectedRecordSector"], 0)
        self.assertEqual(report["selectedBootSlot"], "app0")

    def test_two_bad_crc_records_have_no_active_record(self):
        first = bytearray(self.ota_entry(5, 2))
        second = bytearray(self.ota_entry(6, 2))
        first[28:32] = second[28:32] = b"\0" * 4
        report = self.inspect(self.image(first_record=first, second_record=second))
        self.assertIsNone(report["selectedBootSlot"])
        self.assertEqual(report["otadataSummary"]["selectableRecordCount"], 0)

    def test_highest_sequence_selects_slot_using_idf_mapping(self):
        report = self.inspect(
            self.image(
                first_record=self.ota_entry(17, 2),
                second_record=self.ota_entry(18, 2),
            )
        )
        self.assertEqual(report["selectedRecordSector"], 1)
        self.assertEqual(report["selectedBootSlot"], "app1")

    def test_selected_record_pointing_to_empty_slot_is_inconsistent(self):
        report = self.inspect(
            self.image(
                first_record=self.ota_entry(1, 2),
                second_record=self.ota_entry(2, 2),
                app1=b"\xff" * 0x1E0000,
            )
        )
        self.assertEqual(report["selectedBootSlot"], "app1")
        self.assertFalse(report["otadataSummary"]["activeImageBootable"])
        self.assertFalse(report["otadataSummary"]["selectionConsistent"])
        self.assertFalse(report["rollbackAvailable"])

    def test_unknown_partition_layout_is_rejected_without_guessing(self):
        image = self.image()
        struct.pack_into("<I", image, 0x8000 + 8, 0x1D0000)
        entries_length = 5 * MODULE.PARTITION_ENTRY_SIZE
        trailer = 0x8000 + entries_length
        image[trailer + 16:trailer + 32] = hashlib.md5(
            image[0x8000:trailer]
        ).digest()
        with self.assertRaisesRegex(MODULE.HilError, "does not match VoxOne layout"):
            self.inspect(image)

    def test_firmware_validation_rejects_corrupt_checksum_and_hash(self):
        valid = bytearray(self.esp_image())
        report = MODULE.validate_esp_image(valid)
        self.assertTrue(report["checksumValid"])
        self.assertTrue(report["validationHashValid"])

        bad_checksum = bytearray(valid)
        checksum_offset = report["logicalSize"] - 33
        bad_checksum[checksum_offset] ^= 1
        with self.assertRaisesRegex(MODULE.HilError, "checksum mismatch"):
            MODULE.validate_esp_image(bad_checksum)

        bad_hash = bytearray(valid)
        bad_hash[report["logicalSize"] - 1] ^= 1
        with self.assertRaisesRegex(MODULE.HilError, "validation hash mismatch"):
            MODULE.validate_esp_image(bad_hash)

    def test_partition_table_rejects_bad_md5(self):
        image = self.image()
        image[0x8010] ^= 1
        with self.assertRaisesRegex(MODULE.HilError, "MD5 mismatch"):
            MODULE.validate_partition_table(image)

    def test_nvs_validation_is_aggregate_and_rejects_bad_page_crc(self):
        empty = b"\xff" * 0x5000
        report = MODULE.validate_nvs(empty)
        self.assertEqual(report["pageCount"], 5)
        self.assertNotIn("keys", report)
        self.assertNotIn("values", report)

        damaged = bytearray(empty)
        struct.pack_into("<I", damaged, 0, 0xFFFFFFFE)
        struct.pack_into("<I", damaged, 4, 7)
        damaged[8] = 0xFE
        damaged[9:28] = b"\xff" * 19
        struct.pack_into(
            "<I", damaged, 28,
            zlib.crc32(damaged[4:28], 0xFFFFFFFF) ^ 1,
        )
        with self.assertRaisesRegex(MODULE.HilError, "header CRC mismatch"):
            MODULE.validate_nvs(damaged)

    def test_spiffs_validation_never_returns_file_names(self):
        report = MODULE.validate_spiffs(self.spiffs_image)
        self.assertTrue(report["valid"])
        self.assertEqual(report["fileCount"], 0)
        self.assertNotIn("files", report)
        with self.assertRaisesRegex(MODULE.HilError, "SPIFFS integrity"):
            MODULE.validate_spiffs(b"\xff" * 0x30000)


if __name__ == "__main__":
    unittest.main()
