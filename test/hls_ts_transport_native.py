#!/usr/bin/env python3
"""Host harness for the extracted production HLS/TS transport method.

The test extracts Audio::processWebStreamTS and its transport helpers from
Audio.cpp.  It compiles those method bodies against the production MPEG-TS
parser/recovery classes and controlled FakeClient/FakeBuffer substitutes.
Audio::loop, httpPrint and parsePlaylist_M3U8 are outside this harness.
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
#include "src/audioI2S/mpeg_ts_aac_parser.h"
#include "src/core/radio_reconnect_policy.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

static uint32_t fakeNow;
static uint32_t millis() { return fakeNow++; }
static void log_e(const char*, ...) {}
static void log_w(const char*, ...) {}
static void log_i(const char*, ...) {}
static void audioLog(const char*, ...) {}
static void (*audio_info)(const char*) = nullptr;
#define AUDIO_INFO(...) audioLog(__VA_ARGS__)
#define AUDIO_ERROR(...) audioLog(__VA_ARGS__)
#define min std::min

enum : uint8_t { AUDIO_NONE, HTTP_RESPONSE_HEADER, AUDIO_DATA };

struct FakeClient {
  std::vector<uint8_t> bytes;
  size_t position = 0;
  size_t readCap = std::numeric_limits<size_t>::max();
  bool isConnected = true;
  bool stopped = false;

  void load(std::vector<uint8_t> input, bool connected) {
    bytes = std::move(input);
    position = 0;
    isConnected = connected;
    stopped = false;
  }
  int read() {
    if (position >= bytes.size()) return -1;
    return bytes[position++];
  }
  int16_t read(uint8_t* output, uint32_t count) {
    const size_t remaining = bytes.size() - position;
    const size_t requested = static_cast<size_t>(count) < readCap
                                 ? static_cast<size_t>(count)
                                 : readCap;
    const size_t copied = requested < remaining ? requested : remaining;
    if (copied) std::memcpy(output, bytes.data() + position, copied);
    position += copied;
    return static_cast<int16_t>(copied);
  }
  uint16_t readBytes(char* output, uint16_t count) {
    return static_cast<uint16_t>(
        read(reinterpret_cast<uint8_t*>(output), count));
  }
  uint32_t available() const {
    return static_cast<uint32_t>(bytes.size() - position);
  }
  bool connected() const { return isConnected; }
  void stop() { stopped = true; isConnected = false; }
};

struct FakeFile {
  int read() { return -1; }
  int read(uint8_t*, size_t) { return 0; }
  size_t readBytes(char*, size_t) { return 0; }
};

struct SdFileIoGuard {
  explicit SdFileIoGuard(bool) {}
  explicit operator bool() const { return true; }
};

struct FakeBuffer {
  std::vector<uint8_t> storage = std::vector<uint8_t>(2 * 1024 * 1024);
  size_t filled = 0;
  uint16_t getMaxBlockSize() const { return 1600; }
  size_t bufferFilled() const { return filled; }
  size_t freeSpace() const { return storage.size() - filled; }
  size_t writeSpace() const { return storage.size() - filled; }
  uint8_t* getWritePtr() { return storage.data() + filled; }
  void bytesWritten(size_t count) {
    assert(count <= writeSpace());
    filled += count;
  }
  void lock() {}
  void unlock() {}
};

struct Audio {
  enum ChunkedState : uint8_t {
    CHUNK_SIZE, CHUNK_EXTENSION, CHUNK_SIZE_LF, CHUNK_PAYLOAD,
    CHUNK_PAYLOAD_CR, CHUNK_PAYLOAD_LF, CHUNK_TRAILER_START,
    CHUNK_TRAILER, CHUNK_TRAILER_LF, CHUNK_TRAILER_FINAL_LF,
    CHUNK_DONE, CHUNK_ERROR
  };

  explicit Audio(FakeClient* input) : _client(input) {}
  int readClient();
  int readClient(uint8_t* buffer, size_t length);
  size_t readClientBytes(char* buffer, size_t length);
  size_t chunkedDataTransfer(uint8_t* bytes);
  void processWebStreamTS(bool monitorTransport);

  uint8_t getDatamode() const { return m_datamode; }
  void setDatamode(uint8_t mode) { m_datamode = mode; }
  uint32_t stopSong() { ++stopCalls; m_f_running = false; return 0; }
  void requestAudioDecode() { ++decodeRequests; }
  size_t process_m3u8_ID3_Header(uint8_t*) { return 0; }
  bool tuneAudioSocket(FakeClient&) { return true; }

  FakeClient* _client;
  FakeFile m_localFile;
  FakeBuffer InBuff;
  voxone::audio::MpegTsAacParser m_tsParser;
  voxone::audio::TsStreamRecoveryPolicy m_tsRecoveryPolicy;
  voxone::radio::SegmentTransportWatchdog m_segmentTransportWatchdog;
  bool m_localFileActive = false;
  bool m_localFileUsesSd = false;
  bool m_f_Log = false;
  bool m_f_firstCall = true;
  bool m_f_chunked = false;
  bool m_f_contentLength = false;
  bool m_f_continue = false;
  bool m_f_running = true;
  uint32_t m_contentlength = 0;
  uint32_t m_chunkcount = 0;
  uint16_t m_chunkLineLength = 0;
  ChunkedState m_chunkState = CHUNK_SIZE;
  bool m_chunkHasDigits = false;
  uint32_t m_t0 = 0;
  int m_controlCounter = 0;
  uint8_t m_datamode = AUDIO_DATA;
  uint64_t m_transportProgressBytes = 0;
  unsigned stopCalls = 0;
  unsigned decodeRequests = 0;
};
'''


HARNESS_SUFFIX = r'''
#undef min

static std::vector<uint8_t> nullPackets(size_t count) {
  std::vector<uint8_t> bytes(count * 188, 0xFF);
  for (size_t index = 0; index < count; ++index) {
    uint8_t* packet = bytes.data() + index * 188;
    packet[0] = 0x47;
    packet[1] = 0x1F;
    packet[2] = 0xFF;
    packet[3] = static_cast<uint8_t>(0x10 | (index & 0x0F));
  }
  return bytes;
}

using Packet = std::array<uint8_t, 188>;

static void append16(std::vector<uint8_t>& bytes, uint16_t value) {
  bytes.push_back(static_cast<uint8_t>(value >> 8));
  bytes.push_back(static_cast<uint8_t>(value));
}

static std::vector<uint8_t> patSection(uint16_t pmtPid) {
  std::vector<uint8_t> section = {0x00, 0xB0, 0x0D, 0x00, 0x01,
                                  0xC1, 0x00, 0x00, 0x00, 0x01};
  append16(section, static_cast<uint16_t>(0xE000 | pmtPid));
  section.insert(section.end(), 4, 0);
  assert(section.size() == 16);
  return section;
}

static std::vector<uint8_t> pmtSection(uint16_t audioPid) {
  std::vector<uint8_t> section = {0x02, 0xB0, 0x12, 0x00, 0x01,
                                  0xC1, 0x00, 0x00};
  append16(section, static_cast<uint16_t>(0xE000 | audioPid));
  append16(section, 0xF000);
  section.push_back(0x0F);  // ISO/IEC 13818-7 AAC with ADTS framing.
  append16(section, static_cast<uint16_t>(0xE000 | audioPid));
  append16(section, 0xF000);
  section.insert(section.end(), 4, 0);
  assert(section.size() == 21);
  return section;
}

static Packet packetWithPayload(uint16_t pid, bool payloadUnitStart,
                                uint8_t continuity,
                                const std::vector<uint8_t>& payload,
                                int adaptationLength = -1) {
  Packet packet{};
  packet.fill(0xFF);
  packet[0] = 0x47;
  packet[1] = static_cast<uint8_t>((payloadUnitStart ? 0x40 : 0) |
                                   ((pid >> 8) & 0x1F));
  packet[2] = static_cast<uint8_t>(pid);
  size_t offset = 4;
  if (adaptationLength >= 0) {
    packet[3] = static_cast<uint8_t>(0x30 | (continuity & 0x0F));
    packet[4] = static_cast<uint8_t>(adaptationLength);
    if (adaptationLength > 0) packet[5] = 0;
    offset = 5 + static_cast<size_t>(adaptationLength);
  } else {
    packet[3] = static_cast<uint8_t>(0x10 | (continuity & 0x0F));
  }
  assert(offset <= packet.size());
  assert(payload.size() <= packet.size() - offset);
  if (!payload.empty())
    std::memcpy(packet.data() + offset, payload.data(), payload.size());
  return packet;
}

static Packet psiPacket(uint16_t pid, const std::vector<uint8_t>& section,
                        int adaptationLength) {
  std::vector<uint8_t> payload(1, 0);  // pointer_field
  payload.insert(payload.end(), section.begin(), section.end());
  return packetWithPayload(pid, true, 0, payload, adaptationLength);
}

static Packet pesPacket(uint16_t pid, const std::vector<uint8_t>& audio,
                        uint8_t continuity) {
  std::vector<uint8_t> pes = {0x00, 0x00, 0x01, 0xC0, 0x00, 0x00,
                              0x80, 0x00, 0x00};
  const size_t declaredLength = 3 + audio.size();
  assert(declaredLength <= 0xFFFF);
  pes[4] = static_cast<uint8_t>(declaredLength >> 8);
  pes[5] = static_cast<uint8_t>(declaredLength);
  pes.insert(pes.end(), audio.begin(), audio.end());
  return packetWithPayload(pid, true, continuity, pes,
                           static_cast<int>(183 - pes.size()));
}

static void appendPacket(std::vector<uint8_t>& body, const Packet& packet) {
  body.insert(body.end(), packet.begin(), packet.end());
}

static std::vector<uint8_t> mappedAudioSegment(
    const std::vector<uint8_t>& adts) {
  std::vector<uint8_t> body;
  appendPacket(body, psiPacket(0, patSection(0x20), 166));
  appendPacket(body, psiPacket(0x20, pmtSection(0x50), 161));
  appendPacket(body, pesPacket(0x50, adts, 0));
  return body;
}

static std::vector<uint8_t> audioOnlySegment(
    const std::vector<uint8_t>& adts) {
  std::vector<uint8_t> body;
  appendPacket(body, pesPacket(0x50, adts, 8));
  return body;
}

static std::vector<uint8_t> parseAudio(
    voxone::audio::MpegTsAacParser& parser,
    const std::vector<uint8_t>& body, bool beginSegment) {
  if (beginSegment) parser.beginSegment();
  std::vector<uint8_t> audio;
  bool sawAudioData = false;
  assert(body.size() % voxone::audio::MpegTsAacParser::kPacketSize == 0);
  for (size_t offset = 0; offset < body.size(); offset += 188) {
    const auto parsed = parser.parse(body.data() + offset, 188);
    assert(parsed.status != voxone::audio::TsParseStatus::Malformed);
    assert(parsed.status != voxone::audio::TsParseStatus::UnsupportedAudio);
    if (parsed.status == voxone::audio::TsParseStatus::AudioData) {
      sawAudioData = true;
      audio.insert(audio.end(), body.begin() + offset + parsed.audioOffset,
                   body.begin() + offset + parsed.audioOffset +
                       parsed.audioLength);
    }
  }
  assert(sawAudioData);
  assert(parser.audioPid() == 0x50);
  return audio;
}

static std::vector<uint8_t> chunk(const std::vector<uint8_t>& body) {
  char size[32];
  const int length = std::snprintf(size, sizeof(size), "%zX\r\n", body.size());
  assert(length > 0);
  std::vector<uint8_t> framed(size, size + length);
  framed.insert(framed.end(), body.begin(), body.end());
  const char suffix[] = "\r\n0\r\n\r\n";
  framed.insert(framed.end(), suffix, suffix + sizeof(suffix) - 1);
  return framed;
}

static void begin(Audio& audio, FakeClient& client, std::vector<uint8_t> body,
                  bool connected, bool contentLength, uint32_t declaredLength,
                  bool chunkedTransfer = false,
                  size_t readCap = std::numeric_limits<size_t>::max()) {
  client.load(std::move(body), connected);
  client.readCap = readCap;
  audio.m_f_firstCall = true;
  audio.m_f_continue = false;
  audio.m_f_running = true;
  audio.m_f_chunked = chunkedTransfer;
  audio.m_f_contentLength = contentLength;
  audio.m_contentlength = declaredLength;
  audio.m_chunkcount = 0;
  audio.m_chunkLineLength = 0;
  audio.m_chunkState = Audio::CHUNK_SIZE;
  audio.m_chunkHasDigits = false;
  audio.setDatamode(AUDIO_DATA);
}

static void pump(Audio& audio, unsigned limit = 30000) {
  for (unsigned count = 0;
       count < limit && !audio.m_f_continue &&
       audio.getDatamode() == AUDIO_DATA && audio.m_f_running;
       ++count)
    audio.processWebStreamTS(true);
}

static std::vector<uint8_t> readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
  fakeNow = 100;
  FakeClient client;
  Audio audio(&client);
  const std::vector<uint8_t> twoPackets = nullPackets(2);

  // Deterministic positive path. A direct production-parser pass proves that
  // the tiny PAT -> PMT -> PES fixture yields AudioData for AAC PID 0x50.
  // The extracted production processWebStreamTS method must then copy exactly
  // those ADTS bytes into FakeBuffer through its real memcpy/InBuff path.
  const std::vector<uint8_t> firstAdts = {
      0xFF, 0xF1, 0x50, 0x80, 0x01, 0x7F, 0xFC, 0x11, 0x22, 0x33};
  const std::vector<uint8_t> secondAdts = {
      0xFF, 0xF1, 0x50, 0x80, 0x01, 0x7F, 0xFC, 0x44, 0x55, 0x66};
  const std::vector<uint8_t> firstSegment = mappedAudioSegment(firstAdts);
  const std::vector<uint8_t> secondSegment = audioOnlySegment(secondAdts);
  voxone::audio::MpegTsAacParser fixtureParser;
  assert(parseAudio(fixtureParser, firstSegment, true) == firstAdts);
  assert(parseAudio(fixtureParser, secondSegment, true) == secondAdts);

  FakeClient audioClient;
  Audio positive(&audioClient);
  const unsigned stopsBeforeAudio = positive.stopCalls;

  // Regression for the old EOF behavior: the peer is already disconnected,
  // yet unread Content-Length bytes remain in WiFiClient. A 37-byte cap also
  // splits every 188-byte TS packet across calls. The old socket-first rule
  // recovered after the first partial read; the fixed method must drain the
  // complete body, produce AudioData, and finish the segment locally.
  begin(positive, audioClient, firstSegment, false, true,
        static_cast<uint32_t>(firstSegment.size()), false, 37);
  pump(positive);
  assert(positive.m_f_continue);
  assert(positive.stopCalls == stopsBeforeAudio);
  assert(audioClient.available() == 0);
  assert(positive.m_segmentTransportWatchdog.bodyBytes() ==
         firstSegment.size());
  assert(positive.m_tsParser.audioPid() == 0x50);
  assert(positive.InBuff.bufferFilled() == firstAdts.size());
  assert(std::equal(firstAdts.begin(), firstAdts.end(),
                    positive.InBuff.storage.begin()));

  // A second segment without PAT/PMT exercises retained program state and a
  // second natural close. No reset may lose PID 0x50 or the accumulated audio.
  begin(positive, audioClient, secondSegment, false, true,
        static_cast<uint32_t>(secondSegment.size()), false, 29);
  pump(positive);
  assert(positive.m_f_continue);
  assert(positive.stopCalls == stopsBeforeAudio);
  assert(audioClient.available() == 0);
  assert(positive.m_tsParser.audioPid() == 0x50);
  assert(positive.InBuff.bufferFilled() ==
         firstAdts.size() + secondAdts.size());
  std::vector<uint8_t> expectedAudio = firstAdts;
  expectedAudio.insert(expectedAudio.end(), secondAdts.begin(),
                       secondAdts.end());
  assert(std::equal(expectedAudio.begin(), expectedAudio.end(),
                    positive.InBuff.storage.begin()));

  // Content-Length + close: disconnected with unread bytes is drained and
  // transitions locally without stopSong().
  begin(audio, client, twoPackets, false, true, twoPackets.size());
  pump(audio);
  assert(audio.m_f_continue && audio.stopCalls == 0);

  // Content-Length + keep-alive uses the same byte-complete criterion.
  begin(audio, client, twoPackets, true, true, twoPackets.size());
  pump(audio);
  assert(audio.m_f_continue && audio.stopCalls == 0);

  // Exact declared body with an incomplete TS unit is rejected.
  std::vector<uint8_t> partial = twoPackets;
  partial.resize(200);
  const unsigned beforePartial = audio.stopCalls;
  begin(audio, client, partial, false, true, partial.size());
  pump(audio);
  assert(!audio.m_f_continue && audio.stopCalls == beforePartial + 1);

  // EOF before Content-Length and a connected transport stall are bounded.
  const unsigned beforeShort = audio.stopCalls;
  begin(audio, client, twoPackets, false, true, twoPackets.size() + 188);
  pump(audio);
  assert(!audio.m_f_continue && audio.stopCalls == beforeShort + 1);
  const unsigned beforeStall = audio.stopCalls;
  begin(audio, client, {}, true, true, 188);
  pump(audio);
  assert(!audio.m_f_continue && audio.stopCalls == beforeStall + 1);

  // Chunked completion requires a valid zero chunk; malformed framing stops.
  begin(audio, client, chunk(twoPackets), true, false, 0, true);
  pump(audio);
  assert(audio.m_f_continue);
  const unsigned beforeChunkError = audio.stopCalls;
  begin(audio, client, {'G', '\r', '\n'}, true, false, 0, true);
  pump(audio);
  assert(!audio.m_f_continue && audio.stopCalls == beforeChunkError + 1);

  // The exact Audio loop drops one malformed TS packet without writing it,
  // then performs one controlled stop at the production recovery limit.
  std::vector<uint8_t> malformed(188, 0);
  std::vector<uint8_t> oneMalformed = malformed;
  const std::vector<uint8_t> oneNull = nullPackets(1);
  oneMalformed.insert(oneMalformed.end(), oneNull.begin(), oneNull.end());
  FakeClient recoveryClient;
  Audio isolated(&recoveryClient);
  begin(isolated, recoveryClient, oneMalformed, false, true,
        oneMalformed.size());
  pump(isolated);
  assert(isolated.m_f_continue && isolated.stopCalls == 0);
  assert(isolated.InBuff.bufferFilled() == 0);

  FakeClient abortClient;
  Audio aborting(&abortClient);
  std::vector<uint8_t> malformedBudget(
      188 * voxone::audio::TsStreamRecoveryPolicy::kMaxMalformedWithoutAudio,
      0);
  begin(aborting, abortClient, malformedBudget, false, true,
        malformedBudget.size());
  pump(aborting);
  assert(!aborting.m_f_continue && aborting.stopCalls == 1);
  assert(aborting.InBuff.bufferFilled() == 0);

  // Consecutive segment transitions keep the Audio transport alive.
  for (unsigned segment = 0; segment < 2; ++segment) {
    begin(audio, client, twoPackets, false, true, twoPackets.size());
    pump(audio);
    assert(audio.m_f_continue);
  }

  const size_t audioBeforeFixtures = audio.InBuff.bufferFilled();
  for (int index = 1; index < argc; ++index) {
    std::vector<uint8_t> fixture = readFile(argv[index]);
    assert(!fixture.empty() && fixture.size() % 188 == 0);
    const unsigned before = audio.stopCalls;
    const uint32_t length = static_cast<uint32_t>(fixture.size());
    begin(audio, client, std::move(fixture), false, true, length);
    pump(audio);
    assert(audio.m_f_continue && audio.stopCalls == before);
  }
  if (argc > 1) {
    assert(argc == 13);
    assert(audio.InBuff.bufferFilled() > audioBeforeFixtures);
    assert(audio.m_tsParser.audioPid() == 0x50);
  }
  return 0;
}
'''


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("fixtures", nargs="*")
    args = parser.parse_args()
    if args.fixtures and len(args.fixtures) != 12:
        parser.error("provide exactly 12 qualified TS segments")

    source = AUDIO_CPP.read_text(encoding="utf-8")
    methods = "\n\n".join(
        extract_function(source, signature)
        for signature in (
            "int Audio::readClient()",
            "int Audio::readClient(uint8_t* buffer, size_t length)",
            "size_t Audio::readClientBytes(char* buffer, size_t length)",
            "size_t Audio::chunkedDataTransfer(uint8_t* bytes)",
            "void Audio::processWebStreamTS(bool monitorTransport)",
        )
    )
    translation_unit = HARNESS_PREFIX + "\n" + methods + "\n" + HARNESS_SUFFIX

    with tempfile.TemporaryDirectory(prefix="voxone-hls-ts-transport-") as temp:
        temp_path = Path(temp)
        cpp = temp_path / "hls_ts_transport_extracted.cpp"
        binary = temp_path / "hls_ts_transport_native"
        cpp.write_text(translation_unit, encoding="utf-8")
        compile_args = [
            args.cxx, "-std=c++11", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", "-I", str(ROOT),
        ]
        if args.sanitize:
            compile_args += ["-O1", "-g", "-fsanitize=address,undefined",
                             "-fno-omit-frame-pointer"]
        else:
            compile_args += ["-O2"]
        compile_args += [
            str(cpp), str(ROOT / "src/audioI2S/mpeg_ts_aac_parser.cpp"),
            "-o", str(binary),
        ]
        result = subprocess.run(compile_args, cwd=ROOT, check=False)
        if result.returncode:
            return result.returncode
        return subprocess.run([str(binary), *args.fixtures], cwd=ROOT,
                              check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
