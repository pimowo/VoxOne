#!/usr/bin/env python3

import argparse
import importlib.util
import io
import json
from pathlib import Path
import socket
import sys
import unittest
from contextlib import redirect_stdout
from unittest import mock
from urllib.parse import parse_qs


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location(
    "security_enforce_hil", ROOT / "scripts" / "security_enforce_hil.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class SecurityEnforceHilTest(unittest.TestCase):
    def test_websocket_graceful_close_sends_masked_1000_and_waits_for_peer(self):
        class ClosingSocket:
            def __init__(self):
                self.sent = b""
                self.response = bytearray(b"\x88\x02\x03\xe8")
                self.shutdown_how = None
                self.closed = False

            def sendall(self, payload):
                self.sent += payload

            def recv(self, size):
                result = bytes(self.response[:size])
                del self.response[:size]
                return result

            def settimeout(self, timeout):
                self.timeout = timeout

            def shutdown(self, how):
                self.shutdown_how = how

            def close(self):
                self.closed = True

        sock = ClosingSocket()
        websocket = MODULE.WebSocket(sock)
        self.assertEqual(websocket.close_gracefully(1.0), 1000)

        self.assertEqual(sock.sent[0] & 0x0f, 0x8)
        self.assertTrue(sock.sent[1] & 0x80)
        size = sock.sent[1] & 0x7f
        self.assertEqual(size, 2)
        mask = sock.sent[2:6]
        payload = bytes(
            value ^ mask[index % 4]
            for index, value in enumerate(sock.sent[6:6 + size])
        )
        self.assertEqual(payload, b"\x03\xe8")
        self.assertEqual(sock.shutdown_how, socket.SHUT_WR)
        self.assertTrue(sock.closed)

    def test_http_transport_reset_reports_safe_phase_without_request_data(self):
        class ResetConnection:
            instances = []

            def __init__(self, *args, **kwargs):
                self.closed = False
                self.instances.append(self)

            def putrequest(self, *args, **kwargs):
                pass

            def putheader(self, *args, **kwargs):
                pass

            def endheaders(self, body):
                self.body = body

            def getresponse(self):
                raise ConnectionResetError(104, "secret must not be logged")

            def close(self):
                self.closed = True

        device = MODULE.Device("192.0.2.1", 80, 5.0)
        secret = b"password=do-not-log"
        with mock.patch.object(
            MODULE.http.client, "HTTPConnection", ResetConnection
        ), self.assertRaises(MODULE.HttpTransportFailure) as raised:
            device.http("POST", "/api/security/login", body=secret)

        diagnostic = raised.exception.diagnostic
        rendered = str(raised.exception)
        self.assertEqual(diagnostic.phase, "await_response_headers")
        self.assertIsNone(diagnostic.response_status)
        self.assertEqual(diagnostic.exception_type, "ConnectionResetError")
        self.assertEqual(diagnostic.errno, 104)
        self.assertIn("responseStatus=none", rendered)
        self.assertNotIn("do-not-log", rendered)
        self.assertNotIn("secret", rendered)
        self.assertTrue(ResetConnection.instances[0].closed)

    def test_http_transport_incomplete_body_preserves_status_and_counts_only(self):
        class IncompleteResponse:
            status = 200

            def read(self, _limit):
                raise MODULE.http.client.IncompleteRead(b"private", 11)

            def getheaders(self):
                return [("Set-Cookie", "do-not-log")]

        class IncompleteConnection:
            instance = None

            def __init__(self, *args, **kwargs):
                self.closed = False
                IncompleteConnection.instance = self

            def putrequest(self, *args, **kwargs):
                pass

            def putheader(self, *args, **kwargs):
                pass

            def endheaders(self, body):
                pass

            def getresponse(self):
                return IncompleteResponse()

            def close(self):
                self.closed = True

        device = MODULE.Device("192.0.2.1", 80, 5.0)
        with mock.patch.object(
            MODULE.http.client, "HTTPConnection", IncompleteConnection
        ), self.assertRaises(MODULE.HttpTransportFailure) as raised:
            device.http("POST", "/api/security/login", body=b"private")

        diagnostic = raised.exception.diagnostic
        rendered = str(raised.exception)
        self.assertEqual(diagnostic.phase, "read_response_body")
        self.assertEqual(diagnostic.response_status, 200)
        self.assertEqual(diagnostic.partial_body_bytes, 7)
        self.assertEqual(diagnostic.expected_more_bytes, 11)
        self.assertNotIn("private", rendered)
        self.assertNotIn("do-not-log", rendered)
        self.assertTrue(IncompleteConnection.instance.closed)

    def test_login_diagnostic_reports_only_safe_fields(self):
        response = MODULE.HttpResult(
            401,
            [("Set-Cookie", "voxone_session=do-not-log"),
             ("X-Private", "also-do-not-log")],
            b'{"error":"authentication_failed","detail":"do-not-log"}',
        )
        output = io.StringIO()
        with redirect_stdout(output), self.assertRaisesRegex(
            MODULE.AuthRequired,
            r"AUTH_REQUIRED",
        ):
            MODULE.require_login_success(response)

        logged = output.getvalue()
        self.assertIn("loginHttpStatus=401", logged)
        self.assertIn("loginError=authentication_failed", logged)
        self.assertNotIn("do-not-log", logged)
        self.assertNotIn("also-do-not-log", logged)

    def test_login_diagnostic_sanitizes_unknown_error_and_retry_header(self):
        response = MODULE.HttpResult(
            429,
            [("Retry-After", "60"), ("Retry-After", "secret")],
            b'{"error":"secret-error-code","password":"secret-value"}',
        )
        diagnostic = MODULE.login_http_diagnostic(response)

        self.assertEqual(diagnostic.status, 429)
        self.assertEqual(diagnostic.error, "unrecognized")
        self.assertIsNone(diagnostic.retry_after_seconds)
        self.assertNotIn("secret", repr(diagnostic))

    def test_login_diagnostic_accepts_bounded_numeric_retry_after(self):
        response = MODULE.HttpResult(
            429,
            [("Retry-After", "60")],
            b'{"error":"rate_limited"}',
        )
        diagnostic = MODULE.login_http_diagnostic(response)

        self.assertEqual(diagnostic.error, "rate_limited")
        self.assertEqual(diagnostic.retry_after_seconds, 60)

    def test_login_diagnostic_records_success_without_cookie(self):
        response = MODULE.HttpResult(
            200,
            [("Set-Cookie", "voxone_session=do-not-log")],
            b'{"ok":true}',
        )
        output = io.StringIO()
        with redirect_stdout(output):
            MODULE.require_login_success(response)

        logged = output.getvalue()
        self.assertIn("loginHttpStatus=200", logged)
        self.assertIn("loginError=none", logged)
        self.assertNotIn("do-not-log", logged)

    def test_login_form_preserves_unicode_and_special_characters(self):
        password = " zażółć&=+% "
        body = MODULE.login_form_body(password)

        self.assertEqual(parse_qs(body.decode("ascii")), {"password": [password]})
        self.assertNotIn(password.encode("utf-8"), body)

    def test_login_accepts_http_200_and_sends_expected_form(self):
        class RecordingDevice:
            origin = "http://192.168.1.114"

            def http(self, method, path, *, body, headers, limit):
                self.request = (method, path, body, headers, limit)
                return MODULE.HttpResult(
                    200,
                    [("Set-Cookie",
                      "voxone_session=0123456789abcdef0123456789abcdef; "
                      "Path=/; HttpOnly; SameSite=Strict; Max-Age=86400")],
                    b'{"ok":true}',
                )

        device = RecordingDevice()
        output = io.StringIO()
        with redirect_stdout(output):
            cookie = MODULE.login(device, " zażółć&=+% ")

        method, path, body, headers, limit = device.request
        self.assertEqual((method, path), ("POST", "/api/security/login"))
        self.assertEqual(
            parse_qs(body.decode("ascii")),
            {"password": [" zażółć&=+% "]},
        )
        self.assertEqual(headers["Origin"], device.origin)
        self.assertEqual(
            headers["Content-Type"], "application/x-www-form-urlencoded"
        )
        self.assertEqual(limit, 4096)
        self.assertEqual(
            cookie, "voxone_session=0123456789abcdef0123456789abcdef"
        )
        self.assertIn("loginHttpStatus=200", output.getvalue())
        self.assertNotIn("0123456789abcdef", output.getvalue())

    def test_fifo_line_ending_removes_exactly_one_terminator(self):
        self.assertEqual(MODULE.remove_input_line_ending("hasło\n"), "hasło")
        self.assertEqual(MODULE.remove_input_line_ending("hasło\r\n"), "hasło")
        self.assertEqual(MODULE.remove_input_line_ending(" hasło "), " hasło ")
        self.assertEqual(MODULE.remove_input_line_ending("hasło\n\n"), "hasło\n")

    def test_default_password_source_is_local_store_not_environment(self):
        args = argparse.Namespace(
            password_stdin=False,
            credentials_profile="L0",
        )
        with mock.patch.object(
            MODULE, "www_admin_password", return_value="in-memory-only"
        ) as loader, mock.patch.dict(
            MODULE.os.environ,
            {"VOXONE_HIL_PASSWORD": "must-not-be-used"},
        ):
            password = MODULE.password_from_args(args)

        self.assertEqual(password, "in-memory-only")
        loader.assert_called_once_with("L0")

    def test_getindex_snapshot_accepts_current_multiframe_contract(self):
        frames = [
            {"payload": [{"id": "playerwrap", "value": "stopped"}]},
            {"payload": [{"id": "rssi", "value": -55}]},
            {"payload": [
                {"id": "volume", "value": 80},
                {"id": "volume100", "value": 31},
                {"id": "muted", "value": 0},
                {"id": "maximumVolume", "value": 90},
                {"id": "startupMode", "value": 0},
                {"id": "startupFixedVolume", "value": 25},
            ]},
            {"payload": [{"id": "meta", "value": "Artist - Title"}]},
            {"current": 12},
            {"payload": [
                {"id": "bass", "value": 0},
                {"id": "middle", "value": 0},
                {"id": "trebble", "value": 0},
            ]},
            {"webStatus": {"source": "WEB"}},
            {"payload": [{"id": "balance", "value": 0}]},
            {"payload": [
                {"id": "bitrate", "value": 128},
                {"id": "fmt", "value": "MP3"},
            ]},
            {"payload": [{"id": "nameset", "value": "Radio"}]},
        ]
        evidence = MODULE.GetIndexSnapshotEvidence()
        for frame in frames:
            evidence.feed(json.dumps(frame).encode())

        self.assertTrue(evidence.complete)
        self.assertEqual(evidence.missing_top_level, frozenset())
        self.assertEqual(evidence.missing_payload_ids, frozenset())
        self.assertEqual(evidence.text_frames, len(frames))

    def test_getindex_nameset_and_meta_are_payload_ids_not_top_level(self):
        evidence = MODULE.GetIndexSnapshotEvidence()
        evidence.feed(b'{"nameset":"wrong","meta":"wrong","current":1}')

        self.assertIn("nameset", evidence.missing_payload_ids)
        self.assertIn("meta", evidence.missing_payload_ids)
        self.assertNotIn("current", evidence.missing_top_level)

    def test_getindex_missing_evidence_is_field_only(self):
        evidence = MODULE.GetIndexSnapshotEvidence()
        evidence.feed(b'{"payload":[{"id":"nameset","value":"private station"}]}')

        self.assertIn("current", evidence.missing_top_level)
        self.assertIn("playerwrap", evidence.missing_payload_ids)
        self.assertNotIn("private station", repr(evidence))

    def test_getindex_wait_uses_deadline_and_reports_missing_fields(self):
        class TimeoutWebSocket:
            def __init__(self):
                self.deadlines = []

            def receive(self, deadline=None):
                self.deadlines.append(deadline)
                raise socket.timeout()

        websocket = TimeoutWebSocket()
        with self.assertRaisesRegex(
            MODULE.HilFailure,
            r"textFrames=0.*missingTopLevel=current.*missingPayloadIds=",
        ):
            MODULE.wait_getindex_snapshot(websocket, 0.05)
        self.assertEqual(len(websocket.deadlines), 1)
        self.assertIsNotNone(websocket.deadlines[0])

    def test_getindex_wait_accepts_independent_frames_in_any_order(self):
        frames = [
            {"payload": [{"id": "meta", "value": "metadata"}]},
            {"payload": [
                {"id": "bass", "value": 0},
                {"id": "middle", "value": 0},
                {"id": "trebble", "value": 0},
                {"id": "balance", "value": 0},
            ]},
            {"payload": [{"id": "rssi", "value": -50}]},
            {"payload": [
                {"id": "volume", "value": 80},
                {"id": "volume100", "value": 31},
                {"id": "muted", "value": 0},
                {"id": "maximumVolume", "value": 90},
                {"id": "startupMode", "value": 0},
                {"id": "startupFixedVolume", "value": 25},
            ]},
            {"current": 12},
            {"payload": [{"id": "playerwrap", "value": "playing"}]},
            {"payload": [
                {"id": "bitrate", "value": 128},
                {"id": "fmt", "value": "MP3"},
            ]},
            {"payload": [{"id": "nameset", "value": "Radio"}]},
        ]

        class SequenceWebSocket:
            def __init__(self, values):
                self.values = iter(values)
                self.deadlines = []

            def receive(self, deadline=None):
                self.deadlines.append(deadline)
                return 0x1, json.dumps(next(self.values)).encode()

        websocket = SequenceWebSocket(frames)
        evidence = MODULE.wait_getindex_snapshot(websocket, 1.0)
        self.assertTrue(evidence.complete)
        self.assertEqual(evidence.text_frames, len(frames))
        self.assertTrue(all(value is not None for value in websocket.deadlines))

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
