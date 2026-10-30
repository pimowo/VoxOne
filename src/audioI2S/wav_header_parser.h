#ifndef VOXONE_WAV_HEADER_PARSER_H
#define VOXONE_WAV_HEADER_PARSER_H

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace audio {

enum class WavHeaderStatus : uint8_t {
  NeedMoreData = 0,
  Progress,
  Complete,
  Invalid,
};

struct WavHeaderResult {
  WavHeaderResult(WavHeaderStatus value = WavHeaderStatus::NeedMoreData,
                  size_t bytes = 0)
      : status(value), consumed(bytes) {}
  WavHeaderStatus status;
  size_t consumed;
};

// Incremental RIFF/WAVE chunk parser. The caller removes exactly `consumed`
// bytes from its input buffer, so even large unknown chunks can be skipped
// without allocating or assuming a 44-byte header.
class WavHeaderParser {
 public:
  void reset() {
    phase_ = Phase::RiffHeader;
    headerBytes_ = 0;
    chunkRemaining_ = 0;
    dataOffset_ = 0;
    dataSize_ = 0;
    formatCode_ = 0;
    channels_ = 0;
    sampleRate_ = 0;
    byteRate_ = 0;
    blockAlign_ = 0;
    bitsPerSample_ = 0;
    formatReady_ = false;
    complete_ = false;
    invalid_ = false;
  }

  WavHeaderResult parse(const uint8_t* data, size_t length) {
    if (invalid_) return {WavHeaderStatus::Invalid, 0};
    if (complete_) return {WavHeaderStatus::Complete, 0};
    if (!data && length) return fail();

    switch (phase_) {
      case Phase::RiffHeader:
        return parseRiff(data, length);
      case Phase::ChunkHeader:
        return parseChunkHeader(data, length);
      case Phase::FormatPayload:
        return parseFormat(data, length);
      case Phase::SkipPayload:
        return skipPayload(length);
    }
    return fail();
  }

  bool complete() const { return complete_; }
  bool invalid() const { return invalid_; }
  bool formatReady() const { return formatReady_; }
  uint32_t dataOffset() const { return dataOffset_; }
  uint32_t dataSize() const { return dataSize_; }
  uint16_t formatCode() const { return formatCode_; }
  uint16_t channels() const { return channels_; }
  uint32_t sampleRate() const { return sampleRate_; }
  uint32_t byteRate() const { return byteRate_; }
  uint16_t blockAlign() const { return blockAlign_; }
  uint16_t bitsPerSample() const { return bitsPerSample_; }

 private:
  enum class Phase : uint8_t {
    RiffHeader = 0,
    ChunkHeader,
    FormatPayload,
    SkipPayload,
  };

  static uint16_t little16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
  }

  static uint32_t little32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
  }

  static bool tag(const uint8_t* data, char a, char b, char c, char d) {
    return data[0] == static_cast<uint8_t>(a) &&
           data[1] == static_cast<uint8_t>(b) &&
           data[2] == static_cast<uint8_t>(c) &&
           data[3] == static_cast<uint8_t>(d);
  }

  bool addHeaderBytes(uint64_t count) {
    if (count > UINT32_MAX || headerBytes_ > UINT32_MAX - count) return false;
    headerBytes_ += count;
    return true;
  }

  WavHeaderResult fail() {
    invalid_ = true;
    return {WavHeaderStatus::Invalid, 0};
  }

  WavHeaderResult parseRiff(const uint8_t* data, size_t length) {
    if (length < 12) return {WavHeaderStatus::NeedMoreData, 0};
    if (!tag(data, 'R', 'I', 'F', 'F') ||
        !tag(data + 8, 'W', 'A', 'V', 'E')) {
      return fail();
    }
    headerBytes_ = 12;
    phase_ = Phase::ChunkHeader;
    return {WavHeaderStatus::Progress, 12};
  }

  WavHeaderResult parseChunkHeader(const uint8_t* data, size_t length) {
    if (length < 8) return {WavHeaderStatus::NeedMoreData, 0};
    const uint32_t chunkSize = little32(data + 4);
    if (!addHeaderBytes(8)) return fail();

    if (tag(data, 'd', 'a', 't', 'a')) {
      if (!formatReady_) return fail();
      dataOffset_ = static_cast<uint32_t>(headerBytes_);
      dataSize_ = chunkSize;
      complete_ = true;
      return {WavHeaderStatus::Complete, 8};
    }

    chunkRemaining_ = static_cast<uint64_t>(chunkSize) + (chunkSize & 1U);
    if (tag(data, 'f', 'm', 't', ' ')) {
      if (formatReady_ || chunkSize < 16 || chunkSize > 40)
        return fail();
      phase_ = Phase::FormatPayload;
    } else {
      phase_ = chunkRemaining_ ? Phase::SkipPayload : Phase::ChunkHeader;
    }
    return {WavHeaderStatus::Progress, 8};
  }

  WavHeaderResult parseFormat(const uint8_t* data, size_t length) {
    if (chunkRemaining_ < 16) return fail();
    if (static_cast<uint64_t>(length) < chunkRemaining_)
      return {WavHeaderStatus::NeedMoreData, 0};

    formatCode_ = little16(data);
    channels_ = little16(data + 2);
    sampleRate_ = little32(data + 4);
    byteRate_ = little32(data + 8);
    blockAlign_ = little16(data + 12);
    bitsPerSample_ = little16(data + 14);
    if (formatCode_ != 1 || (channels_ != 1 && channels_ != 2) ||
        (bitsPerSample_ != 8 && bitsPerSample_ != 16) || !sampleRate_) {
      return fail();
    }

    const size_t consumed = static_cast<size_t>(chunkRemaining_);
    if (!addHeaderBytes(chunkRemaining_)) return fail();
    chunkRemaining_ = 0;
    formatReady_ = true;
    phase_ = Phase::ChunkHeader;
    return {WavHeaderStatus::Progress, consumed};
  }

  WavHeaderResult skipPayload(size_t length) {
    if (!chunkRemaining_) {
      phase_ = Phase::ChunkHeader;
      return {WavHeaderStatus::Progress, 0};
    }
    if (!length) return {WavHeaderStatus::NeedMoreData, 0};
    size_t consumed = length;
    if (static_cast<uint64_t>(consumed) > chunkRemaining_)
      consumed = static_cast<size_t>(chunkRemaining_);
    if (!addHeaderBytes(consumed)) return fail();
    chunkRemaining_ -= consumed;
    if (!chunkRemaining_) phase_ = Phase::ChunkHeader;
    return {WavHeaderStatus::Progress, consumed};
  }

  Phase phase_ = Phase::RiffHeader;
  uint64_t headerBytes_ = 0;
  uint64_t chunkRemaining_ = 0;
  uint32_t dataOffset_ = 0;
  uint32_t dataSize_ = 0;
  uint16_t formatCode_ = 0;
  uint16_t channels_ = 0;
  uint32_t sampleRate_ = 0;
  uint32_t byteRate_ = 0;
  uint16_t blockAlign_ = 0;
  uint16_t bitsPerSample_ = 0;
  bool formatReady_ = false;
  bool complete_ = false;
  bool invalid_ = false;
};

}  // namespace audio
}  // namespace voxone

#endif
