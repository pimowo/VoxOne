#pragma once

#include <cstddef>
#include <cstdint>

#include "audio_path_config.h"
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

struct OutputActivationPlan {
  bool initializeSerialI2s;
  bool localPlaybackAvailable;
  bool dspMiniSelected;
};

// DSPmini consumes the same serial I2S stream as PCM5102. Its ADAU1701 I2C
// programming is intentionally outside this stage; selecting it only chooses
// and validates the audio route prepared for the later backend.
constexpr OutputActivationPlan outputActivationPlan(
    hardware::AudioOutputKind output, AudioPath path,
    bool supportsDspMini) {
  return output == hardware::AudioOutputKind::None
             ? OutputActivationPlan{false, false, false}
             : path == AUDIO_DSPMINI
                   ? (supportsDspMini &&
                              output == hardware::AudioOutputKind::DspMini
                          ? OutputActivationPlan{true, true, true}
                          : OutputActivationPlan{false, false, false})
                   : (output == hardware::AudioOutputKind::Pcm5102a ||
                              output == hardware::AudioOutputKind::Max98357 ||
                              output == hardware::AudioOutputKind::Max98357Stereo
                          ? OutputActivationPlan{true, true, false}
                          : OutputActivationPlan{false, false, false});
}

// Only decode and I2S output run here. The network producer stays in loop().
constexpr std::uint8_t kTaskCore = 1;
constexpr std::uint8_t kTaskPriority = 4;
constexpr std::uint32_t kTaskStackBytes = 8U * 1024U;

// Arduino Core 2 links lwIP as a precompiled archive, so tune each audio
// socket through the supported SO_RCVBUF option.
constexpr int kAudioSocketReceiveBufferBytes = 32768;

}  // namespace voxone::audio_runtime
