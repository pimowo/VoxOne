#ifndef VOXONE_OLED128X32_LAYOUT_H
#define VOXONE_OLED128X32_LAYOUT_H

#include <stdint.h>

namespace voxone {
namespace oled128x32 {

constexpr uint16_t kWidth = 128;
constexpr uint16_t kHeight = 32;
constexpr uint16_t kRowHeight = 8;

struct Bounds {
  uint16_t left;
  uint16_t top;
  uint16_t width;
  uint16_t height;
};

constexpr bool fits(Bounds bounds) {
  return bounds.left <= kWidth && bounds.top <= kHeight &&
      bounds.width <= kWidth - bounds.left &&
      bounds.height <= kHeight - bounds.top;
}

constexpr bool contains(Bounds outer, Bounds inner) {
  return inner.left >= outer.left && inner.top >= outer.top &&
      inner.left + inner.width <= outer.left + outer.width &&
      inner.top + inner.height <= outer.top + outer.height;
}

constexpr Bounds kPlayerStation = {0, 0, 128, 24};
constexpr Bounds kPlayerStatus = {0, 24, 128, 8};
constexpr Bounds kPlayerPlayback = {0, 24, 10, 8};
constexpr Bounds kPlayerSource = {10, 24, 42, 8};
constexpr Bounds kPlayerVolume = {52, 24, 56, 8};
constexpr Bounds kPlayerWifi = {108, 24, 20, 8};

constexpr Bounds kPlayerPlay = {1, 24, 8, 8};
constexpr uint16_t kPlayerPlayLeft = 1;
constexpr uint16_t kPlayerPlayTop = 24;
constexpr uint16_t kPlayerPlayRight = 8;
constexpr uint16_t kPlayerPlayBottom = 31;
constexpr uint16_t kPlayerPlayTipY = 27;
constexpr Bounds kPlayerStop = {1, 25, 7, 6};
constexpr Bounds kPlayerWifiDisconnected = {114, 24, 8, 8};

constexpr uint8_t kPlayerSpeakerWidth = 7;
constexpr uint8_t kPlayerVolumeGap = 3;
constexpr uint8_t kPlayerGlyphWidth = 6;
constexpr uint8_t kPlayerSourceMaxCharacters = 7;

constexpr uint8_t playerWifiBarHeight(uint8_t index) {
  return static_cast<uint8_t>((index + 1) * 2);
}

constexpr Bounds playerWifiBar(uint8_t index) {
  return Bounds{static_cast<uint16_t>(kPlayerWifi.left + 1 + index * 4),
                static_cast<uint16_t>(kHeight - playerWifiBarHeight(index)),
                3, playerWifiBarHeight(index)};
}

constexpr uint16_t playerVolumeTextWidth(uint8_t volume, bool muted) {
  return muted ? 4 * kPlayerGlyphWidth
               : (volume >= 100 ? 3 : volume >= 10 ? 2 : 1) *
                     kPlayerGlyphWidth;
}

constexpr uint16_t playerVolumeContentWidth(uint8_t volume, bool muted) {
  return kPlayerSpeakerWidth + kPlayerVolumeGap +
         playerVolumeTextWidth(volume, muted);
}

constexpr uint16_t playerVolumeContentLeft(uint8_t volume, bool muted) {
  return static_cast<uint16_t>(
      kPlayerVolume.left +
      (kPlayerVolume.width - playerVolumeContentWidth(volume, muted)) / 2);
}

constexpr Bounds kVolumePrimary = {0, 0, 128, 16};
constexpr Bounds kVolumeFooter = {0, 24, 128, 8};
constexpr uint8_t kVolumePrimaryGlyphWidth = 12;
constexpr uint8_t kVolumePrimaryMaxCharacters = 4;

static_assert(fits(kPlayerStation) && fits(kPlayerStatus),
              "OLED 128x32 PLAYER layout must fit the physical panel");
static_assert(contains(kPlayerStatus, kPlayerPlayback) &&
                  contains(kPlayerStatus, kPlayerSource) &&
                  contains(kPlayerStatus, kPlayerVolume) &&
                  contains(kPlayerStatus, kPlayerWifi),
              "OLED 128x32 PLAYER status slots must fit the bottom row");
static_assert(kPlayerPlayback.left + kPlayerPlayback.width <=
                      kPlayerSource.left &&
                  kPlayerSource.left + kPlayerSource.width <=
                      kPlayerVolume.left &&
                  kPlayerVolume.left + kPlayerVolume.width <= kPlayerWifi.left,
              "OLED 128x32 PLAYER status slots must not overlap");
static_assert(contains(kPlayerPlayback, kPlayerPlay) &&
                  contains(kPlayerPlayback, kPlayerStop),
              "OLED 128x32 playback icons must fit their slot");
static_assert(kPlayerSourceMaxCharacters * kPlayerGlyphWidth <=
                  kPlayerSource.width,
              "OLED 128x32 source label must fit its slot");
static_assert(playerVolumeContentWidth(100, false) <= kPlayerVolume.width &&
                  playerVolumeContentWidth(100, true) <= kPlayerVolume.width,
              "OLED 128x32 volume content must fit its slot");
static_assert(contains(kPlayerWifi, playerWifiBar(0)) &&
                  contains(kPlayerWifi, playerWifiBar(3)) &&
                  contains(kPlayerWifi, kPlayerWifiDisconnected),
              "OLED 128x32 Wi-Fi icons must fit their slot");
static_assert(fits(kVolumePrimary) && fits(kVolumeFooter),
              "OLED 128x32 VOL layout must fit the physical panel");
static_assert(kVolumePrimaryGlyphWidth * kVolumePrimaryMaxCharacters <=
                  kVolumePrimary.width,
              "OLED 128x32 VOL value must fit the physical panel");

}  // namespace oled128x32
}  // namespace voxone

#endif
