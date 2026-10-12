#!/usr/bin/env python3
"""Native characterization tests for the production web-radio transport.

The harness below does not carry a second chunk/ICY implementation.  It extracts
the selected Audio methods from Audio.cpp, compiles those exact method bodies
against small host-side client/buffer adapters, and then drives them with fixed
byte/time schedules.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
AUDIO_CPP = ROOT / "src/audioI2S/Audio.cpp"


def extract_function(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise RuntimeError(f"production function not found: {signature}")
    brace = source.find("{", start)
    if brace < 0:
        raise RuntimeError(f"opening brace not found: {signature}")
    depth = 0
    in_string = False
    in_char = False
    escaped = False
    for position in range(brace, len(source)):
        char = source[position]
        if escaped:
            escaped = False
            continue
        if char == "\\" and (in_string or in_char):
            escaped = True
            continue
        if char == '"' and not in_char:
            in_string = not in_string
            continue
        if char == "'" and not in_string:
            in_char = not in_char
            continue
        if in_string or in_char:
            continue
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : position + 1]
    raise RuntimeError(f"closing brace not found: {signature}")


HARNESS_PREFIX = r'''
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

static uint32_t fakeNow;
static uint32_t millis() { return fakeNow++; }
static void log_e(const char*, ...) {}
static void log_i(const char*, ...) {}
#define AUDIO_INFO(...) do {} while (false)
#define min std::min

enum : uint8_t { AUDIO_NONE, HTTP_RESPONSE_HEADER, AUDIO_DATA };

namespace voxone { namespace radio {
static bool streamIsDead(bool connected, bool buffered, uint32_t now,
                         uint32_t lastData) {
  return (!connected && !buffered) ||
         (connected && !buffered && uint32_t(now - lastData) >= 10000U);
}
}}

struct FakeClient {
  std::vector<uint8_t> bytes;
  std::vector<uint32_t> release;
  size_t position = 0;
  bool isConnected = true;
  bool stopped = false;

  explicit FakeClient(std::vector<uint8_t> input = {})
      : bytes(std::move(input)), release(bytes.size(), 0) {}

  int read() {
    if (position >= bytes.size() || uint32_t(fakeNow - release[position]) >=
                                        0x80000000U)
      return -1;
    return bytes[position++];
  }
  int16_t read(uint8_t* output, uint32_t count) {
    uint32_t copied = 0;
    while (copied < count && position < bytes.size() &&
           uint32_t(fakeNow - release[position]) < 0x80000000U)
      output[copied++] = bytes[position++];
    return static_cast<int16_t>(copied);
  }
  uint16_t readBytes(char* output, uint16_t count) {
    return static_cast<uint16_t>(read(reinterpret_cast<uint8_t*>(output), count));
  }
  uint32_t available() const {
    size_t end = position;
    while (end < bytes.size() &&
           uint32_t(fakeNow - release[end]) < 0x80000000U)
      ++end;
    return static_cast<uint32_t>(end - position);
  }
  bool connected() const { return isConnected; }
  void stop() { stopped = true; isConnected = false; }
};

struct FakeBuffer {
  std::vector<uint8_t> storage = std::vector<uint8_t>(4096);
  size_t filled = 0;
  uint16_t getMaxBlockSize() const { return 0; }
  size_t bufferFilled() const { return filled; }
  size_t writeSpace() const { return storage.size() - filled; }
  uint8_t* getWritePtr() { return storage.data() + filled; }
  void bytesWritten(size_t count) { filled += count; }
  std::string text() const {
    return std::string(reinterpret_cast<const char*>(storage.data()), filled);
  }
};

struct Audio {
  enum ChunkedState : uint8_t {
    CHUNK_SIZE, CHUNK_EXTENSION, CHUNK_SIZE_LF, CHUNK_PAYLOAD,
    CHUNK_PAYLOAD_CR, CHUNK_PAYLOAD_LF, CHUNK_TRAILER_START,
    CHUNK_TRAILER, CHUNK_TRAILER_LF, CHUNK_TRAILER_FINAL_LF,
    CHUNK_DONE, CHUNK_ERROR
  };
  explicit Audio(FakeClient* input) : _client(input) {}
  uint16_t readMetadata(uint16_t maxBytes, bool first = false);
  size_t chunkedDataTransfer(uint8_t* bytes);
  void processWebStream();

  uint8_t getDatamode() const { return m_datamode; }
  void setDatamode(uint8_t mode) { m_datamode = mode; }
  uint32_t stopSong() { ++stopCalls; setDatamode(AUDIO_NONE); return 0; }
  void playAudioData() {}
  bool latinToUTF8(char*, size_t) { return true; }
  int indexOf(const char* base, const char* token, int start = 0) {
    const char* found = std::strstr(base + start, token);
    return found ? static_cast<int>(found - base) : -1;
  }
  void showstreamtitle(const char*) {}

  FakeClient* _client;
  FakeBuffer InBuff;
  bool m_f_Log = false;
  bool m_f_firstCall = true;
  bool m_f_chunked = true;
  bool m_f_metadata = false;
  uint32_t m_metacount = 0;
  uint32_t m_metaint = 0;
  uint32_t m_chunkcount = 0;
  uint16_t m_chunkLineLength = 0;
  ChunkedState m_chunkState = CHUNK_SIZE;
  bool m_chunkHasDigits = false;
  uint8_t m_datamode = AUDIO_DATA;
  char chbuf[1024]{};
  unsigned stopCalls = 0;
};
'''


HARNESS_SUFFIX = r'''
#undef min

struct Result { const char* name; bool ok; bool knownFailure; };
static std::vector<Result> results;
static void check(const char* name, bool ok, bool knownFailure = false) {
  results.push_back({name, ok, knownFailure});
}
static std::vector<uint8_t> data(const std::string& text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}
static void pump(Audio& audio, unsigned limit = 200) {
  for (unsigned i = 0; i < limit && audio.getDatamode() == AUDIO_DATA; ++i)
    audio.processWebStream();
}

int main(int argc, char** argv) {
  const bool strict = argc > 1 && std::string(argv[1]) == "--strict";

  {
    fakeNow = 100;
    FakeClient client(data("A\r\n"));
    Audio audio(&client);
    uint8_t framingBytes = 0;
    check("valid hexadecimal chunk size",
          audio.chunkedDataTransfer(&framingBytes) == 10);
    check("chunk header byte accounting", framingBytes == 3, true);
  }
  {
    fakeNow = 100;
    FakeClient client(data("1A\r\n"));
    client.release = {100, 140, 180, 220};
    Audio audio(&client);
    uint8_t ignored = 0;
    check("chunk header split between reads",
          audio.chunkedDataTransfer(&ignored) == 26 && fakeNow >= 220);
  }
  {
    fakeNow = 100;
    FakeClient client(data("4;name=value\r\n"));
    Audio audio(&client);
    uint8_t ignored = 0;
    check("chunk extensions", audio.chunkedDataTransfer(&ignored) == 4, true);
  }
  {
    fakeNow = 100;
    FakeClient client(data("0\r\n"));
    Audio audio(&client);
    uint8_t ignored = 0;
    check("zero-length final chunk size",
          audio.chunkedDataTransfer(&ignored) == 0);
    check("zero-length final chunk terminates transport",
          audio.getDatamode() != AUDIO_DATA, true);
  }
  {
    fakeNow = 100;
    FakeClient client(data("G\r\n"));
    Audio audio(&client);
    uint8_t ignored = 0;
    const size_t parsed = audio.chunkedDataTransfer(&ignored);
    check("invalid hexadecimal chunk size rejected",
          parsed == 0 && audio.getDatamode() != AUDIO_DATA, true);
  }
  {
    fakeNow = 100;
    std::string longHeader(300, '1');
    longHeader += "\r\n";
    FakeClient client(data(longHeader));
    Audio audio(&client);
    uint8_t ignored = 0;
    (void)audio.chunkedDataTransfer(&ignored);
    check("overlong chunk header rejected", audio.stopCalls != 0, true);
  }
  {
    fakeNow = 100;
    FakeClient client;
    Audio audio(&client);
    uint8_t ignored = 0;
    check("chunk header timeout", audio.chunkedDataTransfer(&ignored) == 0 &&
                                      audio.stopCalls == 1);
  }
  {
    fakeNow = std::numeric_limits<uint32_t>::max() - 500U;
    FakeClient client(data("4\r\n"));
    client.release.assign(client.bytes.size(), 1000U);
    Audio audio(&client);
    uint8_t ignored = 0;
    const size_t parsed = audio.chunkedDataTransfer(&ignored);
    check("chunk timeout survives millis wrap",
          parsed == 4 && audio.stopCalls == 0, true);
  }
  {
    fakeNow = 100;
    FakeClient client(data("4\r\nABCD\r\n3\r\nEFG\r\n"));
    for (size_t i = 0; i < client.release.size(); ++i)
      client.release[i] = i < 6 ? 100U : (i < 10 ? 125U : 150U);
    Audio audio(&client);
    pump(audio, 20);
    check("chunk payload split between reads", audio.InBuff.text() == "ABCDEFG");
    check("transport framing never reaches decoder buffer",
          audio.InBuff.text() == "ABCDEFG");
    check("exact audio byte count", audio.InBuff.bufferFilled() == 7);
  }
  {
    fakeNow = 100;
    std::vector<uint8_t> bytes = data("6\r\nABCD");
    bytes.push_back(1);                 // one 16-byte ICY metadata block
    bytes.push_back('X');               // metadata starts in this HTTP chunk
    const auto rest = data("\r\nF\r\n123456789012345\r\n4\r\nEFGH\r\n");
    bytes.insert(bytes.end(), rest.begin(), rest.end());
    FakeClient client(bytes);
    Audio audio(&client);
    audio.m_f_metadata = true;
    audio.m_metaint = 4;
    pump(audio, 40);
    if (audio.InBuff.text() != "ABCDEFGH")
      std::printf("INFO  ICY audio bytes=%zu text=%s\n", audio.InBuff.bufferFilled(),
                  audio.InBuff.text().c_str());
    check("ICY metadata crossing HTTP chunk boundaries",
          audio.InBuff.text() == "ABCDEFGH", true);
    check("ICY bytes excluded from audio byte count",
          audio.InBuff.bufferFilled() == 8, true);
  }
  {
    fakeNow = 100;
    FakeClient client(data("1\r\nA\r\n0\r\nX-Test: yes\r\n\r\n"));
    Audio audio(&client);
    pump(audio, 30);
    check("trailers consumed without entering audio buffer",
          audio.InBuff.text() == "A" && client.position == client.bytes.size() &&
              audio.getDatamode() != AUDIO_DATA,
          true);
  }

  unsigned failed = 0;
  unsigned xfailed = 0;
  unsigned passed = 0;
  for (const Result& result : results) {
    if (result.ok) {
      ++passed;
      std::printf("PASS  %s\n", result.name);
    } else if (result.knownFailure) {
      ++xfailed;
      std::printf("XFAIL %s\n", result.name);
      if (strict) ++failed;
    } else {
      ++failed;
      std::printf("FAIL  %s\n", result.name);
    }
  }
  std::printf("SUMMARY %zu cases: %u PASS, %u XFAIL, %u FAIL%s\n",
              results.size(), passed, xfailed,
              failed, strict ? " (strict)" : "");
  return failed ? 1 : 0;
}
'''


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--strict", action="store_true",
                        help="make known contract failures fail the command")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    args = parser.parse_args()

    source = AUDIO_CPP.read_text(encoding="utf-8")
    methods = "\n\n".join(
        extract_function(source, signature)
        for signature in (
            "uint16_t Audio::readMetadata(uint16_t maxBytes, bool first)",
            "size_t Audio::chunkedDataTransfer(uint8_t* bytes)",
            "void Audio::processWebStream()",
        )
    )
    translation_unit = HARNESS_PREFIX + "\n" + methods + "\n" + HARNESS_SUFFIX

    with tempfile.TemporaryDirectory(prefix="voxone-radio-transport-") as temp:
        temp_path = Path(temp)
        cpp = temp_path / "radio_transport_extracted.cpp"
        binary = temp_path / "radio_transport_native"
        cpp.write_text(translation_unit, encoding="utf-8")
        compile_result = subprocess.run(
            [args.cxx, "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-parameter", "-Wno-unused-value", str(cpp), "-o",
             str(binary)],
            cwd=ROOT,
            check=False,
        )
        if compile_result.returncode:
            return compile_result.returncode
        command = [str(binary)]
        if args.strict:
            command.append("--strict")
        return subprocess.run(command, cwd=ROOT, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
