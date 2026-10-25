#!/usr/bin/env python3

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "hil_credentials", ROOT / "scripts" / "hil_credentials.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class HilCredentialsTest(unittest.TestCase):
    def make_store(self, root: Path, *, password: str = "test-only") -> Path:
        directory = root / "voxone-hil"
        directory.mkdir(mode=0o700)
        path = directory / "test-credentials.json"
        path.write_text(json.dumps({
            "version": 1,
            "voxone": {
                "W0": {"www_admin_password": ""},
                "M0": {"www_admin_password": password},
                "L0": {"www_admin_password": ""},
            },
            "mqtt": {
                "host": "mqtt.test",
                "port": 1883,
                "username": "tester",
                "password": "mqtt-test-only",
            },
        }), encoding="utf-8")
        path.chmod(0o600)
        return path

    def test_loads_www_and_mqtt_values_without_printing(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = self.make_store(Path(temporary))
            self.assertEqual(MODULE.www_admin_password("m0", path), "test-only")
            mqtt = MODULE.mqtt_credentials(path)
            self.assertEqual((mqtt.host, mqtt.port), ("mqtt.test", 1883))
            self.assertNotIn("mqtt-test-only", repr(mqtt))
            self.assertNotIn("tester", repr(mqtt))

    def test_empty_password_reports_auth_required(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = self.make_store(Path(temporary), password="")
            with self.assertRaisesRegex(MODULE.AuthRequired, "AUTH_REQUIRED"):
                MODULE.www_admin_password("M0", path)

    def test_rejects_weak_file_mode(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = self.make_store(Path(temporary))
            path.chmod(0o640)
            with self.assertRaisesRegex(MODULE.CredentialsError, "0600"):
                MODULE.www_admin_password("M0", path)

    def test_rejects_weak_directory_mode(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = self.make_store(Path(temporary))
            path.parent.chmod(0o750)
            with self.assertRaisesRegex(MODULE.CredentialsError, "0700"):
                MODULE.www_admin_password("M0", path)

    def test_rejects_file_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            real = self.make_store(root)
            link = real.parent / "linked.json"
            link.symlink_to(real)
            with self.assertRaisesRegex(MODULE.CredentialsError, "safely"):
                MODULE.www_admin_password("M0", link)


if __name__ == "__main__":
    unittest.main()
