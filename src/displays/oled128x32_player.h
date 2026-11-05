#ifndef VOXONE_OLED128X32_PLAYER_H
#define VOXONE_OLED128X32_PLAYER_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "oled128x32_layout.h"

namespace voxone {
namespace oled128x32 {

enum class PlaybackIcon : uint8_t { None, Play, Pause, Stop };

inline const char* btTransportPlaybackLabel(PlaybackIcon playback) {
  switch (playback) {
    case PlaybackIcon::Play: return "PLAY";
    case PlaybackIcon::Pause: return "PAUSE";
    case PlaybackIcon::Stop: return "STOP";
    case PlaybackIcon::None: return "";
  }
  return "";
}

constexpr PlaybackIcon playbackIcon(bool playing, bool paused, bool stopped) {
  return playing ? PlaybackIcon::Play
                 : paused ? PlaybackIcon::Pause
                          : stopped ? PlaybackIcon::Stop
                                    : PlaybackIcon::None;
}

constexpr bool bluetoothVisible(bool connected) { return connected; }

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
    snprintf(output, outputSize, "X");
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

constexpr size_t kMetadataBufferSize = 140;

inline void formatMetadata(char* output, size_t outputSize,
                           const char* artist, const char* title) {
  if (!output || outputSize == 0) return;
  const bool hasArtist = artist && artist[0] != '\0';
  const bool hasTitle = title && title[0] != '\0';
  if (hasArtist && hasTitle) {
    snprintf(output, outputSize, "%s - %s", artist, title);
  } else if (hasArtist) {
    snprintf(output, outputSize, "%s", artist);
  } else if (hasTitle) {
    snprintf(output, outputSize, "%s", title);
  } else {
    output[0] = '\0';
  }
}

constexpr size_t kStartupVisibleChars = 21;

inline void formatStartupSsid(char* output, size_t outputSize,
                              const char* ssid) {
  if (!output || outputSize == 0) return;
  if (!ssid || ssid[0] == '\0') {
    snprintf(output, outputSize, "(brak SSID)");
    return;
  }
  const size_t length = strlen(ssid);
  if (length <= kStartupVisibleChars) {
    snprintf(output, outputSize, "%s", ssid);
    return;
  }
  constexpr size_t suffixLength = 3;
  const size_t prefixLength = kStartupVisibleChars - suffixLength;
  snprintf(output, outputSize, "%.*s...",
           static_cast<int>(prefixLength), ssid);
}

}  // namespace oled128x32
}  // namespace voxone

#endif
