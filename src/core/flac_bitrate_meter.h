#ifndef VOXONE_FLAC_BITRATE_METER_H
#define VOXONE_FLAC_BITRATE_METER_H

#include <cstdint>
#include <limits>

namespace voxone {
namespace audio {

class FlacBitrateMeter {
 public:
  static constexpr uint32_t kWindowMs = 2500;

  void reset() {
    started_ = false;
    windowStartMs_ = 0;
    compressedBytes_ = 0;
    bitrateBps_ = 0;
  }

  uint32_t addBytes(uint32_t nowMs, uint32_t compressedBytes) {
    if (!started_) {
      started_ = true;
      windowStartMs_ = nowMs;
    }
    if (compressedBytes_ > std::numeric_limits<uint64_t>::max() - compressedBytes)
      compressedBytes_ = std::numeric_limits<uint64_t>::max();
    else
      compressedBytes_ += compressedBytes;

    const uint32_t elapsedMs = nowMs - windowStartMs_;
    if (elapsedMs < kWindowMs) return 0;
    const uint32_t measured = calculateBitrateBps(compressedBytes_, elapsedMs);
    windowStartMs_ = nowMs;
    compressedBytes_ = 0;
    if (!measured) return 0;
    bitrateBps_ = measured;
    return measured;
  }

  uint32_t bitrateBps() const { return bitrateBps_; }

  static uint32_t calculateBitrateBps(uint64_t compressedBytes,
                                      uint32_t elapsedMs) {
    if (!compressedBytes || !elapsedMs) return 0;
    constexpr uint64_t kBitsPerByteTimesMsPerSecond = 8000;
    const uint64_t wholeBytesPerMs = compressedBytes / elapsedMs;
    if (wholeBytesPerMs >
        std::numeric_limits<uint32_t>::max() /
            kBitsPerByteTimesMsPerSecond)
      return std::numeric_limits<uint32_t>::max();
    const uint64_t whole = wholeBytesPerMs * kBitsPerByteTimesMsPerSecond;
    const uint64_t remainder = compressedBytes % elapsedMs;
    const uint64_t fraction =
        (remainder * kBitsPerByteTimesMsPerSecond + elapsedMs / 2U) /
        elapsedMs;
    if (whole > std::numeric_limits<uint32_t>::max() - fraction)
      return std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(whole + fraction);
  }

 private:
  uint32_t windowStartMs_ = 0;
  uint64_t compressedBytes_ = 0;
  uint32_t bitrateBps_ = 0;
  bool started_ = false;
};

}  // namespace audio
}  // namespace voxone

#endif
