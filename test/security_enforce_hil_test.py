#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "security_enforce_hil", ROOT / "scripts" / "security_enforce_hil.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class SecurityEnforceHilTest(unittest.TestCase):
    def test_uart_states_are_distinct(self):
        evidence = MODULE.UartEvidence()
        self.assertEqual(evidence.state, MODULE.UartState.NO_BYTES)

        evidence.feed(b"ordinary runtime line\n")
        self.assertEqual(evidence.state, MODULE.UartState.DATA_NO_BOOT)
        self.assertEqual(evidence.byte_count, 22)
        self.assertEqual(evidence.line_count, 1)

        evidence.feed(b"##[BO")
        evidence.feed(b"OT]# setup\n")
        self.assertEqual(evidence.state, MODULE.UartState.BOOT_EVIDENCE)

        evidence.feed(b"")
        self.assertEqual(evidence.state, MODULE.UartState.DISCONNECTED)

    def test_new_boot_id_alone_cannot_pass(self):
        with self.assertRaisesRegex(MODULE.HilFailure, "produced no bytes"):
            MODULE.require_reboot_evidence("new-boot", MODULE.UartEvidence())

        data_only = MODULE.UartEvidence()
        data_only.feed(b"ordinary runtime line\n")
        with self.assertRaisesRegex(MODULE.HilFailure, "no recognized boot evidence"):
            MODULE.require_reboot_evidence("new-boot", data_only)

    def test_boot_uart_alone_cannot_pass_without_new_boot_id(self):
        evidence = MODULE.UartEvidence()
        evidence.feed(b"rst:0x1 boot:0x8\n")
        with self.assertRaisesRegex(MODULE.HilFailure, "new bootId"):
            MODULE.require_reboot_evidence(None, evidence)

    def test_boot_id_and_uart_evidence_pass_together(self):
        evidence = MODULE.UartEvidence()
        evidence.feed(b"ESP32 ROM boot:0x8\n")
        MODULE.require_reboot_evidence("new-boot", evidence)

    def test_disconnected_uart_cannot_pass(self):
        evidence = MODULE.UartEvidence()
        evidence.feed(b"rst:0x1\n")
        evidence.feed(b"")
        with self.assertRaisesRegex(MODULE.HilFailure, "disconnected"):
            MODULE.require_reboot_evidence("new-boot", evidence)

    def test_uart_redaction_does_not_expose_sensitive_value(self):
        line = "token=top-secret boot:0x8"
        safe = MODULE.SENSITIVE_UART.sub(r"\1\2<redacted>", line)
        self.assertNotIn("top-secret", safe)
        self.assertIn("token=<redacted>", safe)


if __name__ == "__main__":
    unittest.main()
