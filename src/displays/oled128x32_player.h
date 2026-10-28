#ifndef VOXONE_OLED128X32_PLAYER_H
#define VOXONE_OLED128X32_PLAYER_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "oled128x32_layout.h"

namespace voxone {
namespace oled128x32 {

enum class PlaybackIcon : uint8_t { Stop, Play };

constexpr PlaybackIcon playbackIcon(bool playing) {
  return playing ? PlaybackIcon::Play : PlaybackIcon::Stop;
}

constexpr uint8_t wifiBars(bool connected, int rssi) {
  return !connected || rssi >= 0 || rssi < -80 ? 0
       : rssi >= -50 ? 4
       : rssi >= -60 ? 3
       : rssi >= -70 ? 2
                     : 1;
}

constexpr uint8_t displayVolume(uint8_t volume) {
  return volume > 100 ? 100 : volume;
}

inline void formatVolume(char* output, size_t outputSize, uint8_t volume,
                         bool muted) {
  if (!output || outputSize == 0) return;
  if (muted) {
    snprintf(output, outputSize, "MUTE");
    return;
  }
  snprintf(output, outputSize, "%u",
           static_cast<unsigned>(displayVolume(volume)));
}

inline void formatVolumePercent(char* output, size_t outputSize,
                                uint8_t volume, bool muted) {
  if (!output || outputSize == 0) return;
  if (muted) {
    snprintf(output, outputSize, "MUTE");
    return;
  }
  snprintf(output, outputSize, "%u%%",
           static_cast<unsigned>(displayVolume(volume)));
}

inline void formatSource(char* output, size_t outputSize,
                         const char* sourceLabel) {
  if (!output || outputSize == 0) return;
  snprintf(output, outputSize, "[%s]", sourceLabel ? sourceLabel : "");
}

}  // namespace oled128x32
}  // namespace voxone

#endif
