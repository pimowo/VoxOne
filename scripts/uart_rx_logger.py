#!/usr/bin/env python3
"""Bounded RX-only UART logger for an explicitly approved DEV observation.

Opening a tty can transiently change DTR/RTS before this process can clear
them. This tool does not make a bidirectional cable electrically RX-only.
Prefer a physical connection containing only device TX -> adapter RX and GND.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import fcntl
import json
import math
import os
from pathlib import Path
import select
import signal
import struct
import termios
import time
from typing import Any
from urllib import request


class LoggerError(RuntimeError):
    pass


UART_BAUD = 115200
UART_BITS_PER_BYTE = 10
READ_SIZE = 4096
SELECT_TIMEOUT_SECONDS = 0.25
RATE_MARGIN = 1.10
STARTUP_GRACE_SECONDS = 1.0
STARTUP_GRACE_BYTES = 4096
FORMATTED_SIZE_MULTIPLIER = 4
FORMATTED_SIZE_GRACE_BYTES = 1024 * 1024


def timestamp() -> str:
    return datetime.now().astimezone().isoformat(timespec="milliseconds")


def validate_stable_port(value: str) -> tuple[Path, Path]:
    path = Path(value)
    if path.parent != Path("/dev/serial/by-id") or not path.name:
        raise LoggerError("UART path must use /dev/serial/by-id/<device>")
    if not path.is_symlink():
        raise LoggerError(f"stable UART symlink not found: {path}")
    target = path.resolve(strict=True)
    if target.parent != Path("/dev") or not target.name.startswith(
        ("ttyUSB", "ttyACM")
    ):
        raise LoggerError(f"unexpected UART target: {target}")
    return path, target


def modem_clear_mask() -> int:
    required = ("TIOCMBIC", "TIOCM_DTR", "TIOCM_RTS")
    missing = [name for name in required if not hasattr(termios, name)]
    if missing:
        raise LoggerError("missing tty modem controls: " + ", ".join(missing))
    return termios.TIOCM_DTR | termios.TIOCM_RTS


def clear_dtr_rts(fd: int) -> None:
    mask = modem_clear_mask()
    fcntl.ioctl(fd, termios.TIOCMBIC, struct.pack("I", mask))


def configure_uart(fd: int) -> None:
    attributes = termios.tcgetattr(fd)
    attributes[0] = 0
    attributes[1] = 0
    clear_flags = termios.CSIZE | termios.PARENB | termios.CSTOPB | termios.HUPCL
    clear_flags |= getattr(termios, "CRTSCTS", 0)
    attributes[2] = (
        (attributes[2] & ~clear_flags)
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


def verify_uart_configuration(fd: int) -> None:
    attributes = termios.tcgetattr(fd)
    cflag = attributes[2]
    forbidden = termios.PARENB | termios.CSTOPB | termios.HUPCL
    forbidden |= getattr(termios, "CRTSCTS", 0)
    if attributes[4:6] != [termios.B115200, termios.B115200]:
        raise LoggerError("UART speed readback is not 115200")
    if (cflag & termios.CSIZE) != termios.CS8:
        raise LoggerError("UART character size readback is not 8 bits")
    if cflag & forbidden:
        raise LoggerError("UART readback unexpectedly enables parity/stop/flow/HUPCL")
    if not cflag & termios.CREAD or not cflag & termios.CLOCAL:
        raise LoggerError("UART readback is missing CREAD/CLOCAL")


def open_uart_rx_only(path: Path) -> int:
    fd = os.open(path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        clear_dtr_rts(fd)
        configure_uart(fd)
        clear_dtr_rts(fd)
        verify_uart_configuration(fd)
        return fd
    except Exception:
        os.close(fd)
        raise


def read_ready(host: str, timeout: float) -> dict[str, Any]:
    try:
        with request.urlopen(f"http://{host}/api/ready", timeout=timeout) as response:
            if response.status != 200:
                return {"http": response.status, "ready": False, "bootId": None}
            body = json.loads(response.read(16384))
            return {
                "http": response.status,
                "ready": body.get("ready") is True,
                "bootId": body.get("bootId"),
            }
    except Exception as error:
        return {
            "http": None,
            "ready": False,
            "bootId": None,
            "error": type(error).__name__,
        }


def serialize_chunk(chunk: bytes, at: str | None = None) -> str:
    record = {"at": at or timestamp(), "bytes": len(chunk), "hex": chunk.hex()}
    return json.dumps(record, separators=(",", ":")) + "\n"


def log_chunk(handle: Any, chunk: bytes, at: str | None = None) -> int:
    serialized = serialize_chunk(chunk, at)
    handle.write(serialized)
    handle.flush()
    return len(serialized.encode("utf-8"))


def physical_byte_budget(elapsed_seconds: float) -> int:
    """Conservative maximum bytes observable at 115200 8N1.

    One second plus 4096 bytes of grace permits scheduling delays and a small
    pre-existing driver buffer. It cannot legitimize a sustained stream above
    the configured line rate.
    """
    elapsed = max(0.0, elapsed_seconds) + STARTUP_GRACE_SECONDS
    bytes_per_second = UART_BAUD / UART_BITS_PER_BYTE
    return STARTUP_GRACE_BYTES + math.ceil(elapsed * bytes_per_second * RATE_MARGIN)


def session_file_limits(duration_seconds: float) -> tuple[int, int]:
    raw_limit = physical_byte_budget(duration_seconds)
    formatted_limit = (
        raw_limit * FORMATTED_SIZE_MULTIPLIER + FORMATTED_SIZE_GRACE_BYTES
    )
    return raw_limit, formatted_limit


class CaptureLedger:
    """Accounts for each read result and writes it at most once per output."""

    def __init__(
        self,
        raw: Any,
        formatted: Any,
        started_monotonic: float,
        raw_limit: int,
        formatted_limit: int,
    ) -> None:
        self.raw = raw
        self.formatted = formatted
        self.started_monotonic = started_monotonic
        self.raw_limit = raw_limit
        self.formatted_limit = formatted_limit
        self.read_calls = 0
        self.nonempty_reads = 0
        self.empty_reads = 0
        self.would_block_reads = 0
        self.read_bytes = 0
        self.raw_write_calls = 0
        self.raw_bytes = 0
        self.formatted_write_calls = 0
        self.formatted_bytes = 0
        self.unwritten_read_bytes = 0
        self.invalid_reason: str | None = None
        self.invalid_detail: dict[str, Any] | None = None

    def invalidate(self, reason: str, detail: dict[str, Any] | None = None) -> None:
        if self.invalid_reason is None:
            self.invalid_reason = reason
            self.invalid_detail = detail or {}

    def record_would_block(self) -> None:
        self.read_calls += 1
        self.would_block_reads += 1

    def record_read(self, chunk: bytes, now_monotonic: float, at: str) -> bool:
        self.read_calls += 1
        if not chunk:
            self.empty_reads += 1
            self.invalidate("uart_eof", {"readCall": self.read_calls})
            return False

        self.nonempty_reads += 1
        self.read_bytes += len(chunk)
        serialized = serialize_chunk(chunk, at)
        serialized_bytes = len(serialized.encode("utf-8"))

        if self.raw_bytes + len(chunk) > self.raw_limit:
            self.unwritten_read_bytes += len(chunk)
            self.invalidate(
                "raw_file_limit_exceeded",
                {
                    "limit": self.raw_limit,
                    "projected": self.raw_bytes + len(chunk),
                    "readCall": self.read_calls,
                },
            )
            return False
        if self.formatted_bytes + serialized_bytes > self.formatted_limit:
            self.unwritten_read_bytes += len(chunk)
            self.invalidate(
                "formatted_file_limit_exceeded",
                {
                    "limit": self.formatted_limit,
                    "projected": self.formatted_bytes + serialized_bytes,
                    "readCall": self.read_calls,
                },
            )
            return False

        raw_written = self.raw.write(chunk)
        raw_written = 0 if raw_written is None else raw_written
        self.raw.flush()
        self.raw_write_calls += 1
        self.raw_bytes += raw_written
        if raw_written != len(chunk):
            self.unwritten_read_bytes += len(chunk) - raw_written
            self.invalidate(
                "short_raw_write",
                {"expected": len(chunk), "written": raw_written},
            )
            return False

        formatted_written = self.formatted.write(serialized)
        formatted_written = 0 if formatted_written is None else formatted_written
        self.formatted.flush()
        self.formatted_write_calls += 1
        self.formatted_bytes += len(serialized[:formatted_written].encode("utf-8"))
        if formatted_written != len(serialized):
            self.invalidate(
                "short_formatted_write",
                {"expected": len(serialized), "written": formatted_written},
            )
            return False

        elapsed = max(0.0, now_monotonic - self.started_monotonic)
        allowed = physical_byte_budget(elapsed)
        if self.read_bytes > allowed:
            self.invalidate(
                "physical_throughput_exceeded",
                {
                    "elapsedSeconds": elapsed,
                    "allowedReadBytes": allowed,
                    "observedReadBytes": self.read_bytes,
                    "baud": UART_BAUD,
                    "bitsPerByte": UART_BITS_PER_BYTE,
                },
            )
            return False
        return True

    def manifest(self) -> dict[str, Any]:
        return {
            "readCalls": self.read_calls,
            "nonemptyReads": self.nonempty_reads,
            "emptyReads": self.empty_reads,
            "wouldBlockReads": self.would_block_reads,
            "readBytes": self.read_bytes,
            "rawWriteCalls": self.raw_write_calls,
            "rawBytes": self.raw_bytes,
            "formattedWriteCalls": self.formatted_write_calls,
            "formattedBytes": self.formatted_bytes,
            "unwrittenReadBytes": self.unwritten_read_bytes,
            "rawFileLimitBytes": self.raw_limit,
            "formattedFileLimitBytes": self.formatted_limit,
            "invalidReason": self.invalid_reason,
            "invalidDetail": self.invalid_detail,
        }


def open_private(path: Path, mode: str) -> Any:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if "b" in mode:
        return os.fdopen(os.open(path, flags, 0o600), mode)
    return os.fdopen(os.open(path, flags, 0o600), mode, encoding="utf-8", newline="\n")


def write_private_json(path: Path, value: dict[str, Any]) -> None:
    with open_private(path, "w") as handle:
        handle.write(json.dumps(value, indent=2, sort_keys=True) + "\n")
        handle.flush()


def run(args: argparse.Namespace) -> int:
    stable_port, target = validate_stable_port(args.port)
    if args.duration <= 0:
        raise LoggerError("--duration must be greater than zero")
    if not args.acknowledge_open_line_risk:
        raise LoggerError(
            "refusing to open tty without --acknowledge-open-line-risk; "
            "DTR/RTS can transition during open()"
        )

    output = Path(args.output_dir)
    output.mkdir(parents=True, exist_ok=False, mode=0o700)
    output.chmod(0o700)
    raw_path = output / "uart.raw"
    chunks_path = output / "uart-chunks.jsonl"
    manifest_path = output / "manifest.json"
    before = read_ready(args.host, args.http_timeout)
    opened_at = timestamp()
    print(
        f"OPENING at={opened_at} port={stable_port} target={target} "
        "mode=RX-only-fd baud=115200 format=8N1 flow=none",
        flush=True,
    )
    print(
        "WARNING DTR/RTS are cleared only after open(); transient line changes "
        "and hardware reset remain possible",
        flush=True,
    )

    fd = open_uart_rx_only(stable_port)
    boot_ids = []
    if before.get("bootId"):
        boot_ids.append(before["bootId"])
    capture_started = time.monotonic()
    deadline = capture_started + args.duration
    next_http = capture_started
    raw_limit, formatted_limit = session_file_limits(args.duration)
    interrupted = False
    ledger: CaptureLedger | None = None

    def stop(_signum: int, _frame: Any) -> None:
        nonlocal interrupted
        interrupted = True

    previous_sigint = signal.signal(signal.SIGINT, stop)
    previous_sigterm = signal.signal(signal.SIGTERM, stop)
    try:
        with open_private(raw_path, "xb") as raw, open_private(
            chunks_path, "w"
        ) as chunks:
            ledger = CaptureLedger(
                raw, chunks, capture_started, raw_limit, formatted_limit
            )
            while not interrupted and time.monotonic() < deadline:
                readable, _, _ = select.select(
                    [fd], [], [], SELECT_TIMEOUT_SECONDS
                )
                if readable:
                    try:
                        chunk = os.read(fd, READ_SIZE)
                    except BlockingIOError:
                        ledger.record_would_block()
                    else:
                        now = time.monotonic()
                        if not ledger.record_read(chunk, now, timestamp()):
                            break
                now = time.monotonic()
                if now >= next_http:
                    state = read_ready(args.host, args.http_timeout)
                    boot_id = state.get("bootId")
                    if boot_id and (not boot_ids or boot_ids[-1] != boot_id):
                        boot_ids.append(boot_id)
                    next_http = now + args.http_interval
    finally:
        os.close(fd)
        signal.signal(signal.SIGINT, previous_sigint)
        signal.signal(signal.SIGTERM, previous_sigterm)

    closed_at = timestamp()
    after = read_ready(args.host, args.http_timeout)
    if after.get("bootId") and (not boot_ids or boot_ids[-1] != after["bootId"]):
        boot_ids.append(after["bootId"])
    if ledger is None:
        raise LoggerError("capture files were not initialized")
    capture_status = "INVALID" if ledger.invalid_reason else "PASS"
    if interrupted and capture_status == "PASS":
        capture_status = "INTERRUPTED"
    manifest = {
        "openedAt": opened_at,
        "closedAt": closed_at,
        "port": str(stable_port),
        "target": str(target),
        "configuration": "115200 8N1, RX-only fd, flow control off, HUPCL off",
        "dtrRts": "cleared immediately after open and after tcsetattr; transient not excluded",
        "durationRequestedSeconds": args.duration,
        "status": capture_status,
        # Compatibility aliases retained for consumers of the first version.
        "bytes": ledger.raw_bytes,
        "chunks": ledger.raw_write_calls,
        "interrupted": interrupted,
        "before": before,
        "after": after,
        "bootIds": boot_ids,
        "bootIdChanged": len(set(boot_ids)) > 1,
        "capture": ledger.manifest(),
    }
    write_private_json(manifest_path, manifest)
    print(
        f"CLOSED at={closed_at} status={capture_status} "
        f"readBytes={ledger.read_bytes} rawBytes={ledger.raw_bytes} "
        f"formattedBytes={ledger.formatted_bytes} "
        f"bootIdChanged={str(manifest['bootIdChanged']).lower()}",
        flush=True,
    )
    if capture_status == "INVALID":
        return 3
    if capture_status == "INTERRUPTED":
        return 4
    return 2 if manifest["bootIdChanged"] else 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--host", required=True)
    parser.add_argument("--duration", required=True, type=float)
    parser.add_argument("--http-interval", type=float, default=5.0)
    parser.add_argument("--http-timeout", type=float, default=2.0)
    parser.add_argument("--acknowledge-open-line-risk", action="store_true")
    return parser.parse_args()


if __name__ == "__main__":
    try:
        raise SystemExit(run(parse_args()))
    except LoggerError as error:
        print(f"FAIL {error}", flush=True)
        raise SystemExit(1)
