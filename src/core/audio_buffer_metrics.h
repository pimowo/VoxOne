#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace voxone::audio {

struct AudioBufferMetricsSnapshot {
  AudioBufferMetricsSnapshot(std::uint32_t underflowCount,
                             std::size_t minimumFill, bool hasObservation)
      : underflows(underflowCount),
        minimumFillBytes(minimumFill),
        observed(hasObservation) {}

  std::uint32_t underflows;
  std::size_t minimumFillBytes;
  bool observed;
};

class AudioBufferMetrics {
 public:
  void observe(std::size_t filledBytes, std::size_t requiredBytes,
               bool finalPartialFrame = false) {
    observed_ = true;
    if (filledBytes < minimumFillBytes_) minimumFillBytes_ = filledBytes;
    const bool starved = filledBytes < requiredBytes && !finalPartialFrame;
    if (starved && !underflowActive_) ++underflows_;
    underflowActive_ = starved;
  }

  AudioBufferMetricsSnapshot snapshot() const {
    return {underflows_,
            minimumFillBytes_ == std::numeric_limits<std::size_t>::max()
                ? 0
                : minimumFillBytes_,
            observed_};
  }

  void reset() {
    underflows_ = 0;
    minimumFillBytes_ = std::numeric_limits<std::size_t>::max();
    underflowActive_ = false;
    observed_ = false;
  }

 private:
  std::uint32_t underflows_ = 0;
  std::size_t minimumFillBytes_ = std::numeric_limits<std::size_t>::max();
  bool underflowActive_ = false;
  bool observed_ = false;
};

}  // namespace voxone::audio
