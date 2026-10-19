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


def open_uart_rx_only(path: Path) -> int:
    fd = os.open(path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        clear_dtr_rts(fd)
        configure_uart(fd)
        clear_dtr_rts(fd)
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


def log_chunk(handle: Any, chunk: bytes) -> None:
    record = {"at": timestamp(), "bytes": len(chunk), "hex": chunk.hex()}
    handle.write(json.dumps(record, separators=(",", ":")) + "\n")
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
    output.mkdir(parents=True, exist_ok=False)
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
    byte_count = 0
    chunk_count = 0
    boot_ids = []
    if before.get("bootId"):
        boot_ids.append(before["bootId"])
    deadline = time.monotonic() + args.duration
    next_http = time.monotonic()
    interrupted = False

    def stop(_signum: int, _frame: Any) -> None:
        nonlocal interrupted
        interrupted = True

    previous_sigint = signal.signal(signal.SIGINT, stop)
    previous_sigterm = signal.signal(signal.SIGTERM, stop)
    try:
        with raw_path.open("xb") as raw, chunks_path.open(
            "x", encoding="utf-8", newline="\n"
        ) as chunks:
            while not interrupted and time.monotonic() < deadline:
                readable, _, _ = select.select([fd], [], [], 0.25)
                if readable:
                    chunk = os.read(fd, 4096)
                    if not chunk:
                        raise LoggerError("UART disconnected")
                    raw.write(chunk)
                    raw.flush()
                    log_chunk(chunks, chunk)
                    byte_count += len(chunk)
                    chunk_count += 1
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
    manifest = {
        "openedAt": opened_at,
        "closedAt": closed_at,
        "port": str(stable_port),
        "target": str(target),
        "configuration": "115200 8N1, RX-only fd, flow control off, HUPCL off",
        "dtrRts": "cleared immediately after open and after tcsetattr; transient not excluded",
        "durationRequestedSeconds": args.duration,
        "bytes": byte_count,
        "chunks": chunk_count,
        "interrupted": interrupted,
        "before": before,
        "after": after,
        "bootIds": boot_ids,
        "bootIdChanged": len(set(boot_ids)) > 1,
    }
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(
        f"CLOSED at={closed_at} bytes={byte_count} chunks={chunk_count} "
        f"bootIdChanged={str(manifest['bootIdChanged']).lower()}",
        flush=True,
    )
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
