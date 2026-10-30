#include "../src/audioI2S/wav_header_parser.h"
#include "../src/audioI2S/web_file_bounds.h"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
using voxone::audio::WavHeaderParser;
using voxone::audio::WavHeaderStatus;

void appendTag(std::vector<uint8_t>& bytes, const char* tag) {
  for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(tag[i]));
}

void append16(std::vector<uint8_t>& bytes, uint16_t value) {
  bytes.push_back(static_cast<uint8_t>(value));
  bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void append32(std::vector<uint8_t>& bytes, uint32_t value) {
  bytes.push_back(static_cast<uint8_t>(value));
  bytes.push_back(static_cast<uint8_t>(value >> 8));
  bytes.push_back(static_cast<uint8_t>(value >> 16));
  bytes.push_back(static_cast<uint8_t>(value >> 24));
}

void set32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

void appendChunk(std::vector<uint8_t>& bytes, const char* id,
                 const std::vector<uint8_t>& payload) {
  appendTag(bytes, id);
  append32(bytes, static_cast<uint32_t>(payload.size()));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  if (payload.size() & 1U) bytes.push_back(0);
}

std::vector<uint8_t> pcmFormat() {
  std::vector<uint8_t> fmt;
  append16(fmt, 1);
  append16(fmt, 2);
  append32(fmt, 48000);
  append32(fmt, 192000);
  append16(fmt, 4);
  append16(fmt, 16);
  return fmt;
}

std::vector<uint8_t> beginWave() {
  std::vector<uint8_t> bytes;
  appendTag(bytes, "RIFF");
  append32(bytes, 0);
  appendTag(bytes, "WAVE");
  return bytes;
}

void appendDataHeader(std::vector<uint8_t>& bytes, uint32_t dataSize) {
  appendTag(bytes, "data");
  append32(bytes, dataSize);
  set32(bytes, 4,
        static_cast<uint32_t>(bytes.size() - 8) + dataSize);
}

size_t parseToData(WavHeaderParser& parser,
                   const std::vector<uint8_t>& header) {
  size_t consumed = 0;
  while (!parser.complete() && !parser.invalid()) {
    const auto result = parser.parse(header.data() + consumed,
                                     header.size() - consumed);
    if (result.status == WavHeaderStatus::NeedMoreData) break;
    assert(result.consumed > 0);
    consumed += result.consumed;
    assert(consumed <= header.size());
  }
  return consumed;
}
}  // namespace

int main(int argc, char** argv) {
  // A. Canonical 44-byte PCM header: data.chunkSize is audio bytes only.
  {
    std::vector<uint8_t> wav = beginWave();
    appendChunk(wav, "fmt ", pcmFormat());
    appendDataHeader(wav, 3648000);
    assert(wav.size() == 44);

    WavHeaderParser parser;
    assert(parseToData(parser, wav) == 44);
    assert(parser.complete() && !parser.invalid());
    assert(parser.dataOffset() == 44);
    assert(parser.dataSize() == 3648000);
    assert(parser.channels() == 2 && parser.sampleRate() == 48000 &&
           parser.bitsPerSample() == 16);

    size_t resolved = 0;
    assert(voxone::audio::resolveWavDataSize(
        3648044, parser.dataOffset(), parser.dataSize(), resolved));
    assert(resolved == 3648000);
    assert(voxone::audio::webFileReadLimit(
               3648044, 3648000, parser.dataOffset(), resolved) == 44);
    assert(!voxone::audio::webFileDataComplete(
        3648044, 3648000, parser.dataOffset(), resolved));
    assert(voxone::audio::webFileDataComplete(
        3648044, 3648044, parser.dataOffset(), resolved));
  }

  // B. Unknown, padded chunks before fmt and data are skipped structurally.
  // Embedded "data" bytes in JUNK must not be mistaken for the data chunk.
  {
    std::vector<uint8_t> wav = beginWave();
    appendChunk(wav, "JUNK", {'d', 'a', 't', 'a', 0x55});
    appendChunk(wav, "fmt ", pcmFormat());
    appendChunk(wav, "LIST", {1, 2, 3});
    appendDataHeader(wav, 100);

    WavHeaderParser parser;
    assert(parseToData(parser, wav) == wav.size());
    assert(parser.complete() && parser.dataOffset() == 70 &&
           parser.dataSize() == 100);
    size_t resolved = 0;
    assert(voxone::audio::resolveWavDataSize(
        170, parser.dataOffset(), parser.dataSize(), resolved));
    assert(resolved == 100);
  }

  // C. An incomplete RIFF or fmt payload requests more bytes and never reads
  // beyond the supplied range. An undersized fmt chunk is rejected.
  {
    std::vector<uint8_t> wav = beginWave();
    appendChunk(wav, "fmt ", pcmFormat());
    appendDataHeader(wav, 32);
    WavHeaderParser shortRiff;
    auto result = shortRiff.parse(wav.data(), 11);
    assert(result.status == WavHeaderStatus::NeedMoreData &&
           result.consumed == 0 && !shortRiff.invalid());

    WavHeaderParser shortFmt;
    result = shortFmt.parse(wav.data(), 12);
    assert(result.consumed == 12);
    result = shortFmt.parse(wav.data() + 12, 8);
    assert(result.consumed == 8);
    result = shortFmt.parse(wav.data() + 20, 8);
    assert(result.status == WavHeaderStatus::NeedMoreData &&
           result.consumed == 0);

    std::vector<uint8_t> invalid = beginWave();
    appendTag(invalid, "fmt ");
    append32(invalid, 8);
    WavHeaderParser badFmt;
    assert(badFmt.parse(invalid.data(), invalid.size()).consumed == 12);
    result = badFmt.parse(invalid.data() + 12, invalid.size() - 12);
    assert(result.status == WavHeaderStatus::Invalid && badFmt.invalid());
  }

  // G. A complete but invalid RIFF/WAVE identity is rejected before any data
  // extent can be treated as complete decoder input.
  {
    std::vector<uint8_t> invalid = beginWave();
    invalid[0] = 'R';
    invalid[1] = 'I';
    invalid[2] = 'F';
    invalid[3] = 'X';
    appendChunk(invalid, "fmt ", pcmFormat());
    appendDataHeader(invalid, 16);
    WavHeaderParser parser;
    const auto result = parser.parse(invalid.data(), invalid.size());
    assert(result.status == WavHeaderStatus::Invalid);
    assert(parser.invalid() && !parser.complete());
  }

  // D. HTTP bodies shorter than dataOffset + data.chunkSize are inconsistent;
  // subtraction is guarded when even the offset lies beyond Content-Length.
  {
    size_t resolved = 123;
    assert(!voxone::audio::resolveWavDataSize(143, 44, 100, resolved));
    assert(!voxone::audio::resolveWavDataSize(43, 44, 0, resolved));
    assert(voxone::audio::webFileReadLimit(43, 44, 44, 100) == 0);
  }

  // H. A syntactically valid header whose data chunk exceeds Content-Length
  // is inconsistent. It cannot become a complete, naturally ending body.
  {
    std::vector<uint8_t> wav = beginWave();
    appendChunk(wav, "fmt ", pcmFormat());
    appendDataHeader(wav, 101);
    WavHeaderParser parser;
    assert(parseToData(parser, wav) == 44);
    size_t resolved = 0;
    assert(!voxone::audio::resolveWavDataSize(
        144, parser.dataOffset(), parser.dataSize(), resolved));
  }

  // E. Natural completion occurs at the full 44 + data byte count, not at
  // the old, truncated body count. Repeated checks remain idempotent.
  {
    assert(!voxone::audio::webFileDataComplete(
        3648044, 3648043, 44, 3648000));
    assert(voxone::audio::webFileDataComplete(
        3648044, 3648044, 44, 3648000));
    assert(voxone::audio::webFileDataComplete(
        3648044, 3648044, 44, 3648000));
  }

  // A short but complete valid WAV body is still transport-complete even when
  // it is smaller than the decoder's normal maximum-frame fill threshold.
  {
    std::vector<uint8_t> wav = beginWave();
    appendChunk(wav, "fmt ", pcmFormat());
    appendDataHeader(wav, 4);
    wav.insert(wav.end(), {0, 0, 0, 0});
    assert(wav.size() == 48);
    WavHeaderParser parser;
    assert(parseToData(parser, wav) == 44);
    size_t resolved = 0;
    assert(voxone::audio::resolveWavDataSize(
        48, parser.dataOffset(), parser.dataSize(), resolved));
    assert(resolved == 4);
    assert(voxone::audio::webFileDataComplete(
        48, 48, parser.dataOffset(), resolved));
  }

  // J. Non-WAV web-file semantics are preserved: an unknown audio extent is
  // Content-Length-bounded, while a known extent may end before trailing data.
  {
    assert(voxone::audio::webFileReadLimit(1000, 900, 0, 0) == 100);
    assert(!voxone::audio::webFileDataComplete(1000, 999, 0, 0));
    assert(voxone::audio::webFileDataComplete(1000, 1000, 0, 0));
    assert(voxone::audio::webFileReadLimit(1000, 700, 100, 600) == 0);
    assert(voxone::audio::webFileDataComplete(1000, 700, 100, 600));
  }

  // Optional evidence mode parses an actual WAV through the same production
  // parser without making the repository test depend on an external asset.
  if (argc == 2) {
    std::ifstream input(argv[1], std::ios::binary);
    assert(input.good());
    const std::vector<uint8_t> wav{std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>()};
    assert(wav.size() <= UINT32_MAX);
    WavHeaderParser parser;
    const size_t headerBytes = parseToData(parser, wav);
    size_t resolved = 0;
    assert(parser.complete());
    assert(voxone::audio::resolveWavDataSize(
        static_cast<uint32_t>(wav.size()), parser.dataOffset(),
        parser.dataSize(), resolved));
    std::printf("bodyBytes=%lu dataOffset=%lu dataBytes=%lu parsedHeader=%lu\n",
                static_cast<unsigned long>(wav.size()),
                static_cast<unsigned long>(parser.dataOffset()),
                static_cast<unsigned long>(resolved),
                static_cast<unsigned long>(headerBytes));
  }
}
