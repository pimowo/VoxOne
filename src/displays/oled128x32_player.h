#ifndef VOXONE_OLED128X32_PLAYER_H
#define VOXONE_OLED128X32_PLAYER_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "oled128x32_layout.h"
#include "../core/utf8_text.h"

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
                         bool muted, bool lineOutEnabled = false) {
  if (!output || outputSize == 0) return;
  if (muted) {
    snprintf(output, outputSize, "X");
    return;
  }
  if (lineOutEnabled) {
    snprintf(output, outputSize, "LINE");
    return;
  }
  snprintf(output, outputSize, "%u",
           static_cast<unsigned>(displayVolume(volume)));
}

inline void formatVolumePercent(char* output, size_t outputSize,
                                uint8_t volume, bool muted,
                                bool lineOutEnabled = false) {
  if (!output || outputSize == 0) return;
  if (muted) {
    snprintf(output, outputSize, "MUTE");
    return;
  }
  if (lineOutEnabled) {
    snprintf(output, outputSize, "LINE");
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
  output[0] = '\0';
  if (hasArtist && hasTitle) {
    voxone::utf8::copy(output, outputSize, artist);
    voxone::utf8::append(output, outputSize, " - ");
    voxone::utf8::append(output, outputSize, title);
  } else if (hasArtist) {
    voxone::utf8::copy(output, outputSize, artist);
  } else if (hasTitle) {
    voxone::utf8::copy(output, outputSize, title);
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
  voxone::utf8::ellipsize(output, outputSize, ssid,
                          kStartupVisibleChars);
}

}  // namespace oled128x32
}  // namespace voxone

#endif
