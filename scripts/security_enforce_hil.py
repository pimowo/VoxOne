#!/usr/bin/env python3
"""Read-only preflight, reboot watcher and approved SECURITY-ENFORCE HIL.

The tool never uploads firmware, writes SPIFFS, changes protection settings or
resets the device.  ``verify`` does create RAM-only administrator sessions and
logs them out, so it requires an explicit confirmation flag.  Passwords and
session cookies are kept in process memory and are never printed or written.
"""

from __future__ import annotations

import argparse
import base64
import getpass
import gzip
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import select
import socket
import struct
import sys
import termios
import time
import urllib.parse
from dataclasses import dataclass, field
from enum import Enum


WS_PROTOCOL = "voxone.v1"
SESSION_COOKIE = "voxone_session"
SAFE_LOGIN_ERROR_CODES = frozenset({
    "authentication_failed",
    "body_required",
    "body_too_large",
    "internal_error",
    "invalid_mode",
    "invalid_password",
    "invalid_request",
    "invalid_state",
    "locked",
    "random_error",
    "rate_limited",
    "request_rejected",
    "service_unavailable",
    "storage_error",
    "unsupported_media_type",
})
DEFAULT_UART = (
    "/dev/serial/by-id/"
    "usb-FTDI_FT232R_USB_UART_A7039N0Z-if00-port0"
)
SENSITIVE_UART = re.compile(
    r"(?i)(password|passwd|cookie|token|session)(\s*[:=]\s*)(\S+)"
)
UART_BOOT_WORDS = re.compile(
    r"(?i)(rst:|boot:|reset|esp32|##\[boot\]|##\[ota-health\]|netserver::begin)"
)


class HilFailure(RuntimeError):
    pass


class UartState(Enum):
    NO_BYTES = "no_bytes"
    DATA_NO_BOOT = "data_no_boot"
    BOOT_EVIDENCE = "boot_evidence"
    DISCONNECTED = "disconnected"


GETINDEX_REQUIRED_TOP_LEVEL = frozenset({"current"})
GETINDEX_REQUIRED_PAYLOAD_IDS = frozenset({
    "nameset", "meta", "volume", "volume100", "muted",
    "maximumVolume", "startupMode", "startupFixedVolume",
    "bass", "middle", "trebble", "balance", "bitrate", "fmt",
    "playerwrap",
})


@dataclass
class GetIndexSnapshotEvidence:
    """Field-only GETINDEX evidence; response values are never retained."""

    text_frames: int = 0
    json_objects: int = 0
    top_level_keys: set[str] = field(default_factory=set)
    payload_ids: set[str] = field(default_factory=set)

    def feed(self, payload: bytes) -> None:
        self.text_frames += 1
        try:
            value = json.loads(payload)
        except (UnicodeDecodeError, json.JSONDecodeError):
            return
        if not isinstance(value, dict):
            return
        self.json_objects += 1
        self.top_level_keys.update(
            key for key in value.keys() if isinstance(key, str)
        )
        entries = value.get("payload")
        if not isinstance(entries, list):
            return
        for entry in entries:
            if isinstance(entry, dict) and isinstance(entry.get("id"), str):
                self.payload_ids.add(entry["id"])

    @property
    def missing_top_level(self) -> frozenset[str]:
        return GETINDEX_REQUIRED_TOP_LEVEL.difference(self.top_level_keys)

    @property
    def missing_payload_ids(self) -> frozenset[str]:
        return GETINDEX_REQUIRED_PAYLOAD_IDS.difference(self.payload_ids)

    @property
    def complete(self) -> bool:
        return not self.missing_top_level and not self.missing_payload_ids


def wait_getindex_snapshot(
    websocket: "WebSocket",
    timeout: float,
    inspect_text=None,
) -> GetIndexSnapshotEvidence:
    """Collect the independent GETINDEX frames in any order before a hard deadline."""

    evidence = GetIndexSnapshotEvidence()
    deadline = time.monotonic() + timeout
    while not evidence.complete:
        try:
            opcode, payload = websocket.receive(deadline=deadline)
        except (socket.timeout, TimeoutError):
            break
        if opcode == 0x8:
            raise HilFailure("WebSocket closed while waiting for GETINDEX snapshot")
        if opcode != 0x1:
            continue
        if inspect_text is not None:
            inspect_text(payload)
        evidence.feed(payload)
    if not evidence.complete:
        missing_top = ",".join(sorted(evidence.missing_top_level)) or "none"
        missing_ids = ",".join(sorted(evidence.missing_payload_ids)) or "none"
        raise HilFailure(
            "incomplete GETINDEX snapshot: "
            f"textFrames={evidence.text_frames} jsonObjects={evidence.json_objects} "
            f"missingTopLevel={missing_top} missingPayloadIds={missing_ids}"
        )
    return evidence


@dataclass
class UartEvidence:
    byte_count: int = 0
    line_count: int = 0
    boot_evidence: bool = False
    disconnected: bool = False
    _search_tail: str = ""

    def feed(self, chunk: bytes) -> None:
        if not chunk:
            self.disconnected = True
            return
        self.byte_count += len(chunk)
        self.line_count += chunk.count(b"\n")
        decoded = chunk.decode("utf-8", "replace")
        searchable = self._search_tail + decoded
        if UART_BOOT_WORDS.search(searchable):
            self.boot_evidence = True
        self._search_tail = searchable[-128:]

    @property
    def state(self) -> UartState:
        if self.disconnected:
            return UartState.DISCONNECTED
        if self.boot_evidence:
            return UartState.BOOT_EVIDENCE
        if self.byte_count:
            return UartState.DATA_NO_BOOT
        return UartState.NO_BYTES


def require_reboot_evidence(new_boot_id: str | None, uart: UartEvidence) -> None:
    if new_boot_id is None:
        raise HilFailure("a new bootId did not appear before timeout")
    if uart.disconnected:
        raise HilFailure("UART disconnected during reboot capture")
    if uart.byte_count == 0:
        raise HilFailure("UART produced no bytes after logger arming")
    if not uart.boot_evidence:
        raise HilFailure("UART has data but no recognized boot evidence")


def passed(label: str) -> None:
    print(f"PASS {label}", flush=True)


def require(condition: bool, label: str) -> None:
    if not condition:
        raise HilFailure(label)
    passed(label)


@dataclass
class HttpResult:
    status: int
    headers: list[tuple[str, str]]
    body: bytes

    def header_values(self, name: str) -> list[str]:
        wanted = name.lower()
        return [value for key, value in self.headers if key.lower() == wanted]


@dataclass(frozen=True)
class HttpTransportDiagnostic:
    """Bounded, secret-free evidence for an HTTP transport failure."""

    phase: str
    elapsed_ms: int
    response_status: int | None
    partial_body_bytes: int | None
    expected_more_bytes: int | None
    exception_type: str
    errno: int | None

    def format(self) -> str:
        status = str(self.response_status) if self.response_status is not None else "none"
        partial = (
            str(self.partial_body_bytes)
            if self.partial_body_bytes is not None else "unknown"
        )
        expected = (
            str(self.expected_more_bytes)
            if self.expected_more_bytes is not None else "unknown"
        )
        error_number = str(self.errno) if self.errno is not None else "none"
        return (
            f"httpTransportPhase={self.phase} elapsedMs={self.elapsed_ms} "
            f"responseStatus={status} partialBodyBytes={partial} "
            f"expectedMoreBytes={expected} exception={self.exception_type} "
            f"errno={error_number}"
        )


class HttpTransportFailure(HilFailure):
    """HTTP failure whose printable form contains no request or response data."""

    def __init__(self, diagnostic: HttpTransportDiagnostic) -> None:
        self.diagnostic = diagnostic
        super().__init__(diagnostic.format())


@dataclass(frozen=True)
class LoginHttpDiagnostic:
    status: int
    error: str
    retry_after_seconds: int | None


def login_http_diagnostic(response: HttpResult) -> LoginHttpDiagnostic:
    """Return only bounded, non-secret fields suitable for a HIL log."""

    error = "none" if response.status == 200 else "unavailable"
    try:
        value = json.loads(response.body)
    except (UnicodeDecodeError, json.JSONDecodeError):
        value = None
    if isinstance(value, dict) and "error" in value:
        candidate = value.get("error")
        error = (
            candidate if isinstance(candidate, str) and
            candidate in SAFE_LOGIN_ERROR_CODES else "unrecognized"
        )

    retry_after_seconds = None
    retry_values = response.header_values("Retry-After")
    if len(retry_values) == 1 and re.fullmatch(r"[0-9]{1,6}", retry_values[0]):
        retry_after_seconds = int(retry_values[0])
    return LoginHttpDiagnostic(response.status, error, retry_after_seconds)


def require_login_success(
    response: HttpResult,
    label: str = "administrator login",
) -> None:
    diagnostic = login_http_diagnostic(response)
    retry = (
        str(diagnostic.retry_after_seconds)
        if diagnostic.retry_after_seconds is not None else "none"
    )
    print(
        f"INFO loginHttpStatus={diagnostic.status} "
        f"loginError={diagnostic.error} retryAfterSeconds={retry}",
        flush=True,
    )
    if diagnostic.status != 200:
        raise HilFailure(
            f"{label}: HTTP {diagnostic.status}, error={diagnostic.error}"
        )
    passed(label)


def remove_input_line_ending(value: str) -> str:
    """Remove one transport line ending while preserving all other characters."""

    if value.endswith("\r\n"):
        return value[:-2]
    if value.endswith("\r") or value.endswith("\n"):
        return value[:-1]
    return value


def login_form_body(password: str) -> bytes:
    return urllib.parse.urlencode({"password": password}).encode("utf-8")


class Device:
    def __init__(self, host: str, port: int, timeout: float) -> None:
        self.host = host
        self.port = port
        self.timeout = timeout
        authority = host if port == 80 else f"{host}:{port}"
        self.authority = authority
        self.origin = f"http://{authority}"

    def http(
        self,
        method: str,
        path: str,
        *,
        body: bytes | None = None,
        headers: dict[str, str] | None = None,
        host_header: str | None = None,
        limit: int = 512 * 1024,
    ) -> HttpResult:
        started = time.monotonic()
        phase = "create_connection"
        response_status = None
        connection = None
        try:
            connection = http.client.HTTPConnection(
                self.host, self.port, timeout=self.timeout
            )
            request_headers = dict(headers or {})
            request_headers.setdefault("Connection", "close")
            phase = "send_request"
            connection.putrequest(
                method, path, skip_host=True, skip_accept_encoding=True
            )
            connection.putheader("Host", host_header or self.authority)
            for name, value in request_headers.items():
                connection.putheader(name, value)
            if body is not None:
                connection.putheader("Content-Length", str(len(body)))
            connection.endheaders(body)
            phase = "await_response_headers"
            response = connection.getresponse()
            response_status = response.status
            phase = "read_response_body"
            data = response.read(limit + 1)
            result = HttpResult(response.status, response.getheaders(), data)
        except (OSError, http.client.HTTPException) as exc:
            partial = None
            expected = None
            if isinstance(exc, http.client.IncompleteRead):
                partial = len(exc.partial)
                expected = exc.expected if isinstance(exc.expected, int) else None
            error_number = getattr(exc, "errno", None)
            if not isinstance(error_number, int):
                error_number = None
            elapsed_ms = max(0, int((time.monotonic() - started) * 1000))
            raise HttpTransportFailure(HttpTransportDiagnostic(
                phase=phase,
                elapsed_ms=elapsed_ms,
                response_status=response_status,
                partial_body_bytes=partial,
                expected_more_bytes=expected,
                exception_type=type(exc).__name__,
                errno=error_number,
            )) from None
        finally:
            if connection is not None:
                connection.close()
        if len(data) > limit:
            raise HilFailure(f"HTTP response exceeds {limit} bytes: {path}")
        return result

    def ready(self) -> tuple[int, str | None, bool]:
        try:
            response = self.http("GET", "/api/ready", limit=4096)
            value = json.loads(response.body)
            boot_id = value.get("bootId") if isinstance(value, dict) else None
            ready = value.get("ready") is True if isinstance(value, dict) else False
            return (
                response.status,
                boot_id if isinstance(boot_id, str) else None,
                ready,
            )
        except (OSError, ValueError, http.client.HTTPException,
                HttpTransportFailure):
            return 0, None, False


def json_object(response: HttpResult, label: str) -> dict:
    try:
        value = json.loads(response.body)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise HilFailure(f"{label}: invalid JSON") from exc
    if not isinstance(value, dict):
        raise HilFailure(f"{label}: JSON is not an object")
    return value


def decoded_entity(response: HttpResult, label: str) -> bytes:
    encoding = ",".join(response.header_values("Content-Encoding")).lower()
    if "gzip" in encoding or response.body.startswith(b"\x1f\x8b"):
        try:
            return gzip.decompress(response.body)
        except (OSError, EOFError) as exc:
            raise HilFailure(f"{label}: invalid gzip entity") from exc
    return response.body


def cookie_from_login(response: HttpResult) -> tuple[str, str]:
    values = response.header_values("Set-Cookie")
    require(len(values) == 1, "login returns one Set-Cookie header")
    raw = values[0]
    parts = [part.strip() for part in raw.split(";")]
    name, separator, token = parts[0].partition("=")
    require(
        separator == "=" and name == SESSION_COOKIE and
        re.fullmatch(r"[0-9a-f]{32}", token) is not None,
        "session cookie has the expected opaque format",
    )
    attributes = {part.lower() for part in parts[1:]}
    require("path=/" in attributes, "session cookie Path is root")
    require("httponly" in attributes, "session cookie is HttpOnly")
    require("samesite=strict" in attributes, "session cookie is SameSite=Strict")
    require("max-age=86400" in attributes, "session cookie lifetime is bounded")
    return f"{name}={token}", token


def websocket_handshake(
    device: Device,
    *,
    origin: str,
    cookie: str | None = None,
    host_header: str | None = None,
    key: str | None = None,
    protocol: str | None = WS_PROTOCOL,
) -> tuple[int, dict[str, str], socket.socket | None, bytes]:
    sock = socket.create_connection((device.host, device.port), device.timeout)
    sock.settimeout(device.timeout)
    ws_key = key if key is not None else base64.b64encode(os.urandom(16)).decode()
    lines = [
        "GET /ws HTTP/1.1",
        f"Host: {host_header or device.authority}",
        "Upgrade: websocket",
        "Connection: Upgrade",
        "Sec-WebSocket-Version: 13",
        f"Sec-WebSocket-Key: {ws_key}",
        f"Origin: {origin}",
    ]
    if protocol is not None:
        lines.append(f"Sec-WebSocket-Protocol: {protocol}")
    if cookie is not None:
        lines.append(f"Cookie: {cookie}")
    sock.sendall(("\r\n".join(lines) + "\r\n\r\n").encode("ascii"))
    received = bytearray()
    while b"\r\n\r\n" not in received and len(received) <= 8192:
        chunk = sock.recv(2048)
        if not chunk:
            break
        received.extend(chunk)
    if b"\r\n\r\n" not in received:
        sock.close()
        raise HilFailure("incomplete WebSocket HTTP response")
    raw_header, remainder = bytes(received).split(b"\r\n\r\n", 1)
    lines = raw_header.decode("latin-1", "replace").splitlines()
    match = re.match(r"HTTP/1\.[01] (\d{3})", lines[0] if lines else "")
    if not match:
        sock.close()
        raise HilFailure("invalid WebSocket HTTP status")
    status = int(match.group(1))
    response_headers: dict[str, str] = {}
    for line in lines[1:]:
        name, separator, value = line.partition(":")
        if separator:
            response_headers[name.strip().lower()] = value.strip()
    if status != 101:
        sock.close()
        sock = None
    return status, response_headers, sock, remainder


class WebSocket:
    def __init__(self, sock: socket.socket, buffered: bytes = b"") -> None:
        self.sock = sock
        self.buffer = bytearray(buffered)

    def _take(self, size: int, deadline: float | None = None) -> bytes:
        while len(self.buffer) < size:
            if deadline is not None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise socket.timeout("WebSocket receive deadline expired")
                self.sock.settimeout(remaining)
            chunk = self.sock.recv(max(2048, size - len(self.buffer)))
            if not chunk:
                raise HilFailure("WebSocket closed before a complete frame")
            self.buffer.extend(chunk)
        result = bytes(self.buffer[:size])
        del self.buffer[:size]
        return result

    def _send_masked_frame(self, opcode: int, payload: bytes) -> None:
        mask = os.urandom(4)
        header = bytearray([0x80 | opcode])
        if len(payload) < 126:
            header.append(0x80 | len(payload))
        elif len(payload) <= 0xFFFF:
            header.append(0x80 | 126)
            header.extend(struct.pack("!H", len(payload)))
        else:
            raise HilFailure("WebSocket test payload is unexpectedly large")
        masked = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
        self.sock.sendall(bytes(header) + mask + masked)

    def send_text(self, text: str) -> None:
        self._send_masked_frame(0x1, text.encode("utf-8"))

    def receive(self, deadline: float | None = None) -> tuple[int, bytes]:
        first, second = self._take(2, deadline)
        opcode = first & 0x0F
        size = second & 0x7F
        if size == 126:
            size = struct.unpack("!H", self._take(2, deadline))[0]
        elif size == 127:
            size = struct.unpack("!Q", self._take(8, deadline))[0]
        mask = self._take(4, deadline) if second & 0x80 else None
        payload = self._take(size, deadline)
        if mask:
            payload = bytes(
                value ^ mask[index % 4] for index, value in enumerate(payload)
            )
        return opcode, payload

    def wait_json(self, required_key: str, timeout: float) -> dict:
        self.sock.settimeout(timeout)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            opcode, payload = self.receive()
            if opcode == 0x8:
                raise HilFailure("WebSocket closed while waiting for JSON")
            if opcode != 0x1:
                continue
            try:
                value = json.loads(payload)
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            if isinstance(value, dict) and required_key in value:
                return value
        raise HilFailure(f"WebSocket JSON key not received: {required_key}")

    def wait_close_code(self, timeout: float) -> int | None:
        self.sock.settimeout(timeout)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                opcode, payload = self.receive()
            except (socket.timeout, TimeoutError):
                return None
            except HilFailure:
                return None
            if opcode == 0x8:
                return struct.unpack("!H", payload[:2])[0] if len(payload) >= 2 else 1005
        return None

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def close_gracefully(self, timeout: float) -> int | None:
        """Complete a client-initiated normal WS close before releasing TCP."""

        code = None
        try:
            self._send_masked_frame(0x8, struct.pack("!H", 1000))
            code = self.wait_close_code(timeout)
            if code != 1000:
                raise HilFailure(
                    "WebSocket normal close handshake did not return code 1000"
                )
            try:
                self.sock.shutdown(socket.SHUT_WR)
            except OSError:
                pass
            return code
        finally:
            self.close()


def open_ws(device: Device, cookie: str) -> WebSocket:
    status, headers, sock, buffered = websocket_handshake(
        device, origin=device.origin, cookie=cookie
    )
    require(status == 101 and sock is not None, "authorized WebSocket handshake")
    require(
        headers.get("sec-websocket-protocol") == WS_PROTOCOL,
        "WebSocket subprotocol negotiated",
    )
    return WebSocket(sock, buffered)


def login(device: Device, password: str) -> str:
    body = login_form_body(password)
    response = device.http(
        "POST",
        "/api/security/login",
        body=body,
        headers={
            "Origin": device.origin,
            "Content-Type": "application/x-www-form-urlencoded",
        },
        limit=4096,
    )
    require_login_success(response)
    cookie, _token = cookie_from_login(response)
    return cookie


def logout(device: Device, cookie: str) -> None:
    response = device.http(
        "POST",
        "/api/security/logout",
        body=b"",
        headers={"Origin": device.origin, "Cookie": cookie},
        limit=4096,
    )
    require(response.status == 204, "administrator logout")
    values = response.header_values("Set-Cookie")
    require(
        len(values) == 1 and f"{SESSION_COOKIE}=;" in values[0] and
        "Max-Age=0" in values[0],
        "logout expires the browser cookie",
    )


def best_effort_logout(device: Device, cookie: str | None) -> None:
    if not cookie:
        return
    try:
        device.http(
            "POST",
            "/api/security/logout",
            body=b"",
            headers={"Origin": device.origin, "Cookie": cookie},
            limit=4096,
        )
    except (OSError, socket.timeout, http.client.HTTPException, HilFailure):
        pass


def preflight(device: Device) -> None:
    ready = device.http("GET", "/api/ready", limit=4096)
    require(ready.status == 200, "readiness endpoint reachable")
    ready_state = json_object(ready, "readiness")
    boot_id = ready_state.get("bootId")
    require(
        ready_state.get("ready") is True and
        isinstance(boot_id, str) and bool(re.fullmatch(r"[0-9a-fA-F]{8}", boot_id)),
        "readiness includes a valid bootId",
    )
    state_response = device.http("GET", "/api/security/state", limit=4096)
    require(state_response.status == 200, "security state endpoint reachable")
    state = json_object(state_response, "security state")
    ota = ready_state.get("ota")
    ota_summary = "unavailable"
    if isinstance(ota, dict):
        ota_summary = "{}/{}/{}".format(
            ota.get("runningSlot"), ota.get("imageState"), ota.get("health")
        )
    print(
        "INFO bootId={} securityState={} authenticated={} protectionEnforced={} ota={}".format(
            boot_id,
            state.get("state"),
            bool(state.get("authenticated")),
            bool(state.get("protectionEnforced")),
            ota_summary,
        )
    )


def ota_preflight(device: Device) -> None:
    ready = device.http("GET", "/api/ready", limit=4096)
    require(ready.status == 200, "read-only OTA diagnostics are reachable")
    state = json_object(ready, "OTA readiness")
    require(state.get("ready") is True, "currently running image is healthy")
    boot_id = state.get("bootId")
    require(
        isinstance(boot_id, str) and bool(re.fullmatch(r"[0-9a-fA-F]{8}", boot_id)),
        "OTA preflight has a valid bootId",
    )
    ota = state.get("ota")
    require(isinstance(ota, dict), "firmware exposes OTA slot diagnostics")
    running = ota.get("runningSlot")
    boot = ota.get("bootSlot")
    require(running in ("app0", "app1"), "running slot is an OTA application slot")
    require(boot == running, "configured boot slot matches the running slot")
    require(ota.get("imageState") == "valid", "running image is marked VALID")
    require(ota.get("pending") is False, "no earlier image awaits validation")
    require(
        ota.get("health") in ("not_pending", "validated"),
        "health gate is in a terminal healthy state",
    )
    require(ota.get("reason") == "none", "health gate reports no current failure")
    print(
        "INFO bootId={} runningSlot={} bootSlot={} imageState={} "
        "rollbackCandidateNow={} lastFailure={}".format(
            boot_id, running, boot, ota.get("imageState"),
            bool(ota.get("rollbackAvailable")), ota.get("lastFailure")
        )
    )


def verify_rollback(
    device: Device,
    previous_boot_id: str,
    expected_slot: str,
    expected_failure: str,
) -> None:
    ready = device.http("GET", "/api/ready", limit=4096)
    require(ready.status == 200, "rolled-back device is reachable")
    state = json_object(ready, "rollback readiness")
    boot_id = state.get("bootId")
    require(
        state.get("ready") is True and isinstance(boot_id, str),
        "rolled-back image reports ready",
    )
    require(boot_id != previous_boot_id, "rollback produced a new bootId")
    ota = state.get("ota")
    require(isinstance(ota, dict), "rolled-back image exposes OTA diagnostics")
    require(
        ota.get("runningSlot") == expected_slot and
        ota.get("bootSlot") == expected_slot,
        "running and boot slots returned to the expected previous slot",
    )
    require(
        ota.get("imageState") == "valid" and ota.get("pending") is False,
        "previous image remains VALID with no pending validation",
    )
    require(
        ota.get("lastFailure") == expected_failure,
        "persistent rollback reason matches the injected failure",
    )
    print(
        f"INFO rollbackBootId={boot_id} slot={expected_slot} "
        f"lastFailure={expected_failure}"
    )


def parse_partition_size(value: str) -> int:
    cleaned = value.strip()
    return int(cleaned, 16 if cleaned.lower().startswith("0x") else 10)


def rollback_audit(project_root: Path, framework_root: Path, environment: str) -> None:
    partition_file = project_root / "partitions_yoradio.csv"
    rows: dict[str, tuple[str, str, int, int]] = {}
    for raw in partition_file.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) < 5:
            raise HilFailure(f"invalid partition row: {line}")
        rows[fields[0]] = (
            fields[1], fields[2], parse_partition_size(fields[3]),
            parse_partition_size(fields[4])
        )
    require("otadata" in rows and rows["otadata"][3] == 0x2000,
            "partition table has an 8 KiB otadata partition")
    require("app0" in rows and "app1" in rows,
            "partition table has two OTA application slots")
    require(rows["app0"][1] == "ota_0" and rows["app1"][1] == "ota_1",
            "application slots use ota_0 and ota_1 subtypes")
    require(rows["app0"][3] == rows["app1"][3],
            "OTA application slots have equal size")

    sdkconfigs = (
        framework_root / "tools/sdk/esp32/qio_qspi/include/sdkconfig.h",
        framework_root / "tools/sdk/esp32s3/qio_qspi/include/sdkconfig.h",
        framework_root / "tools/sdk/esp32s3/qio_opi/include/sdkconfig.h",
    )
    require(
        all(
            "#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1" in
            path.read_text(encoding="utf-8")
            for path in sdkconfigs
        ),
        "ESP32 and ESP32-S3 target SDKs compile bootloader rollback",
    )

    build = project_root / ".pio/build" / environment
    firmware = build / "firmware.bin"
    firmware_map = build / "firmware.map"
    require(firmware.is_file() and firmware.stat().st_size <= rows["app0"][3],
            "firmware artifact fits either OTA slot")
    map_text = firmware_map.read_text(encoding="utf-8", errors="replace")
    require("esp_ota_mark_app_valid_cancel_rollback" in map_text,
            "linked firmware contains the rollback validation API")
    require(".pio/build/" + environment + "/libFrameworkArduino.a(esp32-hal-misc.c.o)" in map_text,
            "Arduino rollback hook implementation is linked")

    hal_source = framework_root / "cores/esp32/esp32-hal-misc.c"
    hal_text = hal_source.read_text(encoding="utf-8")
    default_accepts = bool(re.search(
        r"bool\s+verifyOta\s*\(\s*\)\s*\{\s*return\s+true\s*;\s*\}",
        hal_text,
    ))
    marks_in_init = (
        "ota_state == ESP_OTA_IMG_PENDING_VERIFY" in hal_text and
        "esp_ota_mark_app_valid_cancel_rollback();" in hal_text
    )
    require(default_accepts and marks_in_init,
            "Arduino default marks PENDING_VERIFY valid during initArduino")

    updater_text = (
        framework_root / "libraries/Update/src/Updater.cpp"
    ).read_text(encoding="utf-8")
    require(
        "esp_ota_set_boot_partition(_partition)" in updater_text and
        "return _verifyEnd();" in updater_text and
        "if(!isFinished() && !evenIfRemaining)" in updater_text,
        "Arduino Update selects the new boot slot only after final verification",
    )

    runtime_source = project_root / "src/core/ota_boot_runtime.cpp"
    runtime_text = runtime_source.read_text(encoding="utf-8")
    require(
        bool(re.search(
            r'extern\s+"C"\s+bool\s+verifyRollbackLater\s*\(\s*\)\s*'
            r'\{\s*return\s+true\s*;\s*\}', runtime_text
        )),
        "VoxOne supplies a strong delayed-validation Arduino hook",
    )
    require("bool verifyOta(" not in runtime_text,
            "VoxOne does not reuse Arduino's eager verifier")
    require(
        f".pio/build/{environment}/src/core/ota_boot_runtime.cpp.o" in map_text and
        "verifyRollbackLater" in map_text,
        "linked firmware resolves verifyRollbackLater from VoxOne",
    )
    require(
        all(name in runtime_text for name in (
            "esp_ota_get_state_partition", "esp_ota_check_rollback_is_possible",
            "esp_ota_mark_app_valid_cancel_rollback",
            "esp_ota_mark_app_invalid_rollback_and_reboot",
        )),
        "runtime links explicit state, validation and rollback operations",
    )
    config_text = (project_root / "src/core/config.cpp").read_text(encoding="utf-8")
    require("SPIFFS.begin(false)" in config_text and
            "SPIFFS.begin(true)" not in config_text,
            "pending boot cannot auto-format the shared SPIFFS partition")
    digest = hashlib.sha256(firmware.read_bytes()).hexdigest()
    print(
        f"INFO artifact={firmware} size={firmware.stat().st_size} "
        f"slotSize={rows['app0'][3]} sha256={digest}"
    )


def verify(device: Device, password: str, previous_boot_id: str | None) -> None:
    ready = device.http("GET", "/api/ready", limit=4096)
    require(ready.status == 200, "public readiness remains available")
    ready_state = json_object(ready, "readiness")
    boot_id = ready_state.get("bootId")
    require(
        ready_state.get("ready") is True and isinstance(boot_id, str),
        "readiness reports a bootId",
    )
    ota = ready_state.get("ota")
    require(isinstance(ota, dict), "post-update OTA diagnostics are present")
    require(
        ota.get("imageState") == "valid" and ota.get("pending") is False and
        ota.get("health") == "validated" and ota.get("reason") == "none",
        "PENDING_VERIFY passed the health gate and became VALID",
    )
    if previous_boot_id is not None:
        require(boot_id != previous_boot_id, "bootId changed after Web Update restart")

    state_response = device.http("GET", "/api/security/state", limit=4096)
    require(state_response.status == 200, "public security state is readable")
    state = json_object(state_response, "security state")
    require(state.get("state") == "protected", "existing protected state survived update")
    require(state.get("authenticated") is False, "new process has no inherited session")
    # The dedicated test switch exercises the real gate but is reported
    # explicitly as test, never as production protection.  HIL still proves
    # enforcement independently from the HTTP/WS behavior below.
    require(
        state.get("protectionEnforced") is True and
        state.get("enforcementMode") == "test",
        "test checkpoint reports active test enforcement, not production",
    )

    root = device.http("GET", "/", limit=64 * 1024)
    require(root.status in (200, 302), "root remains available with existing SPIFFS")
    html = device.http("GET", "/voxone.html", limit=256 * 1024)
    script = device.http("GET", "/voxone.js", limit=512 * 1024)
    html_body = decoded_entity(html, "SPIFFS HTML")
    script_body = decoded_entity(script, "SPIFFS JavaScript")
    require(html.status == 200 and len(html_body) > 1000, "existing SPIFFS HTML is served")
    require(
        script.status == 200 and b"/api/security/login" in script_body and
        b"/api/security/state" in script_body and b"new WebSocket" in script_body,
        "existing SPIFFS JavaScript supports login and compatible WS startup",
    )

    protected = device.http("GET", "/api/hardware", limit=64 * 1024)
    require(protected.status == 401, "protected HTTP rejects a missing session")
    direct_gzip = device.http("GET", "/voxone.js.gz", limit=4096)
    require(direct_gzip.status == 403, "direct compressed asset is rejected")
    unknown = device.http("GET", "/security-hil-unknown", limit=4096)
    require(unknown.status == 404, "unknown route fails closed")
    foreign_host = device.http(
        "GET", "/api/security/state", host_header="evil.invalid", limit=4096
    )
    require(foreign_host.status == 403, "foreign Host is rejected")
    foreign_origin = device.http(
        "GET",
        "/api/security/state",
        headers={"Origin": "http://evil.invalid"},
        limit=4096,
    )
    require(foreign_origin.status == 403, "foreign Origin is rejected")

    status, _headers, _sock, _buffered = websocket_handshake(
        device, origin=device.origin
    )
    require(status == 401, "WebSocket rejects a missing session")
    status, _headers, _sock, _buffered = websocket_handshake(
        device, origin="http://evil.invalid"
    )
    require(status == 403, "WebSocket rejects a foreign Origin")
    status, _headers, _sock, _buffered = websocket_handshake(
        device, origin=device.origin, key="invalid"
    )
    require(status == 400, "malformed WebSocket handshake is rejected")

    rejected_login = device.http(
        "POST",
        "/api/security/login",
        body=b"password=not-used",
        headers={
            "Origin": "http://evil.invalid",
            "Content-Type": "application/x-www-form-urlencoded",
        },
        limit=4096,
    )
    require(rejected_login.status == 403, "foreign-Origin login is rejected before auth")

    cookie: str | None = None
    reconnect_cookie: str | None = None
    try:
        cookie = login(device, password)
        authenticated_state = device.http(
            "GET", "/api/security/state", headers={"Cookie": cookie}, limit=4096
        )
        require(
            json_object(authenticated_state, "authenticated state").get("authenticated") is True,
            "session authenticates HTTP",
        )
        hardware_before = device.http(
            "GET", "/api/hardware", headers={"Cookie": cookie}, limit=64 * 1024
        )
        require(hardware_before.status == 200, "authenticated protected GET succeeds")
        hardware_digest = hashlib.sha256(hardware_before.body).digest()

        ws = open_ws(device, cookie)
        ws.send_text("ping=1")
        require(ws.wait_json("pong", device.timeout).get("pong") == 1, "allowed WS ping command")
        ws.send_text("getsystem=1")
        system = ws.wait_json("systemInfo", device.timeout).get("systemInfo")
        require(
            isinstance(system, dict) and system.get("profile") == "M0",
            "allowed WS diagnostic reports M0 profile",
        )

        ws.send_text("security_hil_forbidden=1")
        require(ws.wait_close_code(device.timeout) == 1008, "disallowed WS command closes with 1008")
        ws.close()
        ready_after = device.http("GET", "/api/ready", limit=4096)
        hardware_after = device.http(
            "GET", "/api/hardware", headers={"Cookie": cookie}, limit=64 * 1024
        )
        require(
            json_object(ready_after, "readiness after rejection").get("bootId") == boot_id,
            "rejected WS command did not restart the device",
        )
        require(
            hardware_after.status == 200 and
            hashlib.sha256(hardware_after.body).digest() == hardware_digest,
            "rejected WS command did not change hardware configuration",
        )

        lifecycle_ws = open_ws(device, cookie)
        logout(device, cookie)
        require(
            lifecycle_ws.wait_close_code(device.timeout) == 1008,
            "logout invalidates an active WebSocket",
        )
        lifecycle_ws.close()
        stale = device.http(
            "GET", "/api/hardware", headers={"Cookie": cookie}, limit=4096
        )
        require(stale.status == 401, "logged-out session cannot access HTTP")
        cookie = None

        reconnect_cookie = login(device, password)
        reconnect_ws = open_ws(device, reconnect_cookie)
        reconnect_ws.send_text("ping=1")
        require(
            reconnect_ws.wait_json("pong", device.timeout).get("pong") == 1,
            "WebSocket reconnect works with a new session",
        )
        reconnect_ws.close()
        logout(device, reconnect_cookie)
        reconnect_cookie = None
    finally:
        best_effort_logout(device, cookie)
        best_effort_logout(device, reconnect_cookie)
    print(f"INFO verified bootId={boot_id}; no credentials or cookies were logged")


def configure_uart(fd: int) -> None:
    attributes = termios.tcgetattr(fd)
    attributes[0] = 0
    attributes[1] = 0
    attributes[2] = (
        (attributes[2] & ~(termios.CSIZE | termios.PARENB | termios.CSTOPB))
        | termios.CS8
        | termios.CREAD
        | termios.CLOCAL
    )
    attributes[3] = 0
    attributes[4] = termios.B115200
    attributes[5] = termios.B115200
    attributes[6][termios.VMIN] = 0
    attributes[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attributes)


def watch_reboot(
    device: Device, previous_boot_id: str, uart_path: str, duration: float
) -> None:
    fd = os.open(uart_path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    configure_uart(fd)
    termios.tcflush(fd, termios.TCIFLUSH)
    status, baseline_boot_id, baseline_ready = device.ready()
    if status != 200 or not baseline_ready or baseline_boot_id != previous_boot_id:
        os.close(fd)
        raise HilFailure(
            "logger cannot be armed: current readiness does not match --previous-boot-id"
        )
    print(f"INFO UART state=armed path={uart_path} baud=115200 mode=8N1")
    print(
        f"LOGGER ARMED bootId={previous_boot_id}; perform the separately approved "
        f"restart now (timeout {duration:.0f}s)", flush=True
    )
    deadline = time.monotonic() + duration
    next_poll = 0.0
    line_buffer = bytearray()
    saw_offline = False
    new_boot_id: str | None = None
    new_boot_seen_at: float | None = None
    uart = UartEvidence()
    try:
        while time.monotonic() < deadline:
            now = time.monotonic()
            if new_boot_seen_at is not None and now - new_boot_seen_at >= 1.0:
                break
            timeout = min(0.25, max(0.0, next_poll - now)) if next_poll else 0.25
            ready, _, _ = select.select([fd], [], [], timeout)
            if ready:
                try:
                    chunk = os.read(fd, 4096)
                except OSError:
                    uart.disconnected = True
                    break
                uart.feed(chunk)
                if not chunk:
                    break
                line_buffer.extend(chunk)
                while b"\n" in line_buffer:
                    raw, _, remainder = line_buffer.partition(b"\n")
                    line_buffer[:] = remainder
                    line = raw.decode("utf-8", "replace").rstrip("\r")
                    safe = SENSITIVE_UART.sub(r"\1\2<redacted>", line)
                    if UART_BOOT_WORDS.search(safe):
                        print("UART " + safe[:500])
            now = time.monotonic()
            if now >= next_poll:
                status, boot_id, device_ready = device.ready()
                if status != 200:
                    saw_offline = True
                elif device_ready and boot_id and boot_id != previous_boot_id:
                    new_boot_id = boot_id
                    if new_boot_seen_at is None:
                        new_boot_seen_at = now
                next_poll = now + 0.5
    finally:
        os.close(fd)
    print(
        f"INFO rebootEvidence newBootId={new_boot_id or 'none'} "
        f"offlineObserved={str(saw_offline).lower()} uartState={uart.state.value} "
        f"uartBytes={uart.byte_count} uartLines={uart.line_count}"
    )
    require_reboot_evidence(new_boot_id, uart)


def password_from_args(args: argparse.Namespace) -> str:
    if args.password_stdin:
        password = remove_input_line_ending(sys.stdin.readline())
    else:
        password = os.environ.pop(args.password_env, "")
        if not password:
            if not sys.stdin.isatty():
                raise HilFailure(
                    f"set {args.password_env} or use --password-stdin; password is never logged"
                )
            password = getpass.getpass("VoxOne administrator password: ")
    if not password:
        raise HilFailure("administrator password is empty")
    return password


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "mode", choices=(
            "preflight", "ota-preflight", "rollback-audit", "watch-reboot",
            "verify-rollback", "verify"
        )
    )
    parser.add_argument("--host", default="192.168.1.114")
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--timeout", type=float, default=4.0)
    parser.add_argument("--previous-boot-id")
    parser.add_argument("--expected-slot", choices=("app0", "app1"))
    parser.add_argument(
        "--expected-failure",
        choices=(
            "config_restore", "configuration", "data_partition",
            "runtime_service", "network", "http_api", "timeout",
            "mark_valid", "rollback_failed",
        ),
    )
    parser.add_argument("--uart", default=DEFAULT_UART)
    parser.add_argument("--watch-seconds", type=float, default=180.0)
    parser.add_argument("--password-env", default="VOXONE_HIL_PASSWORD")
    parser.add_argument("--password-stdin", action="store_true")
    parser.add_argument("--confirm-session-lifecycle", action="store_true")
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
    )
    parser.add_argument(
        "--framework-root",
        type=Path,
        default=Path.home() / ".platformio/packages/framework-arduinoespressif32",
    )
    parser.add_argument("--pio-env", default="m0-security-test")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    device = Device(args.host, args.port, args.timeout)
    try:
        if args.mode == "preflight":
            preflight(device)
        elif args.mode == "ota-preflight":
            ota_preflight(device)
        elif args.mode == "rollback-audit":
            rollback_audit(
                args.project_root.resolve(), args.framework_root.resolve(), args.pio_env
            )
        elif args.mode == "watch-reboot":
            if not args.previous_boot_id:
                raise HilFailure("watch-reboot requires --previous-boot-id")
            watch_reboot(
                device, args.previous_boot_id, args.uart, args.watch_seconds
            )
        elif args.mode == "verify-rollback":
            if not (args.previous_boot_id and args.expected_slot and
                    args.expected_failure):
                raise HilFailure(
                    "verify-rollback requires --previous-boot-id, "
                    "--expected-slot and --expected-failure"
                )
            verify_rollback(
                device, args.previous_boot_id, args.expected_slot,
                args.expected_failure,
            )
        else:
            if not args.confirm_session_lifecycle:
                raise HilFailure(
                    "verify requires --confirm-session-lifecycle because it creates and logs out RAM-only sessions"
                )
            password = password_from_args(args)
            try:
                verify(device, password, args.previous_boot_id)
            finally:
                password = ""
    except (HilFailure, OSError, socket.timeout, http.client.HTTPException) as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1
    print("RESULT PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
