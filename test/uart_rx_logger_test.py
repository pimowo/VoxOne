#!/usr/bin/env python3

import importlib.util
import io
import json
import os
from pathlib import Path
import pty
import tempfile
import termios
from types import SimpleNamespace
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
    def ledger(self, raw_limit=100000, formatted_limit=1000000):
        raw = io.BytesIO()
        formatted = io.StringIO()
        ledger = MODULE.CaptureLedger(
            raw, formatted, 0.0, raw_limit, formatted_limit
        )
        return ledger, raw, formatted

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
                mock.patch.object(MODULE, "configure_uart", side_effect=configure), \
                mock.patch.object(MODULE, "verify_uart_configuration") as verified:
            self.assertEqual(MODULE.open_uart_rx_only(Path("/stable")), 41)
        opened.assert_called_once_with(
            Path("/stable"),
            MODULE.os.O_RDONLY | MODULE.os.O_NOCTTY | MODULE.os.O_NONBLOCK,
        )
        self.assertEqual(calls, [("clear", 41), ("configure", 41), ("clear", 41)])
        verified.assert_called_once_with(41)

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

    def test_uart_configuration_readback_must_match(self):
        cflag = termios.CS8 | termios.CREAD | termios.CLOCAL
        valid = [0, 0, cflag, 0, termios.B115200, termios.B115200, [0] * 32]
        with mock.patch.object(MODULE.termios, "tcgetattr", return_value=valid):
            MODULE.verify_uart_configuration(7)
        invalid = list(valid)
        invalid[4] = termios.B9600
        with mock.patch.object(MODULE.termios, "tcgetattr", return_value=invalid):
            with self.assertRaises(MODULE.LoggerError):
                MODULE.verify_uart_configuration(7)

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
            written = MODULE.log_chunk(sink, b"\x00A\xff")
        self.assertIn('"at":"2026-10-19T20:00:00+02:00"', sink.value)
        self.assertIn('"bytes":3', sink.value)
        self.assertIn('"hex":"0041ff"', sink.value)
        self.assertTrue(sink.flushed)
        self.assertEqual(written, len(sink.value.encode("utf-8")))

    def test_single_read_is_written_once_to_each_output(self):
        ledger, raw, formatted = self.ledger()
        self.assertTrue(ledger.record_read(b"abc", 1.0, "t1"))
        self.assertEqual(raw.getvalue(), b"abc")
        records = [json.loads(line) for line in formatted.getvalue().splitlines()]
        self.assertEqual(records, [{"at": "t1", "bytes": 3, "hex": "616263"}])
        self.assertEqual(ledger.read_calls, 1)
        self.assertEqual(ledger.raw_write_calls, 1)
        self.assertEqual(ledger.formatted_write_calls, 1)

    def test_multiple_and_partial_reads_remain_ordered_and_lossless(self):
        ledger, raw, formatted = self.ledger()
        chunks = [b"first", b"x", b"partial"]
        for index, chunk in enumerate(chunks, 1):
            self.assertTrue(ledger.record_read(chunk, float(index), f"t{index}"))
        self.assertEqual(raw.getvalue(), b"".join(chunks))
        decoded = b"".join(
            bytes.fromhex(json.loads(line)["hex"])
            for line in formatted.getvalue().splitlines()
        )
        self.assertEqual(decoded, raw.getvalue())
        self.assertEqual(ledger.read_bytes, len(raw.getvalue()))
        self.assertEqual(ledger.raw_write_calls, len(chunks))

    def test_bytes_bytearray_and_memoryview_are_serialized_by_length(self):
        ledger, raw, formatted = self.ledger()
        chunks = [b"a", bytearray(b"bc"), memoryview(b"def")]
        for index, chunk in enumerate(chunks, 1):
            self.assertTrue(ledger.record_read(chunk, float(index), f"t{index}"))
        self.assertEqual(raw.getvalue(), b"abcdef")
        records = [json.loads(line) for line in formatted.getvalue().splitlines()]
        self.assertEqual([record["bytes"] for record in records], [1, 2, 3])

    def test_empty_read_is_eof_and_invalid_without_output(self):
        ledger, raw, formatted = self.ledger()
        self.assertFalse(ledger.record_read(b"", 1.0, "t"))
        self.assertEqual(ledger.invalid_reason, "uart_eof")
        self.assertEqual(ledger.empty_reads, 1)
        self.assertEqual(raw.getvalue(), b"")
        self.assertEqual(formatted.getvalue(), "")

    def test_would_block_is_counted_without_output(self):
        ledger, raw, formatted = self.ledger()
        ledger.record_would_block()
        self.assertEqual(ledger.read_calls, 1)
        self.assertEqual(ledger.would_block_reads, 1)
        self.assertEqual(raw.getvalue(), b"")
        self.assertEqual(formatted.getvalue(), "")

    def test_repeated_data_is_valid_when_rate_is_physically_possible(self):
        ledger, raw, formatted = self.ledger()
        chunk = b"same\n"
        for index in range(100):
            self.assertTrue(
                ledger.record_read(chunk, 1.0 + index * 0.01, f"t{index}")
            )
        self.assertIsNone(ledger.invalid_reason)
        self.assertEqual(raw.getvalue(), chunk * 100)
        self.assertEqual(len(formatted.getvalue().splitlines()), 100)

    def test_observed_replay_is_stopped_as_impossible_throughput(self):
        ledger, raw, formatted = self.ledger(
            raw_limit=MODULE.session_file_limits(60)[0],
            formatted_limit=MODULE.session_file_limits(60)[1],
        )
        chunk = b"m=2144 audioBuffer=0 playerRunning=0 wifi=1 rssi=-61\n"
        accepted = 0
        for index in range(10000):
            if not ledger.record_read(chunk, 0.050, f"t{index}"):
                break
            accepted += 1
        self.assertEqual(ledger.invalid_reason, "physical_throughput_exceeded")
        self.assertLess(ledger.raw_bytes, 25000)
        self.assertEqual(ledger.raw_bytes, len(raw.getvalue()))
        self.assertEqual(ledger.raw_write_calls, accepted + 1)
        self.assertEqual(ledger.formatted_write_calls, ledger.raw_write_calls)

    def test_raw_limit_fails_closed_without_partial_record(self):
        ledger, raw, formatted = self.ledger(raw_limit=4)
        self.assertTrue(ledger.record_read(b"1234", 1.0, "t1"))
        self.assertFalse(ledger.record_read(b"5", 1.1, "t2"))
        self.assertEqual(ledger.invalid_reason, "raw_file_limit_exceeded")
        self.assertEqual(ledger.read_bytes, 5)
        self.assertEqual(ledger.raw_bytes, 4)
        self.assertEqual(ledger.unwritten_read_bytes, 1)
        self.assertEqual(raw.getvalue(), b"1234")
        self.assertEqual(len(formatted.getvalue().splitlines()), 1)

    def test_formatted_limit_fails_closed_without_raw_divergence(self):
        first = MODULE.serialize_chunk(b"a", "t1")
        ledger, raw, formatted = self.ledger(formatted_limit=len(first.encode()))
        self.assertTrue(ledger.record_read(b"a", 1.0, "t1"))
        self.assertFalse(ledger.record_read(b"b", 1.1, "t2"))
        self.assertEqual(ledger.invalid_reason, "formatted_file_limit_exceeded")
        self.assertEqual(raw.getvalue(), b"a")
        self.assertEqual(len(formatted.getvalue().splitlines()), 1)

    def test_physical_budget_accounts_for_delay_and_long_session(self):
        initial = MODULE.physical_byte_budget(0)
        delayed = MODULE.physical_byte_budget(10)
        four_hours, formatted = MODULE.session_file_limits(4 * 60 * 60)
        self.assertGreater(delayed, initial)
        self.assertGreater(four_hours, delayed)
        self.assertGreater(formatted, four_hours * 2)
        self.assertLess(initial, 20000)

    def test_long_session_keeps_read_and_output_accounting_consistent(self):
        raw_limit, formatted_limit = MODULE.session_file_limits(4 * 60 * 60)
        ledger, raw, formatted = self.ledger(raw_limit, formatted_limit)
        chunk = b"periodic diagnostic\n"
        for index in range(10000):
            elapsed = index * (4 * 60 * 60 / 10000)
            self.assertTrue(ledger.record_read(chunk, elapsed, f"t{index}"))
        self.assertIsNone(ledger.invalid_reason)
        self.assertEqual(ledger.read_bytes, len(chunk) * 10000)
        self.assertEqual(ledger.raw_bytes, ledger.read_bytes)
        self.assertEqual(ledger.raw_write_calls, 10000)
        self.assertEqual(ledger.formatted_write_calls, 10000)
        self.assertEqual(len(raw.getvalue()), ledger.raw_bytes)
        self.assertEqual(len(formatted.getvalue().splitlines()), 10000)

    def test_private_files_and_directory_have_restrictive_modes(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "evidence"
            output.mkdir(mode=0o700)
            output.chmod(0o700)
            path = output / "manifest.json"
            MODULE.write_private_json(path, {"status": "PASS"})
            self.assertEqual(output.stat().st_mode & 0o777, 0o700)
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def test_pseudoterminal_read_is_not_duplicated(self):
        master, slave = pty.openpty()
        try:
            MODULE.configure_uart(slave)
            payload = b"one UART payload\n"
            os.write(master, payload)
            readable, _, _ = MODULE.select.select([slave], [], [], 1.0)
            self.assertEqual(readable, [slave])
            chunk = os.read(slave, 4096)
            self.assertEqual(chunk, payload)
            ledger, raw, formatted = self.ledger()
            self.assertTrue(ledger.record_read(chunk, 1.0, "pty"))
            self.assertEqual(raw.getvalue(), payload)
            self.assertEqual(len(formatted.getvalue().splitlines()), 1)
        finally:
            os.close(slave)
            os.close(master)

    def test_run_closes_pseudoterminal_and_writes_pass_manifest(self):
        master, slave = pty.openpty()
        MODULE.configure_uart(slave)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "capture"
            args = SimpleNamespace(
                port="/dev/serial/by-id/mock-uart",
                output_dir=str(output),
                host="offline.invalid",
                duration=0.02,
                http_interval=10.0,
                http_timeout=0.01,
                acknowledge_open_line_risk=True,
            )
            os.write(master, b"bounded payload\n")
            with mock.patch.object(
                MODULE,
                "validate_stable_port",
                return_value=(Path(args.port), Path("/dev/pts/mock")),
            ), mock.patch.object(
                MODULE, "open_uart_rx_only", return_value=slave
            ), mock.patch.object(
                MODULE,
                "read_ready",
                return_value={"http": None, "ready": False, "bootId": None},
            ):
                self.assertEqual(MODULE.run(args), 0)
            with self.assertRaises(OSError):
                os.fstat(slave)
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual(manifest["status"], "PASS")
            self.assertEqual(manifest["capture"]["readBytes"], 16)
            self.assertEqual(manifest["capture"]["rawBytes"], 16)
            self.assertEqual(manifest["capture"]["rawWriteCalls"], 1)
            self.assertEqual((output / "uart.raw").read_bytes(), b"bounded payload\n")
            self.assertEqual((output / "uart.raw").stat().st_mode & 0o777, 0o600)
        os.close(master)

    def test_source_has_no_serial_write_or_automatic_reopen(self):
        source = (ROOT / "scripts" / "uart_rx_logger.py").read_text(encoding="utf-8")
        self.assertNotIn("os.write(", source)
        self.assertNotIn("serial.write(", source)
        self.assertEqual(
            source.count("fd = os.open(path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)"),
            1,
        )
        self.assertNotIn("while True:\n        fd = open_uart_rx_only", source)
        self.assertIn("DTR/RTS are cleared only after open()", source)


if __name__ == "__main__":
    unittest.main()
