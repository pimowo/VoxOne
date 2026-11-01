#!/usr/bin/env python3
"""Offline replay of a bounded HTTP/1.1 chunked+ICY capture.

The production Audio.cpp transport methods are extracted by the existing
radio_transport_native harness.  This tool never connects to a device or host.
"""

from __future__ import annotations

import argparse
from datetime import datetime
from hashlib import sha256
import importlib.util
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
HARNESS = ROOT / "test/radio_transport_native.py"
READ = re.compile(r"^(\d\d:\d\d:\d\d\.\d+) <= Recv data, (\d+) bytes")
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211


def load_harness():
    spec = importlib.util.spec_from_file_location("radio_transport_native", HARNESS)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def arrival_schedule(trace: Path, byte_count: int) -> list[tuple[int, int]]:
    reads = []
    for line in trace.read_text(errors="replace").splitlines():
        match = READ.match(line)
        if match:
            reads.append((datetime.strptime(match[1], "%H:%M:%S.%f"),
                          int(match[2])))
    if not reads or sum(length for _, length in reads) != byte_count:
        raise ValueError("trace/data byte counts disagree")
    first = reads[0][0]
    return [(round((time - first).total_seconds() * 1000), length)
            for time, length in reads]


def reference_audio(raw: bytes, metaint: int) -> tuple[bytes, list[tuple[int, int]], int]:
    """Independently dechunk and strip ICY, recording raw chunk spans."""
    payload = bytearray()
    chunks = []
    cursor = 0
    while cursor < len(raw):
        end = raw.find(b"\r\n", cursor)
        if end < 0:
            raise ValueError("incomplete chunk header")
        size = int(raw[cursor:end].split(b";", 1)[0], 16)
        start = end + 2
        if not size or start + size + 2 > len(raw):
            raise ValueError("capture must end on a complete nonfinal chunk")
        if raw[start + size:start + size + 2] != b"\r\n":
            raise ValueError("invalid chunk delimiter")
        payload.extend(raw[start:start + size])
        chunks.append((cursor, start + size + 2))
        cursor = start + size + 2

    audio = bytearray()
    cursor = 0
    metadata_count = 0
    while cursor < len(payload):
        count = min(metaint, len(payload) - cursor)
        audio.extend(payload[cursor:cursor + count])
        cursor += count
        if count < metaint or cursor == len(payload):
            break
        metadata_size = payload[cursor] * 16
        cursor += 1
        if cursor + metadata_size > len(payload):
            raise ValueError("capture ends inside ICY metadata")
        cursor += metadata_size
        metadata_count += 1
    return bytes(audio), chunks, metadata_count


def fnv1a(data: bytes) -> int:
    value = FNV_OFFSET
    for byte in data:
        value = ((value ^ byte) * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("raw", type=Path)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--metaint", type=int, default=16384)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    raw = args.raw.read_bytes()
    schedule = arrival_schedule(args.trace, len(raw))
    expected, chunks, metadata_count = reference_audio(raw, args.metaint)
    # A bounded capture can end exactly after a chunk's CRLF.  The production
    # parser then waits for the *next* header and times out because capture
    # stopped, not because the live stream failed.  Leave the final CRLF out
    # of the finite replay so it ends inside that framing transition.
    if not raw.endswith(b"\r\n") or schedule[-1][1] < 2:
        raise ValueError("capture must end on a chunk delimiter in last read")
    replay_raw = raw[:-2]
    schedule[-1] = (schedule[-1][0], schedule[-1][1] - 2)
    gaps = [b[0] - a[0] for a, b in zip(schedule, schedule[1:])]

    harness = load_harness()
    source = harness.AUDIO_CPP.read_text(encoding="utf-8")
    methods = "\n\n".join(harness.extract_function(source, signature)
                          for signature in (
                              "int Audio::readClient()",
                              "int Audio::readClient(uint8_t* buffer, size_t length)",
                              "size_t Audio::readClientBytes(char* buffer, size_t length)",
                              "uint16_t Audio::readMetadata(uint16_t maxBytes, bool first)",
                              "size_t Audio::chunkedDataTransfer(uint8_t* bytes)",
                              "void Audio::processWebStream()"))
    prefix = harness.HARNESS_PREFIX.replace(
        "std::vector<uint8_t> storage = std::vector<uint8_t>(4096);",
        "std::vector<uint8_t> storage = std::vector<uint8_t>(1048576);")
    prefix = prefix.replace(
        "static void log_e(const char*, ...) {}",
        'static void log_e(const char* fmt, ...) { va_list args; va_start(args, fmt); std::vfprintf(stderr, fmt, args); std::fputc(10, stderr); va_end(args); }')
    if prefix == harness.HARNESS_PREFIX:
        raise ValueError("host buffer adapter changed")
    suffix = r'''
int main(int argc, char** argv) {
  if (argc != 5) return 2;
  std::ifstream rawFile(argv[1], std::ios::binary);
  std::vector<uint8_t> raw((std::istreambuf_iterator<char>(rawFile)),
                           std::istreambuf_iterator<char>());
  std::ifstream timing(argv[2]);
  FakeClient client(raw);
  size_t offset = 0;
  uint32_t when = 0;
  size_t length = 0;
  while (timing >> when >> length) {
    if (offset + length > raw.size()) return 3;
    std::fill(client.release.begin() + offset,
              client.release.begin() + offset + length, when);
    offset += length;
  }
  if (offset != raw.size()) return 4;
  Audio audio(&client);
  audio.m_f_metadata = true;
  audio.m_metaint = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10));
  const size_t expectedSize = static_cast<size_t>(std::strtoull(argv[4], nullptr, 10));
  for (size_t turn = 0; turn < 1000000 && client.position < raw.size() &&
                        audio.getDatamode() == AUDIO_DATA; ++turn)
    audio.processWebStream();
  uint64_t hash = 14695981039346656037ULL;
  for (size_t i = 0; i < audio.InBuff.filled; ++i)
    hash = (hash ^ audio.InBuff.storage[i]) * 1099511628211ULL;
  std::printf("replay raw=%zu consumed=%zu audio=%zu hash=%016llx "
              "stop=%u simulatedMs=%u\n", raw.size(), client.position,
              audio.InBuff.filled, static_cast<unsigned long long>(hash),
              audio.stopCalls, fakeNow);
  return client.position == raw.size() && audio.InBuff.filled == expectedSize &&
         audio.stopCalls == 0 ? 0 : 1;
}
'''
    unit = ("#include <fstream>\n#include <iterator>\n#include <cstdlib>\n#include <cstdarg>\n" +
            prefix + "\n" + methods + "\n" + suffix)
    with tempfile.TemporaryDirectory(prefix="voxone-ahfm-replay-") as tmp:
        folder = Path(tmp)
        unit_path = folder / "replay.cpp"
        binary = folder / "replay"
        timing = folder / "schedule.tsv"
        replay_file = folder / "replay.raw"
        unit_path.write_text(unit, encoding="utf-8")
        timing.write_text("".join(f"{when} {length}\n" for when, length in schedule),
                          encoding="ascii")
        replay_file.write_bytes(replay_raw)
        compile_cmd = ["g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra",
                       "-Werror", "-Wno-unused-parameter", "-Wno-unused-value"]
        if args.sanitize:
            compile_cmd += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        compile_cmd += [str(unit_path), "-o", str(binary)]
        subprocess.run(compile_cmd, check=True, cwd=ROOT)
        run = subprocess.run([str(binary), str(replay_file), str(timing),
                              str(args.metaint), str(len(expected))],
                             check=False, cwd=ROOT, capture_output=True, text=True)
        print(f"capture raw={len(raw)} finiteTailTrim=2 reads={len(schedule)} chunks={len(chunks)} "
              f"metadata={metadata_count} audio={len(expected)} "
              f"fnv={fnv1a(expected):016x} sha256={sha256(expected).hexdigest()} "
              f"gapMaxMs={max(gaps)}")
        print(run.stdout, end="")
        if run.stderr:
            print(run.stderr, end="")
        if run.returncode:
            return run.returncode
        reported = re.search(r"hash=([0-9a-f]{16})", run.stdout)
        if not reported or int(reported[1], 16) != fnv1a(expected):
            print("FAIL: decoded byte hash differs from independent oracle")
            return 1
        print("PASS: exact dechunked/ICY audio bytes and no transport stop")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
