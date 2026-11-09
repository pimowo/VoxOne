#pragma once

#include <cstddef>
#include <cstdint>

#include "../hardware/hardware_descriptor.h"

namespace voxone::audio_runtime {

// Profiles with PSRAM keep the network-facing ring there. Non-PSRAM profiles
// use a bounded DRAM ring sized for MP3/AAC and never attempt the PSRAM path.
constexpr std::size_t kPsramInputBufferBytes = 512U * 1024U;
constexpr std::size_t kDramInputBufferBytes = 48U * 1024U;
constexpr std::size_t kFlacFrameReserveBytes = 65535U;
constexpr std::size_t kCompressedFrameReserveBytes = 1600U;

struct InputBufferPlan {
  bool usePsram;
  std::size_t inputBytes;
  std::size_t reserveBytes;
};

constexpr InputBufferPlan inputBufferPlan(bool supportsPsram) {
  return supportsPsram
             ? InputBufferPlan{true, kPsramInputBufferBytes,
                               kFlacFrameReserveBytes}
             : InputBufferPlan{false, kDramInputBufferBytes,
                               kCompressedFrameReserveBytes};
}

inline InputBufferPlan currentInputBufferPlan() {
  return inputBufferPlan(
      hardware::hardwareCapabilities().supportsPsram);
}

// Only decode and I2S output run here. The network producer stays in loop().
constexpr std::uint8_t kTaskCore = 1;
constexpr std::uint8_t kTaskPriority = 4;
constexpr std::uint32_t kTaskStackBytes = 8U * 1024U;

// Arduino Core 2 links lwIP as a precompiled archive, so tune each audio
// socket through the supported SO_RCVBUF option.
constexpr int kAudioSocketReceiveBufferBytes = 32768;

}  // namespace voxone::audio_runtime
