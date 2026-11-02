#!/usr/bin/env python3
"""Offline-only regression tests for bounded chaos v2 tooling."""

import copy
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import chaos_v2 as c


class FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.close()


class FakeWs:
    last = None

    def __init__(self, journal, host=c.HOST):
        self.journal = journal
        self.commands_sent = 0
        self.commands_failed = 0
        self.state = {"station": 1, "playerwrap": "playing", "volume": 7,
                      "volume100": 12, "muted": 0, "balance": 0,
                      "systemInfo": {"build": "bf1d147", "freeHeap": 117500,
                                     "psramFree": 7800000},
                      "webStatus": {"activeSource": "radio", "btConnected": False}}
        FakeWs.last = self

    def snapshot(self, timeout=5):
        return copy.deepcopy(self.state)

    def command(self, text, expected, timeout=8):
        self.commands_sent += 1
        key, value = text.split("=", 1)
        if key == "playstation":
            self.state["station"] = int(value)
            self.state["playerwrap"] = "playing"
        elif key == "source":
            self.state["webStatus"]["activeSource"] = value
        elif key == "start":
            self.state["playerwrap"] = "playing"
        elif key == "stop":
            self.state["playerwrap"] = "stopped"
        elif key == "vol100":
            self.state["volume100"] = int(value)
        elif key == "mute":
            self.state["muted"] = int(value)
        if not expected(self.state):
            self.commands_failed += 1
            raise c.StopRun("fake readback mismatch")
        return self.snapshot()

    def close(self):
        pass


class FakeTelemetry:
    def __init__(self, *args):
        self.failure = None

    def arm(self):
        pass

    def check(self):
        if self.failure:
            raise c.StopRun(self.failure)

    def close(self):
        return True


class ChaosV2Tests(unittest.TestCase):
    def test_dry_run_current_capabilities(self):
        result = c.dry_run(16328847690973426075, 900)
        self.assertEqual(result["planned_scenarios"], 11)
        self.assertEqual(result["active_scenarios"], 7)
        self.assertEqual(set(result["blocked"]), {"BT", "TTS", "MQTT", "MIXED"})
        self.assertFalse(result["full_coverage"])

    def test_dry_run_simulated_full_capabilities(self):
        result = c.dry_run(16328847690973426075, 900,
                           {"radio", "ws", "bt", "tts", "mqtt"})
        self.assertEqual(result["active_scenarios"], 11)
        self.assertTrue(result["full_coverage"])
        self.assertEqual(set(result["phase_budgets"]), set(c.PHASE_ORDER))

    def test_plan_full_capabilities_and_budget(self):
        budgets, blocked = c.plan(810, {"radio", "ws", "bt", "tts", "mqtt"})
        self.assertFalse(blocked)
        self.assertEqual(sum(budgets.values()), 810)
        self.assertEqual(set(budgets), set(c.PHASE_ORDER))

    def test_smoke_scope_and_safe_volume_mute_cycles(self):
        self.assertEqual(len(c.SMOKE_CASES), 9)
        self.assertEqual({s.capabilities for s in c.SMOKE_CASES},
                         {frozenset({"radio"}), frozenset({"radio", "ws"}),
                          frozenset({"ws"})})
        with tempfile.TemporaryDirectory() as tmp:
            runner = c.LiveRunner(mock.Mock(seed=42))
            runner.journal = c.Journal(Path(tmp) / "run")
            runner.ws = FakeWs(runner.journal)
            runner.initial = runner.ws.snapshot()
            runner.deadline = c.time.monotonic() + 10
            runner.wait = lambda _seconds: None
            runner.scenario("radio_volume_cycle")
            runner.scenario("radio_mute_cycle")
            self.assertEqual(runner.ws.state["volume100"], 12)
            self.assertEqual(runner.ws.state["muted"], 0)
            self.assertEqual(runner.ws.commands_sent, 4)

    def test_coverage_never_counts_missing_or_passive_phase(self):
        coverage = c.Coverage()
        coverage.record("RADIO", "radio_stop_play", True)
        result = coverage.phase_result("RADIO", 5400)
        self.assertEqual(result["status"], "PARTIAL")
        self.assertEqual(result["qualified_seconds"], 0)
        self.assertIn("radio_station", result["missing_scenarios"])

    def test_coverage_requires_repeats_for_long_tts(self):
        coverage = c.Coverage()
        coverage.record("TTS", "tts_radio", True)
        result = coverage.phase_result("TTS", 1800)
        self.assertEqual(result["repeat_minimum_per_scenario"], 3)
        self.assertEqual(result["status"], "PARTIAL")

    def test_ws_command_allowlist(self):
        self.assertTrue(c.SAFE_COMMAND.fullmatch("playstation=3"))
        self.assertFalse(c.SAFE_COMMAND.fullmatch("reset=1"))
        self.assertFalse(c.SAFE_COMMAND.fullmatch("vol100=-1"))
        self.assertFalse(c.SAFE_COMMAND.fullmatch("playstation=3;reboot"))

    def test_mqtt_length_encoding(self):
        self.assertEqual(c._mqtt_len(0), b"\x00")
        self.assertEqual(c._mqtt_len(127), b"\x7f")
        self.assertEqual(c._mqtt_len(128), b"\x80\x01")

    def test_mqtt_read_only_retained_parser_offline(self):
        class FakeSocket:
            def __init__(self, data):
                self.data = bytearray(data)
                self.writes = []

            def settimeout(self, *_args):
                pass

            def sendall(self, data):
                self.writes.append(data)

            def recv(self, count):
                if not self.data:
                    raise c.socket.timeout()
                block = bytes(self.data[:count])
                del self.data[:count]
                return block

            def close(self):
                pass

        topic = c.MqttObserver.TOPICS[1]
        publish = c._mqtt_utf8(topic) + b"51"
        stream = b"\x20\x02\x00\x00\x90\x05\x00\x01\x00\x00\x00" + bytes([0x31]) + c._mqtt_len(len(publish)) + publish
        fake = FakeSocket(stream)
        with tempfile.TemporaryDirectory() as tmp, \
             mock.patch.object(c.socket, "create_connection", return_value=fake):
            observer = c.MqttObserver(c.Journal(Path(tmp) / "run"),
                                      auth_raw=b'{"username":"user","password":"secret"}')
            items = observer.read(0.02)
            self.assertEqual(items, [{"topic": topic, "payload": "51", "qos": 0, "retain": True}])
            observer.close()
            self.assertEqual(fake.writes[-1], b"\xe0\x00")

    def test_ha_adapter_uses_supplied_token_and_explicit_qos(self):
        requests = []

        def urlopen(request, timeout=4):
            requests.append(request)
            response = FakeResponse(b"[]")
            response.status = 200
            return response

        read_fd, write_fd = os.pipe()
        try:
            os.write(write_fd, b"explicit-test-token")
            os.close(write_fd)
            with tempfile.TemporaryDirectory() as tmp, \
                 mock.patch.object(c.urllib.request, "urlopen", side_effect=urlopen):
                adapter = c.HomeAssistantPublisher(read_fd, c.Journal(Path(tmp) / "run"))
                adapter.tts("http://192.168.1.15:8099/chaos-v2/normal.wav")
            body = json.loads(requests[-1].data)
            self.assertEqual(body["topic"], "voxone-8CBD1C/command")
            self.assertEqual((body["qos"], body["retain"]), (2, False))
        finally:
            os.close(read_fd)

    def test_ws_snapshot_requires_fresh_readback(self):
        class FakeFrames:
            def __init__(self):
                self.frames = [
                    {"systemInfo": {"build": "bf1d147"}},
                    {"webStatus": {"activeSource": "radio"}},
                    {"current": 1},
                    {"payload": [{"id": "playerwrap", "value": "playing"},
                                 {"id": "volume", "value": 7},
                                 {"id": "volume100", "value": 12},
                                 {"id": "muted", "value": 0},
                                 {"id": "balance", "value": 0}]},
                ]
                self.sent = []

            def send_text(self, value):
                self.sent.append(value)

            def receive(self, deadline=None):
                return 1, json.dumps(self.frames.pop(0)).encode()

        with tempfile.TemporaryDirectory() as tmp:
            ws = c.WsSession.__new__(c.WsSession)
            ws.journal = c.Journal(Path(tmp) / "run")
            ws.ws = FakeFrames()
            state = ws.snapshot()
            self.assertEqual(state["station"], 1)
            self.assertEqual(state["balance"], 0)
            self.assertEqual(ws.ws.sent, list(c.READS))

    def test_restore_exact_fields(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = c.Journal(Path(tmp) / "run")
            ws = FakeWs(journal)
            initial = ws.snapshot()
            ws.state["station"] = 2
            ws.state["muted"] = 1
            final = c.restore(ws, initial, journal)
            self.assertTrue(c.validate_restore(initial, final))
            self.assertEqual(ws.commands_sent, 2)

    def test_full_stop_executes_cleanup_offline(self):
        ready = {"ready": True, "bootId": "offline-test", "ota": {"imageState": "valid"},
                 "www": {"status": "READY"}}
        playlist = "#VOXONE_STATIONS\n" + "\n".join(
            f"id{i}\tStation{i}\thttp://host/{'x.mp3' if i in (2, 3) else 'x'}\t0\tnormal"
            for i in range(1, 8))

        def urlopen(request, timeout=4):
            url = request if isinstance(request, str) else request.full_url
            if url.endswith("/api/ready"):
                body = json.dumps(ready).encode()
            elif url.endswith("/api/security/state"):
                body = b'{"state":"open"}'
            elif url.endswith("/api/config/backup"):
                body = b"fake-config"
            else:
                body = playlist.encode()
            return FakeResponse(body)

        def stop_after_mutation(runner, *_args):
            runner.ws.state["station"] = 2
            raise c.StopRun("simulated STOP after mutation")

        args = mock.Mock(duration=900, seed=42, safe_volume_confirmed=True, smoke=False,
                         bt_tone=None, ha_token_fd=None, mqtt_auth_fd=None)
        with tempfile.TemporaryDirectory() as tmp, \
             mock.patch.object(c, "OUT_ROOT", Path(tmp) / "runs"), \
             mock.patch.object(c, "WsSession", FakeWs), \
             mock.patch.object(c, "Telemetry", FakeTelemetry), \
             mock.patch.object(c.urllib.request, "urlopen", side_effect=urlopen), \
             mock.patch.object(c.LiveRunner, "run_phase", stop_after_mutation):
            exit_code = c.LiveRunner(args).run()
            self.assertEqual(exit_code, 2)
            result_file = next((Path(tmp) / "runs").glob("*/RESULT.json"))
            result = json.loads(result_file.read_text())
            self.assertEqual(result["result"], "STOPPED")
            self.assertTrue(result["restored"])
            self.assertEqual(FakeWs.last.state["station"], 1)


if __name__ == "__main__":
    unittest.main()
