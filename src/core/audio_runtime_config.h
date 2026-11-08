#pragma once

#include <cstddef>
#include <cstdint>

namespace voxone::audio_runtime {

// Keep the network-facing ring buffer in PSRAM. 512 KiB gives bursty/VBR FLAC
// streams enough headroom while leaving ample PSRAM on the 2 MiB S0 profile.
constexpr std::size_t kPsramInputBufferBytes = 512U * 1024U;
constexpr std::size_t kFlacFrameReserveBytes = 65535U;

// Only decode and I2S output run here. The network producer stays in loop().
constexpr std::uint8_t kTaskCore = 1;
constexpr std::uint8_t kTaskPriority = 4;
constexpr std::uint32_t kTaskStackBytes = 8U * 1024U;

// Arduino Core 2 links lwIP as a precompiled archive, so tune each audio
// socket through the supported SO_RCVBUF option.
constexpr int kAudioSocketReceiveBufferBytes = 32768;

}  // namespace voxone::audio_runtime
