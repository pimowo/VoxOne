#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import tempfile
import termios
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "uart_rx_logger", ROOT / "scripts" / "uart_rx_logger.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class UartRxLoggerTest(unittest.TestCase):
    def test_rejects_unstable_device_path(self):
        with self.assertRaises(MODULE.LoggerError):
            MODULE.validate_stable_port("/dev/ttyUSB0")

    def test_open_is_read_only_and_clears_lines_around_configuration(self):
        calls = []

        def clear(fd):
            calls.append(("clear", fd))

        def configure(fd):
            calls.append(("configure", fd))

        with mock.patch.object(MODULE.os, "open", return_value=41) as opened, \
                mock.patch.object(MODULE, "clear_dtr_rts", side_effect=clear), \
                mock.patch.object(MODULE, "configure_uart", side_effect=configure):
            self.assertEqual(MODULE.open_uart_rx_only(Path("/stable")), 41)
        opened.assert_called_once_with(
            Path("/stable"),
            MODULE.os.O_RDONLY | MODULE.os.O_NOCTTY | MODULE.os.O_NONBLOCK,
        )
        self.assertEqual(calls, [("clear", 41), ("configure", 41), ("clear", 41)])

    def test_open_failure_closes_descriptor(self):
        with mock.patch.object(MODULE.os, "open", return_value=42), \
                mock.patch.object(
                    MODULE, "clear_dtr_rts", side_effect=MODULE.LoggerError("no")
                ), mock.patch.object(MODULE.os, "close") as closed:
            with self.assertRaises(MODULE.LoggerError):
                MODULE.open_uart_rx_only(Path("/stable"))
        closed.assert_called_once_with(42)

    def test_uart_configuration_is_115200_8n1_without_flow_or_hupcl(self):
        attributes = [1, 2, 0xFFFFFFFF, 3, termios.B9600, termios.B9600, [0] * 32]
        captured = []
        with mock.patch.object(MODULE.termios, "tcgetattr", return_value=attributes), \
                mock.patch.object(
                    MODULE.termios,
                    "tcsetattr",
                    side_effect=lambda fd, when, value: captured.append((fd, when, value)),
                ):
            MODULE.configure_uart(7)
        self.assertEqual(len(captured), 1)
        fd, when, result = captured[0]
        self.assertEqual((fd, when), (7, termios.TCSANOW))
        self.assertEqual(result[0], 0)
        self.assertEqual(result[1], 0)
        self.assertEqual(result[3], 0)
        self.assertEqual(result[4:6], [termios.B115200, termios.B115200])
        self.assertTrue(result[2] & termios.CS8)
        self.assertTrue(result[2] & termios.CREAD)
        self.assertTrue(result[2] & termios.CLOCAL)
        self.assertFalse(result[2] & termios.PARENB)
        self.assertFalse(result[2] & termios.CSTOPB)
        self.assertFalse(result[2] & termios.HUPCL)
        if hasattr(termios, "CRTSCTS"):
            self.assertFalse(result[2] & termios.CRTSCTS)

    def test_chunk_log_is_timestamped_and_lossless_hex(self):
        class Sink:
            def __init__(self):
                self.value = ""
                self.flushed = False

            def write(self, value):
                self.value += value

            def flush(self):
                self.flushed = True

        sink = Sink()
        with mock.patch.object(MODULE, "timestamp", return_value="2026-10-19T20:00:00+02:00"):
            MODULE.log_chunk(sink, b"\x00A\xff")
        self.assertIn('"at":"2026-10-19T20:00:00+02:00"', sink.value)
        self.assertIn('"bytes":3', sink.value)
        self.assertIn('"hex":"0041ff"', sink.value)
        self.assertTrue(sink.flushed)

    def test_source_has_no_serial_write_or_automatic_reopen(self):
        source = (ROOT / "scripts" / "uart_rx_logger.py").read_text(encoding="utf-8")
        self.assertNotIn("os.write(", source)
        self.assertNotIn("serial.write(", source)
        self.assertEqual(source.count("os.open("), 1)
        self.assertIn("DTR/RTS are cleared only after open()", source)


if __name__ == "__main__":
    unittest.main()
