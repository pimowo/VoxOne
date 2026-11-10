#ifndef VOXONE_OLED128X64_LAYOUT_H
#define VOXONE_OLED128X64_LAYOUT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../core/utf8_text.h"

namespace voxone {
namespace oled128x64 {

struct Bounds {
  uint16_t left;
  uint16_t top;
  uint16_t width;
  uint16_t height;
};

constexpr uint16_t kDisplayWidth = 128;
constexpr uint16_t kGlyphWidth = 6;
constexpr Bounds kStartupLogoBand = {0, 0, 128, 32};
constexpr Bounds kStartupProgressBand = {0, 32, 128, 16};
constexpr Bounds kStartupSsidBand = {0, 48, 128, 16};
constexpr Bounds kStartupLogo = {10, 4, 108, 23};
constexpr Bounds kStartupProgress = {0, 36, 128, 8};
constexpr Bounds kStartupSsid = {0, 52, 128, 8};

constexpr Bounds kStationRow = {0, 0, 128, 8};
constexpr Bounds kArtistRow = {0, 10, 128, 8};
constexpr Bounds kTitleRow = {0, 20, 128, 8};
constexpr Bounds kClockBand = {0, 30, 128, 24};
constexpr Bounds kPlayback = {0, 56, 10, 8};
constexpr Bounds kSource = {10, 56, 45, 8};
constexpr Bounds kSpeaker = {55, 56, 7, 8};
constexpr Bounds kVolume = {65, 56, 18, 8};
constexpr Bounds kBluetoothBand = {83, 56, 33, 8};
constexpr Bounds kWifi = {116, 56, 12, 8};
constexpr uint16_t kBluetoothTextWidth = 2 * kGlyphWidth;
constexpr size_t kStartupVisibleChars = 21;

enum class PlaybackIcon : uint8_t { None, Play, Pause, Stop };

constexpr PlaybackIcon playbackIcon(bool playing, bool paused, bool stopped) {
  return playing ? PlaybackIcon::Play
                 : paused ? PlaybackIcon::Pause
                          : stopped ? PlaybackIcon::Stop
                                    : PlaybackIcon::None;
}

constexpr bool bluetoothVisible(bool connected) { return connected; }

constexpr uint16_t right(Bounds bounds) {
  return bounds.left + bounds.width;
}

constexpr bool fits(Bounds bounds) {
  return right(bounds) <= kDisplayWidth && bounds.top + bounds.height <= 64;
}

constexpr uint16_t bottom(Bounds bounds) {
  return bounds.top + bounds.height;
}

constexpr uint16_t verticalGap(Bounds upper, Bounds lower) {
  return lower.top - bottom(upper);
}

constexpr bool contains(Bounds outer, Bounds inner) {
  return inner.left >= outer.left && inner.top >= outer.top &&
         right(inner) <= right(outer) && bottom(inner) <= bottom(outer);
}

constexpr uint16_t textWidth(size_t glyphs) {
  return static_cast<uint16_t>(glyphs * kGlyphWidth);
}

constexpr uint16_t centeredLeft(uint16_t left, uint16_t rightExclusive,
                                uint16_t width) {
  return width >= rightExclusive - left
             ? left
             : static_cast<uint16_t>(left +
                 (rightExclusive - left - width) / 2);
}

constexpr uint16_t centeredTextLeft(Bounds bounds, size_t glyphs) {
  return centeredLeft(bounds.left, right(bounds), textWidth(glyphs));
}

constexpr uint8_t displayVolume(uint8_t volume) {
  return volume > 100 ? 100 : volume;
}

inline void formatSource(char* output, size_t outputSize, const char* label) {
  if (!output || outputSize == 0) return;
  snprintf(output, outputSize, "[%s]", label ? label : "");
}

inline void formatStatusVolume(char* output, size_t outputSize,
                               uint8_t volume, bool muted,
                               bool lineOutEnabled = false) {
  if (!output || outputSize == 0) return;
  if (muted) {
    snprintf(output, outputSize, "X");
  } else if (lineOutEnabled) {
    snprintf(output, outputSize, "LINE");
  } else {
    snprintf(output, outputSize, "%u",
             static_cast<unsigned>(displayVolume(volume)));
  }
}

constexpr uint16_t bluetoothTextLeft(uint16_t volumeRight) {
  return centeredLeft(volumeRight, kWifi.left, kBluetoothTextWidth);
}

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

static_assert(fits(kPlayback) && fits(kSource) && fits(kSpeaker) &&
                  fits(kVolume) && fits(kBluetoothBand) && fits(kWifi),
              "OLED64 status bounds must fit the display");
static_assert(bottom(kStartupLogoBand) == kStartupProgressBand.top &&
                  bottom(kStartupProgressBand) == kStartupSsidBand.top &&
                  bottom(kStartupSsidBand) == 64,
              "OLED64 startup must use exact 32/16/16-pixel bands");
static_assert(contains(kStartupLogoBand, kStartupLogo) &&
                  contains(kStartupProgressBand, kStartupProgress) &&
                  contains(kStartupSsidBand, kStartupSsid),
              "OLED64 startup content must stay inside its assigned band");
static_assert(verticalGap(kStationRow, kArtistRow) == 2 &&
                  verticalGap(kArtistRow, kTitleRow) == 2 &&
                  verticalGap(kTitleRow, kClockBand) == 2 &&
                  verticalGap(kClockBand, kPlayback) == 2,
              "OLED64 player vertical gaps must remain uniformly 2 px");
static_assert(right(kPlayback) == kSource.left &&
                  right(kSource) == kSpeaker.left &&
                  right(kSpeaker) < kVolume.left &&
                  right(kVolume) == kBluetoothBand.left &&
                  right(kBluetoothBand) == kWifi.left,
              "OLED64 status bounds must remain ordered and disjoint");
static_assert(textWidth(7) <= kSource.width,
              "[SPDIF] must fit the source band");
static_assert(textWidth(3) <= kVolume.width,
              "Volume 100 must fit the volume band");
static_assert(kBluetoothTextWidth <= kBluetoothBand.width,
              "BT text must fit between volume and Wi-Fi");

}  // namespace oled128x64
}  // namespace voxone

#endif
