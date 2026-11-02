#!/usr/bin/env python3
"""Bounded L0 live chaos orchestrator. Default mode is offline dry-run.

This is test tooling, not firmware. Live mode requires a separate approval,
verified preflight, and explicit --live --confirm-live flags. It never starts
an 8 h run by default. No broker/HA credential discovery is implemented.
"""

from __future__ import annotations

import argparse
import collections
import fcntl
import hashlib
import json
import math
import os
import random
import re
import select
import signal
import socket
import subprocess
import sys
import threading
import time
import urllib.request
import uuid
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import security_enforce_hil as hil  # noqa: E402

HOST = "192.168.1.56"
BRIDGE = "voxone-bridge"
UART_PORT = "/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A7039N0Z-if00-port0"
OUT_ROOT = Path.home() / "voxone-hil" / "chaos-v2" / "runs"
FATAL_UART = re.compile(rb"Guru Meditation|panic|assert failed|Task watchdog|\bWDT\b|Stack canary|CORRUPT HEAP|Bad (head|tail)", re.I)
READS = ("getindex=1", "getsystem=1", "getwebstatus=1")
SAFE_COMMAND = re.compile(r"^(?:start=1|stop=1|next=1|prev=1|source=(?:radio|bt)|mute=[01]|vol100=(?:[0-9]|[1-9][0-9]|100)|playstation=[1-9][0-9]*)$")


def iso():
    return datetime.now().astimezone().isoformat(timespec="milliseconds")


def sha(path: Path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


class StopRun(RuntimeError):
    pass


class Journal:
    def __init__(self, root: Path):
        self.root = root
        root.mkdir(parents=True, exist_ok=False)
        self.lock = threading.Lock()

    def write(self, file: str, **data):
        row = {"at": iso(), **data}
        with self.lock, (self.root / file).open("a", encoding="utf-8") as f:
            f.write(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n")
        return row

    def finish(self, result: dict):
        (self.root / "RESULT.json").write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n")
        lines = [f"{sha(p)}  {p.name}" for p in sorted(self.root.iterdir()) if p.is_file() and p.name != "SHA256SUMS"]
        (self.root / "SHA256SUMS").write_text("\n".join(lines) + "\n")


@dataclass(frozen=True)
class Scenario:
    name: str
    phase: str
    capabilities: frozenset[str]


SCENARIOS = (
    Scenario("radio_stop_play", "RADIO", frozenset({"radio"})),
    Scenario("radio_station", "RADIO", frozenset({"radio"})),
    Scenario("radio_idempotent", "RADIO", frozenset({"radio"})),
    Scenario("radio_fast_bounded", "RADIO", frozenset({"radio"})),
    Scenario("radio_steady", "RADIO", frozenset({"radio"})),
    Scenario("radio_ws_interleave", "RADIO", frozenset({"radio", "ws"})),
    Scenario("ws_reconnect", "WS", frozenset({"ws"})),
    Scenario("bt_radio_cycle", "BT", frozenset({"bt", "radio"})),
    Scenario("tts_radio", "TTS", frozenset({"tts", "radio"})),
    Scenario("mqtt_retained_live", "MQTT", frozenset({"mqtt", "radio"})),
    Scenario("mixed_bt_tts_ws", "MIXED", frozenset({"bt", "tts", "ws", "radio"})),
)
SMOKE_CASES = (
    *(s for s in SCENARIOS if s.name in (
        "radio_stop_play", "radio_station", "radio_idempotent",
        "radio_fast_bounded", "radio_steady", "radio_ws_interleave",
        "ws_reconnect")),
    Scenario("radio_volume_cycle", "RADIO", frozenset({"radio"})),
    Scenario("radio_mute_cycle", "RADIO", frozenset({"radio"})),
)
PHASE_ORDER = ("RADIO", "WS", "BT", "TTS", "MQTT", "MIXED")
WEIGHTS = {"RADIO": 5, "WS": 2, "BT": 2, "TTS": 2, "MQTT": 2, "MIXED": 2}
MIN_ACTION_GAP = 8.0
MAX_COMMANDS_PER_MINUTE = 4.0
MIN_PHASE_ACTION_RATE = 0.5


def plan(total_seconds: int, capabilities: set[str]):
    """Skip unavailable phases before time allocation; never count them."""
    if not 60 <= total_seconds <= 28800:
        raise ValueError("duration outside 60..28800 s")
    blocked = {}
    active = []
    for phase in PHASE_ORDER:
        needed = set().union(*(s.capabilities for s in SCENARIOS if s.phase == phase))
        missing = sorted(needed - capabilities)
        if missing:
            blocked[phase] = missing
        else:
            active.append(phase)
    if not active:
        raise StopRun("no admitted active phase")
    weight = sum(WEIGHTS[p] for p in active)
    budgets = {p: total_seconds * WEIGHTS[p] // weight for p in active}
    budgets[active[-1]] += total_seconds - sum(budgets.values())
    return budgets, blocked


class Coverage:
    def __init__(self):
        self.operations = collections.Counter()
        self.failed = collections.Counter()
        self.scenarios = collections.Counter()
        self.phase_seconds = collections.Counter()

    def record(self, phase: str, name: str, ok: bool):
        self.operations[phase] += 1
        if ok:
            self.scenarios[name] += 1
        else:
            self.failed[phase] += 1

    def phase_result(self, phase: str, budget: float):
        count = self.operations[phase]
        rate = count / max(budget / 60, 1e-9)
        names = [s.name for s in SCENARIOS if s.phase == phase]
        missing = [n for n in names if self.scenarios[n] == 0]
        # A phase must execute every admitted scenario and maintain a bounded
        # action rate. A passive wait can never be misreported as stress.
        repeat_min = max(1, math.ceil(budget / {"BT": 1200, "TTS": 600,
                                                "MQTT": 900, "MIXED": 1200}.get(phase, budget)))
        repeat_short = [n for n in names if self.scenarios[n] < repeat_min]
        rate_ok = phase not in ("RADIO", "WS") or rate >= MIN_PHASE_ACTION_RATE
        passed = count > 0 and not missing and not repeat_short and not self.failed[phase] and rate_ok
        return {"phase": phase, "status": "PASS" if passed else "PARTIAL",
                "operations": count, "failed": self.failed[phase],
                "rate_per_minute": round(rate, 3), "missing_scenarios": missing,
                "below_repeat_minimum": repeat_short, "repeat_minimum_per_scenario": repeat_min,
                "qualified_seconds": round(budget, 3) if passed else 0}


class WsSession:
    def __init__(self, journal: Journal, host=HOST):
        self.journal = journal
        self.host = host
        self.ws = None
        self.commands_sent = 0
        self.commands_failed = 0
        self.open()

    def open(self):
        device = hil.Device(self.host, 80, 4)
        status, headers, sock, buffered = hil.websocket_handshake(device, origin=device.origin)
        if status != 101 or sock is None or headers.get("sec-websocket-protocol") != "voxone.v1":
            raise StopRun(f"WS handshake failed: {status}")
        self.ws = hil.WebSocket(sock, buffered)

    def close(self):
        if self.ws:
            try:
                self.ws.close_gracefully(2)
            finally:
                self.ws = None

    def snapshot(self, timeout=5):
        start = time.monotonic()
        for cmd in READS:
            self.ws.send_text(cmd)
            self.journal.write("ws.jsonl", direction="tx", command=cmd)
        data = {"payload": {}}
        fresh = set()
        first_ms = None
        deadline = start + timeout
        need = {"current", "systemInfo", "webStatus", "playerwrap", "volume100", "muted", "balance", "volume"}
        while time.monotonic() < deadline and not need <= fresh:
            try:
                opcode, payload = self.ws.receive(deadline=deadline)
            except (socket.timeout, TimeoutError):
                break
            if opcode == 8:
                raise StopRun("WS closed unexpectedly")
            if opcode != 1:
                continue
            if first_ms is None:
                first_ms = round((time.monotonic() - start) * 1000, 2)
            try:
                value = json.loads(payload)
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            if not isinstance(value, dict):
                continue
            self.journal.write("ws.jsonl", direction="rx", frame=value)
            for key in ("current", "systemInfo", "webStatus"):
                if key in value:
                    data[key] = value[key]
                    fresh.add(key)
            for item in value.get("payload", []):
                if isinstance(item, dict) and isinstance(item.get("id"), str):
                    key = item["id"]
                    data["payload"][key] = item.get("value")
                    fresh.add(key)
        if not need <= fresh:
            raise StopRun("incomplete fresh WS readback: " + ",".join(sorted(need - fresh)))
        result = {"station": data["current"], "systemInfo": data["systemInfo"],
                  "webStatus": data["webStatus"], **data["payload"]}
        self.journal.write("operations.jsonl", kind="ws_readback", firstResponseMs=first_ms,
                           completeMs=round((time.monotonic() - start) * 1000, 2), state=result)
        return result

    def command(self, text: str, expected, timeout=8):
        if not SAFE_COMMAND.fullmatch(text):
            raise StopRun("command not in safe allowlist")
        start = time.monotonic()
        self.ws.send_text(text)
        self.commands_sent += 1
        self.journal.write("operations.jsonl", kind="command_send", command=text)
        while time.monotonic() - start < timeout:
            state = self.snapshot(timeout=min(4, timeout - (time.monotonic() - start)))
            if expected(state):
                self.journal.write("operations.jsonl", kind="command_readback", command=text,
                                   matched=True, latencyMs=round((time.monotonic()-start)*1000, 2), state=state)
                return state
        self.journal.write("operations.jsonl", kind="command_readback", command=text, matched=False)
        self.commands_failed += 1
        raise StopRun("command readback timeout: " + text)

    def reconnect(self):
        self.close()
        self.open()
        return self.snapshot()


class Telemetry:
    """Independent HTTP/UART watchdog; never sends state-changing commands."""
    def __init__(self, journal: Journal, initial_boot: str, seconds: int, host=HOST):
        self.journal, self.initial_boot, self.seconds, self.host = journal, initial_boot, seconds, host
        self.stop = threading.Event()
        self.failure = None
        self.thread = None
        self.logger = None
        self.tail = None
        self.remote = "/home/piotrek/voxone-bridge/logs/CHAOS-V2-" + uuid.uuid4().hex[:12]
        self.live_uart = journal.root / "uart-live.raw"
        self.previous = b""
        self.started = None
        self.remote_pid = None

    def arm(self):
        argv = ["python3", "/home/piotrek/voxone-bridge/uart_rx_logger.py", "--port", UART_PORT,
                "--output-dir", self.remote, "--host", self.host, "--duration", str(self.seconds + 60),
                "--http-interval", "5", "--continuous-capture", "--acknowledge-open-line-risk"]
        import shlex
        command = "echo REMOTE_PID=$$; exec " + " ".join(shlex.quote(a) for a in argv)
        self.logger = subprocess.Popen(["ssh", "-o", "BatchMode=yes", BRIDGE, command],
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        deadline = time.monotonic() + 25
        armed = False
        while time.monotonic() < deadline:
            if self.logger.poll() is not None:
                raise StopRun("UART logger exited before ARMED")
            if select.select([self.logger.stdout], [], [], 0.5)[0]:
                line = self.logger.stdout.readline().strip()
                self.journal.write("events.jsonl", kind="uart_logger", line=line)
                if line.startswith("REMOTE_PID="):
                    self.remote_pid = int(line.split("=", 1)[1])
                if line.startswith("ARMED"):
                    armed = True
                    break
        if not armed or self.remote_pid is None:
            raise StopRun("UART logger not armed")
        self.tail = subprocess.Popen(["ssh", "-o", "BatchMode=yes", BRIDGE,
                                      "tail -c +1 -F " + shlex.quote(self.remote + "/uart.raw")],
                                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        os.set_blocking(self.tail.stdout.fileno(), False)
        self.started = time.monotonic()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self):
        failures = 0
        try:
            while not self.stop.wait(2):
                if time.monotonic() - self.started > self.seconds:
                    raise StopRun("independent wall-clock limit")
                if self.logger.poll() is not None or self.tail.poll() is not None:
                    raise StopRun("UART logger/tail exited")
                while True:
                    try:
                        chunk = os.read(self.tail.stdout.fileno(), 65536)
                    except BlockingIOError:
                        break
                    if not chunk:
                        break
                    with self.live_uart.open("ab") as f:
                        f.write(chunk)
                    if FATAL_UART.search(self.previous + chunk):
                        raise StopRun("fatal UART marker")
                    self.previous = chunk[-128:]
                try:
                    with urllib.request.urlopen("http://" + self.host + "/api/ready", timeout=3) as r:
                        ready = json.load(r)
                    if not ready.get("ready") or ready.get("bootId") != self.initial_boot or ready.get("www", {}).get("status") != "READY":
                        raise StopRun("READY/bootId/WWW changed")
                    failures = 0
                    self.journal.write("telemetry.jsonl", ready=ready)
                except StopRun:
                    raise
                except Exception as exc:
                    failures += 1
                    self.journal.write("telemetry.jsonl", error=type(exc).__name__, consecutive=failures)
                    if failures >= 2:
                        raise StopRun("two consecutive READY transport failures")
        except Exception as exc:
            self.failure = str(exc)
            self.stop.set()
            self.journal.write("events.jsonl", kind="STOP", reason=self.failure)

    def check(self):
        if self.failure:
            raise StopRun(self.failure)

    def close(self):
        self.stop.set()
        if self.thread:
            self.thread.join(timeout=5)
        if self.tail:
            self.tail.terminate()
            try:
                self.tail.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.tail.kill()
        if self.logger:
            # SIGINT the exact process started by this run; its finalizer writes
            # the authoritative manifest. Do not kill a broad process group.
            if self.remote_pid is not None:
                subprocess.run(["ssh", "-o", "BatchMode=yes", BRIDGE,
                                "kill -INT " + str(self.remote_pid)], timeout=8, check=False)
            try:
                self.logger.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.journal.write("events.jsonl", kind="uart_logger_timeout")
        # Exact remote files are copied only to this new run directory.
        copied = True
        for name in ("uart.raw", "manifest.json", "uart-chunks.jsonl"):
            source = f"{BRIDGE}:{self.remote}/{name}"
            result = subprocess.run(["scp", "-q", "-o", "BatchMode=yes", source,
                                     str(self.journal.root / ("uart-" + name))], timeout=12, check=False)
            copied = copied and result.returncode == 0
        verified = False
        if copied:
            try:
                raw = (self.journal.root / "uart-uart.raw").read_bytes()
                manifest = json.loads((self.journal.root / "uart-manifest.json").read_text())
                chunks = [json.loads(line) for line in
                          (self.journal.root / "uart-uart-chunks.jsonl").read_text().splitlines()]
                verified = (len(raw) > 0 and manifest.get("bytes") == len(raw) and
                            manifest.get("bootIdChanged") is False and
                            manifest.get("capture", {}).get("unwrittenReadBytes") == 0 and
                            b"".join(bytes.fromhex(x["hex"]) for x in chunks) == raw and
                            manifest.get("status") in ("PASS", "INTERRUPTED"))
                self.journal.write("events.jsonl", kind="uart_integrity", verified=verified,
                                   manifestStatus=manifest.get("status"), bytes=len(raw))
            except Exception as exc:
                self.journal.write("events.jsonl", kind="uart_integrity", verified=False,
                                   error=type(exc).__name__)
        else:
            self.journal.write("events.jsonl", kind="uart_integrity", verified=False,
                               reason="remote copy incomplete")
        return verified


def validate_restore(initial: dict, final: dict):
    keys = ("station", "playerwrap", "volume", "volume100", "muted", "balance")
    return all(initial.get(k) == final.get(k) for k in keys) and initial["webStatus"].get("activeSource") == final["webStatus"].get("activeSource")


def restore(ws: WsSession, initial: dict, journal: Journal):
    """Read back each correction; never change balance or exceed initial volume."""
    state = ws.snapshot()
    target_source = initial["webStatus"]["activeSource"]
    if state["webStatus"].get("activeSource") != target_source:
        state = ws.command("source=" + target_source, lambda x: x["webStatus"].get("activeSource") == target_source)
    if state["station"] != initial["station"]:
        state = ws.command(f"playstation={initial['station']}", lambda x: x["station"] == initial["station"])
    if state["playerwrap"] != initial["playerwrap"]:
        command = "start=1" if initial["playerwrap"] == "playing" else "stop=1"
        state = ws.command(command, lambda x: x["playerwrap"] == initial["playerwrap"])
    if state["volume100"] != initial["volume100"]:
        state = ws.command(f"vol100={initial['volume100']}", lambda x: x["volume100"] == initial["volume100"])
    if state["muted"] != initial["muted"]:
        state = ws.command(f"mute={int(initial['muted'])}", lambda x: x["muted"] == initial["muted"])
    final = ws.snapshot()
    ok = validate_restore(initial, final)
    journal.write("events.jsonl", kind="restore", ok=ok, final=final)
    if not ok:
        raise StopRun("restore readback mismatch")
    return final


class HomeAssistantPublisher:
    """Uses only a caller-supplied HA bearer token; never reads HA config."""
    def __init__(self, token_fd: int, journal: Journal):
        token = os.read(token_fd, 8192).decode().strip()
        if not token or len(token) > 4096:
            raise StopRun("invalid explicitly supplied HA token")
        self.token = token
        self.journal = journal
        request = urllib.request.Request("http://192.168.1.14:8123/api/",
                                         headers={"Authorization": "Bearer " + token})
        with urllib.request.urlopen(request, timeout=4) as response:
            if response.status != 200:
                raise StopRun("HA API authorization failed")

    def tts(self, url: str):
        if not url.startswith("http://192.168.1.15:8099/chaos-v2/normal.wav"):
            raise StopRun("TTS URL outside approved local fixture")
        body = json.dumps({"topic": "voxone-8CBD1C/command", "payload": url,
                           "qos": 2, "retain": False}).encode()
        request = urllib.request.Request(
            "http://192.168.1.14:8123/api/services/mqtt/publish", data=body,
            headers={"Authorization": "Bearer " + self.token,
                     "Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(request, timeout=5) as response:
            if response.status not in (200, 201):
                raise StopRun("HA mqtt.publish rejected")
        self.journal.write("operations.jsonl", kind="ha_mqtt_publish", topic="voxone-8CBD1C/command",
                           payload=url, qos=2, retain=False)


def _mqtt_len(value: int):
    out = bytearray()
    while True:
        digit = value % 128
        value //= 128
        out.append(digit | (128 if value else 0))
        if not value:
            return bytes(out)


def _mqtt_utf8(value: str):
    b = value.encode("utf-8")
    if len(b) > 65535:
        raise ValueError("MQTT field too long")
    return len(b).to_bytes(2, "big") + b


class MqttObserver:
    """Minimal read-only MQTT 3.1.1 client. Credentials must be user-supplied FD."""
    TOPICS = ("voxone-8CBD1C/status", "voxone-8CBD1C/volume",
              "voxone-8CBD1C/playlist")

    def __init__(self, journal: Journal, auth_fd: int | None = None, auth_raw: bytes | None = None):
        raw = auth_raw if auth_raw is not None else os.read(auth_fd, 8192)
        auth = json.loads(raw)
        username, password = auth.get("username"), auth.get("password")
        if not isinstance(username, str) or not isinstance(password, str):
            raise StopRun("MQTT credentials must be supplied explicitly")
        self.journal = journal
        self.sock = socket.create_connection(("192.168.1.14", 1883), timeout=4)
        self.sock.settimeout(4)
        cid = "voxone-chaos-v2-" + uuid.uuid4().hex[:12]
        variable = _mqtt_utf8("MQTT") + b"\x04\xc2" + (900).to_bytes(2, "big")
        payload = _mqtt_utf8(cid) + _mqtt_utf8(username) + _mqtt_utf8(password)
        self._send(0x10, variable + payload)
        kind, body = self._recv()
        if kind != 0x20 or len(body) != 2 or body[1] != 0:
            self.close()
            raise StopRun("MQTT CONNACK rejected")
        subscribe = b"\x00\x01" + b"".join(_mqtt_utf8(topic) + b"\x00" for topic in self.TOPICS)
        self._send(0x82, subscribe)
        kind, body = self._recv()
        if kind != 0x90 or body[:2] != b"\x00\x01" or len(body) != 5 or any(x == 0x80 for x in body[2:]):
            self.close()
            raise StopRun("MQTT SUBACK rejected")

    def _send(self, header, payload):
        self.sock.sendall(bytes([header]) + _mqtt_len(len(payload)) + payload)

    def _take(self, count):
        out = bytearray()
        while len(out) < count:
            block = self.sock.recv(count - len(out))
            if not block:
                raise StopRun("MQTT socket closed")
            out.extend(block)
        return bytes(out)

    def _recv(self):
        header = self._take(1)[0]
        multiplier, length = 1, 0
        for _ in range(4):
            b = self._take(1)[0]
            length += (b & 127) * multiplier
            if not b & 128:
                break
            multiplier *= 128
        else:
            raise StopRun("malformed MQTT remaining length")
        if length > 65536:
            raise StopRun("MQTT packet too large")
        return header, self._take(length)

    def read(self, seconds: float):
        deadline = time.monotonic() + seconds
        found = []
        while time.monotonic() < deadline:
            self.sock.settimeout(min(3, max(0.1, deadline - time.monotonic())))
            try:
                header, body = self._recv()
            except socket.timeout:
                continue
            if header >> 4 != 3:
                continue
            if len(body) < 2:
                raise StopRun("short MQTT PUBLISH")
            size = int.from_bytes(body[:2], "big")
            if size + 2 > len(body):
                raise StopRun("short MQTT topic")
            topic = body[2:2+size].decode()
            if topic not in self.TOPICS:
                raise StopRun("unexpected MQTT topic")
            qos = (header >> 1) & 3
            offset = 2 + size + (2 if qos else 0)
            item = {"topic": topic, "payload": body[offset:].decode(errors="replace"),
                    "qos": qos, "retain": bool(header & 1)}
            found.append(item)
            self.journal.write("mqtt.jsonl", **item)
        return found

    def close(self):
        if getattr(self, "sock", None):
            try:
                self._send(0xE0, b"")
            except OSError:
                pass
            self.sock.close()
            self.sock = None


class BlueAlsaSource:
    """Pre-paired, pre-powered bridge A2DP only; never scan/pair/unblock."""
    PEER = "30:76:F5:EF:F3:5A"

    def __init__(self, tone: Path, journal: Journal):
        self.tone, self.journal = tone, journal
        self.play_process = None
        self.connected_by_us = False
        show = self._ssh("bluetoothctl show")
        paired = self._ssh("bluetoothctl devices Paired")
        alsa_devices = self._ssh("aplay -L") if self._ssh("command -v aplay") else ""
        if ("Powered: yes" not in show or "Audio Source" not in show or
                self.PEER not in paired or "bluealsa" not in alsa_devices.lower()):
            raise StopRun("bridge A2DP Source preflight unavailable")
        if "Connected: no" not in self._ssh("bluetoothctl info " + self.PEER):
            raise StopRun("BT peer already connected; refusing to take ownership")
        if not tone.is_file() or tone.stat().st_size > 5_000_000:
            raise StopRun("missing/big BT tone")

    @staticmethod
    def _ssh(command, timeout=8):
        return subprocess.run(["ssh", "-o", "BatchMode=yes", BRIDGE, command],
                              capture_output=True, text=True, timeout=timeout, check=True).stdout

    def connect(self):
        self._ssh("bluetoothctl connect " + self.PEER, timeout=20)
        self.connected_by_us = True
        info = self._ssh("bluetoothctl info " + self.PEER)
        if "Connected: yes" not in info:
            raise StopRun("BT connect readback mismatch")
        self.journal.write("operations.jsonl", kind="bt_connect", peer=self.PEER)

    def play(self):
        if self.play_process is not None:
            raise StopRun("BT tone already playing")
        pcm = "bluealsa:DEV=" + self.PEER + ",PROFILE=a2dp"
        stream = self.tone.open("rb")
        self.play_process = subprocess.Popen(["ssh", "-o", "BatchMode=yes", BRIDGE,
                                               "aplay -q -D " + pcm + " -"],
                                              stdin=stream, stdout=subprocess.DEVNULL,
                                              stderr=subprocess.PIPE)
        stream.close()
        self.journal.write("operations.jsonl", kind="bt_tone_start", sha256=sha(self.tone))

    def stop(self):
        if self.play_process is not None:
            self.play_process.terminate()
            try:
                self.play_process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                self.play_process.kill()
            self.play_process = None
            self.journal.write("operations.jsonl", kind="bt_tone_stop")

    def verify_playback(self):
        if self.play_process is None:
            raise StopRun("BT tone process absent")
        code = self.play_process.poll()
        if code not in (None, 0):
            raise StopRun("BT aplay exited with error")

    def disconnect(self):
        self.stop()
        self._ssh("bluetoothctl disconnect " + self.PEER, timeout=20)
        info = self._ssh("bluetoothctl info " + self.PEER)
        if "Connected: no" not in info:
            raise StopRun("BT disconnect readback mismatch")
        self.connected_by_us = False
        self.journal.write("operations.jsonl", kind="bt_disconnect", peer=self.PEER)


class LiveRunner:
    """One command owner; independent UART/READY telemetry and hard deadline."""
    SAFE_STATIONS = (2, 3, 7)  # verified existing MP3 radio choices; never edits playlist

    def __init__(self, args):
        self.args = args
        self.rng = random.Random(args.seed)
        self.journal = None
        self.ws = None
        self.telemetry = None
        self.bt = None
        self.ha = None
        self.mqtt = None
        self.mqtt_auth_raw = None
        self.initial = None
        self.initial_ready = None
        self.initial_security = None
        self.playlist_sha = None
        self.config_sha = None
        self.coverage = Coverage()
        self.stopping = threading.Event()
        self.deadline = None
        self.result = "UNVERIFIED"
        self.command_times = collections.deque()
        self.last_command_at = None
        self.last_ws_monitor = 0.0

    def check(self):
        if self.stopping.is_set() or time.monotonic() >= self.deadline:
            raise StopRun("hard stop/deadline")
        if self.telemetry:
            self.telemetry.check()

    def wait(self, seconds):
        end = min(self.deadline, time.monotonic() + seconds)
        while time.monotonic() < end:
            self.check()
            if self.ws and self.telemetry and time.monotonic() - self.last_ws_monitor >= 10:
                state = self.ws.snapshot()
                heap = state["systemInfo"].get("freeHeap")
                psram = state["systemInfo"].get("psramFree")
                if (not isinstance(heap, int) or heap < 30000 or
                        not isinstance(psram, int) or psram < 1000000):
                    raise StopRun("heap/PSRAM safety threshold")
                self.last_ws_monitor = time.monotonic()
            time.sleep(min(0.5, end - time.monotonic()))

    def command(self, text, expected):
        self.check()
        if self.last_command_at is not None:
            self.wait(max(0, MIN_ACTION_GAP - (time.monotonic() - self.last_command_at)))
        while self.command_times and time.monotonic() - self.command_times[0] >= 60:
            self.command_times.popleft()
        if len(self.command_times) >= int(MAX_COMMANDS_PER_MINUTE):
            self.wait(60 - (time.monotonic() - self.command_times[0]) + 0.1)
            self.check()
        if text.startswith("vol100=") and int(text.split("=", 1)[1]) > self.initial["volume100"]:
            raise StopRun("volume would exceed initial safe level")
        result = self.ws.command(text, expected)
        self.last_command_at = time.monotonic()
        self.command_times.append(self.last_command_at)
        self.check()
        return result

    def api_probe(self):
        self.check()
        started = time.monotonic()
        with urllib.request.urlopen("http://" + HOST + "/api/ready", timeout=4) as response:
            ready = json.load(response)
        ok = (ready.get("ready") is True and
              ready.get("bootId") == self.initial_ready["bootId"] and
              ready.get("ota", {}).get("imageState") == "valid" and
              ready.get("www", {}).get("status") == "READY")
        self.journal.write("operations.jsonl", kind="api_probe", ok=ok,
                           latencyMs=round((time.monotonic() - started) * 1000, 2), ready=ready)
        if not ok:
            raise StopRun("API READY/bootId/OTA/WWW check failed")

    def station(self):
        current = self.ws.snapshot()["station"]
        target = self.rng.choice([x for x in self.SAFE_STATIONS if x != current])
        return self.command(f"playstation={target}", lambda x: x["station"] == target and x["playerwrap"] == "playing")

    def fixture(self):
        path = Path.home() / "voxone-hil" / "tts-abc-release-fixtures-1" / "tts-abc-release-4s-mono.wav"
        if not path.is_file() or sha(path) != "781f1a9def10816a70b07c3de77198e92d9a23b547519908b7da78cd0906980c":
            raise StopRun("TTS fixture SHA mismatch")
        log = self.journal.root / ("fixture-" + uuid.uuid4().hex[:8] + ".jsonl")
        proc = subprocess.Popen([sys.executable, str(ROOT / "tools" / "chaos_v2_fixture.py"),
                                 "--wav", str(path), "--log", str(log),
                                 "--max-requests", "2", "--max-seconds", "60"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            self.wait(1.0)
            if proc.poll() is not None:
                raise StopRun("TTS fixture server failed to start")
            self.ha.tts("http://192.168.1.15:8099/chaos-v2/normal.wav")
            deadline = min(self.deadline, time.monotonic() + 25)
            while time.monotonic() < deadline:
                self.check()
                if log.exists() and '"kind": "complete"' in log.read_text():
                    break
                self.wait(0.5)
            else:
                raise StopRun("no complete L0 TTS fixture request")
            self.wait(6)
            self.journal.write("operations.jsonl", kind="tts_http_complete", fixtureLog=str(log))
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=4)
            except subprocess.TimeoutExpired:
                proc.kill()

    def scenario(self, name):
        if name == "radio_stop_play":
            self.command("stop=1", lambda x: x["playerwrap"] == "stopped")
            self.wait(MIN_ACTION_GAP)
            self.command("start=1", lambda x: x["playerwrap"] == "playing")
        elif name == "radio_station":
            self.station()
        elif name == "radio_idempotent":
            before = self.ws.snapshot()
            self.command("start=1", lambda x: x["playerwrap"] == "playing" and x["station"] == before["station"])
        elif name == "radio_fast_bounded":
            self.station()
            self.wait(MIN_ACTION_GAP)
            self.station()
        elif name == "radio_steady":
            self.wait(30)
            state = self.ws.snapshot()
            if state["playerwrap"] != "playing":
                raise StopRun("RADIO steady window lost PLAY")
        elif name == "radio_ws_interleave":
            self.station()
            self.ws.snapshot()  # telemetry runs in parallel; no second WS client
        elif name == "radio_volume_cycle":
            initial = self.initial["volume100"]
            target = max(1, initial // 2)
            if target >= initial:
                raise StopRun("initial volume too low for safe cycle")
            self.command(f"vol100={target}", lambda x: x["volume100"] == target)
            self.command(f"vol100={initial}", lambda x: x["volume100"] == initial)
        elif name == "radio_mute_cycle":
            initial = int(self.initial["muted"])
            self.command(f"mute={1 - initial}", lambda x: int(x["muted"]) == 1 - initial)
            self.command(f"mute={initial}", lambda x: int(x["muted"]) == initial)
        elif name == "ws_reconnect":
            self.ws.reconnect()
        elif name == "bt_radio_cycle":
            self.bt.connect()
            try:
                self.command("source=bt", lambda x: x["webStatus"].get("activeSource") == "bt")
                self.bt.play()
                self.wait(2)
                self.bt.verify_playback()
                self.wait(10)
                self.bt.verify_playback()
                state = self.ws.snapshot()
                if not state["webStatus"].get("btConnected"):
                    raise StopRun("BT connected status absent")
            finally:
                self.bt.stop()
                if self.bt.connected_by_us:
                    self.bt.disconnect()
            restore(self.ws, self.initial, self.journal)
        elif name == "tts_radio":
            before = self.ws.snapshot()
            if before["webStatus"].get("activeSource") != "radio" or before["playerwrap"] != "playing":
                raise StopRun("TTS RADIO precondition failed")
            self.fixture()
            after = self.ws.snapshot()
            if (after["webStatus"].get("activeSource"), after["station"], after["playerwrap"]) != (
                    "radio", before["station"], "playing"):
                raise StopRun("TTS RADIO restore mismatch")
        elif name == "mqtt_retained_live":
            self.mqtt = MqttObserver(self.journal, auth_raw=self.mqtt_auth_raw)
            try:
                initial = self.mqtt.read(4)
                retained = {x["topic"] for x in initial if x["retain"] and x["qos"] == 0}
                if retained != set(MqttObserver.TOPICS):
                    raise StopRun("MQTT retained status/volume/playlist incomplete")
                by_topic = {x["topic"].split("/")[-1]: x["payload"] for x in initial if x["retain"]}
                status = json.loads(by_topic["status"])
                if (not isinstance(status.get("station"), int) or
                        status.get("on") not in (0, 1) or
                        not by_topic["volume"].isdigit() or
                        by_topic["playlist"] != "http://192.168.1.56/data/stations.tsv"):
                    raise StopRun("MQTT retained payload format mismatch")
                self.station()
                live = self.mqtt.read(8)
                station = self.ws.snapshot()["station"]
                if not any(x["topic"].endswith("/status") and not x["retain"] and
                           json.loads(x["payload"]).get("station") == station for x in live):
                    raise StopRun("MQTT live status not consistent with WS")
                fresh = MqttObserver(self.journal, auth_raw=self.mqtt_auth_raw)
                try:
                    replay = fresh.read(4)
                    if not any(x["topic"].endswith("/status") and x["retain"] and
                               json.loads(x["payload"]).get("station") == station for x in replay):
                        raise StopRun("MQTT fresh retained status mismatch")
                finally:
                    fresh.close()
            finally:
                self.mqtt.close()
                self.mqtt = None
        elif name == "mixed_bt_tts_ws":
            self.bt.connect()
            try:
                self.bt.play()
                self.wait(2)
                self.bt.verify_playback()
                self.command("source=bt", lambda x: x["webStatus"].get("activeSource") == "bt")
                self.ws.reconnect()
                self.fixture()
                after = self.ws.snapshot()
                if after["webStatus"].get("activeSource") != "bt" or not after["webStatus"].get("btConnected"):
                    raise StopRun("mixed BT/TTS restore mismatch")
            finally:
                self.bt.stop()
                if self.bt.connected_by_us:
                    self.bt.disconnect()
            restore(self.ws, self.initial, self.journal)
        else:
            raise StopRun("unknown scenario")

    def execute(self, scenario: Scenario):
        self.check()
        start = time.monotonic()
        self.journal.write("operations.jsonl", kind="scenario_start", name=scenario.name, phase=scenario.phase)
        try:
            self.scenario(scenario.name)
            self.coverage.record(scenario.phase, scenario.name, True)
            self.journal.write("operations.jsonl", kind="scenario_end", name=scenario.name,
                               ok=True, elapsed=round(time.monotonic() - start, 3))
        except Exception as exc:
            self.coverage.record(scenario.phase, scenario.name, False)
            self.journal.write("operations.jsonl", kind="scenario_end", name=scenario.name,
                               ok=False, error=str(exc))
            raise

    def run_phase(self, phase, seconds):
        phase_start = time.monotonic()
        end = min(self.deadline, phase_start + seconds)
        cases = [x for x in SCENARIOS if x.phase == phase]
        repeat_min = max(1, math.ceil(seconds / {"BT": 1200, "TTS": 600,
                                                "MQTT": 900, "MIXED": 1200}.get(phase, seconds)))
        queue = cases * repeat_min
        self.rng.shuffle(queue)
        for item in queue:
            if time.monotonic() >= end:
                break
            self.execute(item)
            self.wait(MIN_ACTION_GAP)
        # RADIO/WS need continuing intensity. Other phases use bounded repeats.
        while phase in ("RADIO", "WS") and time.monotonic() + 35 < end:
            self.execute(self.rng.choice(cases))
            self.wait(45 if phase == "RADIO" else 55)
        while time.monotonic() < end:
            self.wait(min(2, end - time.monotonic()))
        row = self.coverage.phase_result(phase, time.monotonic() - phase_start)
        self.journal.write("events.jsonl", kind="phase_end", **row)
        if row["status"] != "PASS":
            raise StopRun("coverage/intensity failed: " + phase)
        return row

    def run_smoke(self):
        """Three seeded, meaningful cycles; no BT/TTS/MQTT admission or calls."""
        start = time.monotonic()
        active_end = self.deadline - 90
        api_reads = 0
        for cycle in range(3):
            cases = list(SMOKE_CASES)
            self.rng.shuffle(cases)
            self.journal.write("events.jsonl", kind="smoke_cycle", number=cycle + 1,
                               order=[x.name for x in cases])
            for case in cases:
                if time.monotonic() + 60 >= active_end:
                    break
                self.execute(case)
                self.api_probe()
                api_reads += 1
                self.wait(min(MIN_ACTION_GAP, max(0, active_end - time.monotonic())))
            if time.monotonic() >= active_end:
                break
        while time.monotonic() < active_end:
            self.wait(min(2, active_end - time.monotonic()))
        seen = {s.name: self.coverage.scenarios[s.name] for s in SMOKE_CASES}
        interactions = self.ws.commands_sent + api_reads + self.coverage.scenarios["ws_reconnect"]
        all_seen = all(seen.values())
        no_failures = not any(self.coverage.failed.values()) and self.ws.commands_failed == 0
        qualified = all_seen and no_failures and interactions >= 50
        self.journal.write("events.jsonl", kind="smoke_coverage", scenarios=seen,
                           interactions=interactions, commands=self.ws.commands_sent,
                           apiProbes=api_reads, wsReconnects=self.coverage.scenarios["ws_reconnect"],
                           activeSeconds=round(time.monotonic() - start, 3), qualified=qualified)
        return qualified

    def run(self):
        if not self.args.safe_volume_confirmed:
            raise StopRun("safe volume was not separately confirmed")
        OUT_ROOT.mkdir(parents=True, exist_ok=True)
        owner = OUT_ROOT.parent / "owner.lock"
        with owner.open("a+") as lock, (Path.home() / "voxone-hil" / "chaos-soak-8h" / "runner.lock").open("a+") as old_lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            fcntl.flock(old_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            run_id = datetime.now().strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8]
            self.journal = Journal(OUT_ROOT / run_id)
            self.journal.write("events.jsonl", kind="start", seed=self.args.seed, duration=self.args.duration)
            self.deadline = time.monotonic() + self.args.duration
            original_handler = signal.getsignal(signal.SIGINT)
            signal.signal(signal.SIGINT, lambda *_: self.stopping.set())
            try:
                with urllib.request.urlopen("http://" + HOST + "/api/ready", timeout=4) as r:
                    self.initial_ready = json.load(r)
                if (not self.initial_ready.get("ready") or
                        self.initial_ready.get("ota", {}).get("imageState") != "valid" or
                        self.initial_ready.get("www", {}).get("status") != "READY"):
                    raise StopRun("L0 READY/OTA/WWW preflight failed")
                with urllib.request.urlopen("http://" + HOST + "/data/stations.tsv", timeout=4) as r:
                    playlist = r.read()
                self.playlist_sha = hashlib.sha256(playlist).hexdigest()
                with urllib.request.urlopen("http://" + HOST + "/api/config/backup", timeout=4) as r:
                    self.config_sha = hashlib.sha256(r.read()).hexdigest()
                with urllib.request.urlopen("http://" + HOST + "/api/security/state", timeout=4) as r:
                    self.initial_security = json.load(r).get("state")
                if self.initial_security != "open":
                    raise StopRun("SECURITY not OPEN")
                lines = playlist.decode("utf-8-sig").splitlines()[1:]
                if len(lines) < 7 or not all("mp3" in lines[i-1].lower() or i == 7
                                             for i in self.SAFE_STATIONS):
                    raise StopRun("safe MP3 stations not confirmed in playlist")
                self.ws = WsSession(self.journal)
                self.initial = self.ws.snapshot()
                if (self.initial["systemInfo"].get("build") != "bf1d147" or
                        self.initial["webStatus"].get("activeSource") != "radio" or
                        self.initial["playerwrap"] != "playing" or
                        self.initial["volume100"] > 20 or
                        (self.args.smoke and self.initial["volume100"] < 2)):
                    raise StopRun("identity/source/PLAY/safe volume preflight failed")
                self.journal.write("events.jsonl", kind="preflight", state=self.initial,
                                   ready=self.initial_ready, playlistSha256=self.playlist_sha,
                                   configBackupSha256=self.config_sha, security=self.initial_security)
                caps = {"radio", "ws"}
                if self.args.smoke and any((self.args.bt_tone, self.args.ha_token_fd is not None,
                                            self.args.mqtt_auth_fd is not None)):
                    raise StopRun("smoke forbids BT/TTS/MQTT adapters")
                if self.args.bt_tone and not self.initial["webStatus"].get("btConnected"):
                    try:
                        self.bt = BlueAlsaSource(self.args.bt_tone, self.journal)
                        caps.add("bt")
                    except Exception as exc:
                        self.journal.write("events.jsonl", kind="bt_blocked", reason=str(exc))
                elif self.args.bt_tone:
                    self.journal.write("events.jsonl", kind="bt_blocked", reason="another BT peer already connected")
                if self.args.ha_token_fd is not None:
                    try:
                        self.ha = HomeAssistantPublisher(self.args.ha_token_fd, self.journal)
                        caps.add("tts")
                    except Exception as exc:
                        self.journal.write("events.jsonl", kind="tts_blocked", reason=type(exc).__name__)
                if self.args.mqtt_auth_fd is not None:
                    try:
                        self.mqtt_auth_raw = os.read(self.args.mqtt_auth_fd, 8192)
                        probe = MqttObserver(self.journal, auth_raw=self.mqtt_auth_raw)
                        probe.close()
                        caps.add("mqtt")
                    except Exception as exc:
                        self.journal.write("events.jsonl", kind="mqtt_blocked", reason=type(exc).__name__)
                budgets, blocked = plan(self.args.duration - 90, caps)
                self.journal.write("events.jsonl", kind="admission", caps=sorted(caps), budgets=budgets,
                                   blocked=blocked)
                self.telemetry = Telemetry(self.journal, self.initial_ready["bootId"], self.args.duration)
                self.telemetry.arm()
                if self.args.smoke:
                    self.result = "SMOKE_PASS" if self.run_smoke() else "SMOKE_PARTIAL"
                else:
                    for phase, seconds in budgets.items():
                        self.check()
                        self.journal.write("events.jsonl", kind="phase_start", phase=phase, seconds=seconds)
                        self.run_phase(phase, seconds)
                    self.result = "PASS" if not blocked else "PARTIAL"
            except Exception as exc:
                self.result = "STOPPED"
                self.journal.write("events.jsonl", kind="STOP", reason=str(exc))
            finally:
                signal.signal(signal.SIGINT, original_handler)
                if self.bt:
                    try:
                        self.bt.stop()
                        if self.bt.connected_by_us:
                            self.bt.disconnect()
                    except Exception as exc:
                        self.journal.write("events.jsonl", kind="bt_cleanup_error", reason=str(exc))
                restored = False
                if self.ws and self.initial:
                    try:
                        with urllib.request.urlopen("http://" + HOST + "/api/ready", timeout=4) as r:
                            ready = json.load(r)
                        if ready.get("ready") and ready.get("bootId") == self.initial_ready["bootId"]:
                            restore(self.ws, self.initial, self.journal)
                            restored = True
                        else:
                            self.journal.write("events.jsonl", kind="restore_blocked", reason="READY/bootId changed")
                    except Exception as exc:
                        self.journal.write("events.jsonl", kind="restore_error", reason=str(exc))
                if self.mqtt:
                    self.mqtt.close()
                if self.ws:
                    try:
                        self.ws.close()
                    except Exception:
                        pass
                if self.telemetry:
                    try:
                        uart_verified = self.telemetry.close()
                        if not uart_verified:
                            self.result = "STOPPED/UART_UNVERIFIED"
                        if self.telemetry.failure:
                            self.result = "STOPPED/TELEMETRY"
                    except Exception as exc:
                        self.result = "STOPPED/UART_UNVERIFIED"
                        self.journal.write("events.jsonl", kind="uart_cleanup_error", error=type(exc).__name__)
                if self.initial_ready:
                    try:
                        with urllib.request.urlopen("http://" + HOST + "/api/ready", timeout=4) as r:
                            final_ready = json.load(r)
                        with urllib.request.urlopen("http://" + HOST + "/data/stations.tsv", timeout=4) as r:
                            final_playlist = hashlib.sha256(r.read()).hexdigest()
                        with urllib.request.urlopen("http://" + HOST + "/api/config/backup", timeout=4) as r:
                            final_config = hashlib.sha256(r.read()).hexdigest()
                        with urllib.request.urlopen("http://" + HOST + "/api/security/state", timeout=4) as r:
                            final_security = json.load(r).get("state")
                        post_ok = (final_ready.get("ready") is True and
                                   final_ready.get("bootId") == self.initial_ready.get("bootId") and
                                   final_ready.get("ota", {}).get("imageState") == "valid" and
                                   final_ready.get("www", {}).get("status") == "READY" and
                                   (self.playlist_sha is None or final_playlist == self.playlist_sha) and
                                   (self.config_sha is None or final_config == self.config_sha) and
                                   (self.initial_security is None or final_security == self.initial_security))
                        self.journal.write("events.jsonl", kind="postcheck", ok=post_ok, ready=final_ready,
                                           playlistSha256=final_playlist, configBackupSha256=final_config,
                                           security=final_security)
                        if not post_ok:
                            self.result = "STOPPED/POSTCHECK"
                    except Exception as exc:
                        self.result = "STOPPED/POSTCHECK_UNVERIFIED"
                        self.journal.write("events.jsonl", kind="postcheck", ok=False, error=type(exc).__name__)
                if not restored:
                    self.result = "STOPPED/RESTORE_UNVERIFIED"
                self.journal.finish({"result": self.result, "seed": self.args.seed,
                                     "durationLimitSeconds": self.args.duration,
                                     "commands": self.ws.commands_sent if self.ws else 0,
                                     "failedCommands": self.ws.commands_failed if self.ws else 0,
                                     "scenarioCounts": dict(self.coverage.scenarios),
                                     "failedScenarios": dict(self.coverage.failed),
                                     "restored": restored})
        return 0 if self.result in ("PASS", "PARTIAL", "SMOKE_PASS", "SMOKE_PARTIAL") else 2


def dry_run(seed: int, duration: int, capabilities: set[str] | None = None):
    rng = random.Random(seed)
    capabilities = {"radio", "ws"} if capabilities is None else capabilities
    if duration < 180:
        raise ValueError("duration must leave 90 s cleanup reserve")
    budgets, blocked = plan(duration - 90, capabilities)
    coverage = Coverage()
    for phase, seconds in budgets.items():
        cases = [s for s in SCENARIOS if s.phase == phase]
        for case in cases:
            coverage.record(phase, case.name, True)
        extra = max(0, int(seconds / 45) - len(cases))
        for _ in range(extra):
            coverage.record(phase, rng.choice(cases).name, True)
    results = [coverage.phase_result(p, budgets[p]) for p in budgets]
    assert all(x["status"] == "PASS" for x in results)
    assert all(p not in budgets for p in blocked)
    weak = Coverage()
    weak.record("RADIO", "radio_stop_play", True)
    assert weak.phase_result("RADIO", 5400)["status"] == "PARTIAL"
    return {"mode": "dry-run", "seed": seed, "duration": duration,
            "cleanup_reserve_seconds": 90,
            "planned_scenarios": len(SCENARIOS), "active_scenarios": sum(s.phase in budgets for s in SCENARIOS),
            "phase_budgets": budgets, "blocked": blocked, "results": results,
            "full_coverage": not blocked, "stop_cleanup_probe": "see unit tests"}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--simulate-all-capabilities", action="store_true",
                   help="offline planner simulation only; does not assert hardware readiness")
    p.add_argument("--live", action="store_true")
    p.add_argument("--smoke", action="store_true", help="bounded RADIO/WS/volume/MUTE live smoke only")
    p.add_argument("--confirm-live", action="store_true")
    p.add_argument("--duration", type=int, default=900)
    p.add_argument("--seed", type=int, default=16328847690973426075)
    p.add_argument("--safe-volume-confirmed", action="store_true")
    p.add_argument("--ha-token-fd", type=int, help="already-authorized HA token on inherited FD")
    p.add_argument("--mqtt-auth-fd", type=int, help="explicitly provided JSON username/password on inherited FD")
    p.add_argument("--bt-tone", type=Path, help="verified local synthetic 48 kHz PCM WAV")
    args = p.parse_args()
    if args.dry_run or not args.live:
        caps = {"radio", "ws", "bt", "tts", "mqtt"} if args.simulate_all_capabilities else None
        print(json.dumps(dry_run(args.seed, args.duration, caps), ensure_ascii=False, indent=2))
        return 0
    if not args.confirm_live:
        p.error("live mode requires separate approval and --confirm-live")
    if not 600 <= args.duration <= 28800:
        p.error("live duration must be 600..28800 s, including 90 s cleanup reserve")
    if args.smoke and args.duration > 900:
        p.error("smoke duration must be at most 900 s")
    return LiveRunner(args).run()


if __name__ == "__main__":
    raise SystemExit(main())
