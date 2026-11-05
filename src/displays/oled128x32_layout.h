#ifndef VOXONE_OLED128X32_LAYOUT_H
#define VOXONE_OLED128X32_LAYOUT_H

#include <stdint.h>

namespace voxone {
namespace oled128x32 {

constexpr uint16_t kWidth = 128;
constexpr uint16_t kHeight = 32;
constexpr uint16_t kRowHeight = 8;
constexpr uint16_t kGlyphWidth = 6;

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

constexpr Bounds kStartupLogoBand = {0, 0, 128, 16};
constexpr Bounds kStartupProgressBand = {0, 16, 128, 8};
constexpr Bounds kStartupSsidBand = {0, 24, 128, 8};
constexpr Bounds kStartupLogo = {26, 0, 75, 16};
constexpr Bounds kStartupProgress = {0, 16, 128, 8};
constexpr Bounds kStartupSsid = {0, 24, 128, 8};

constexpr Bounds kPlayerStation = {0, 0, 128, 12};
constexpr Bounds kPlayerStationText = {0, 2, 128, 8};
constexpr Bounds kPlayerArtist = {0, 14, 128, 8};
constexpr Bounds kPlayerStatus = {0, 24, 128, 8};
constexpr Bounds kPlayerPlayback = {0, 24, 10, 8};
constexpr Bounds kPlayerSource = {10, 24, 45, 8};
constexpr Bounds kPlayerSpeaker = {55, 24, 7, 8};
constexpr Bounds kPlayerVolume = {65, 24, 18, 8};
constexpr Bounds kPlayerBluetoothBand = {83, 24, 33, 8};
constexpr Bounds kPlayerWifi = {116, 24, 12, 8};

constexpr Bounds kPlayerPlay = {0, 24, 8, 8};
constexpr Bounds kPlayerPause = {1, 24, 6, 8};
constexpr Bounds kPlayerStop = {1, 25, 6, 6};
constexpr uint16_t kPlayerPlayLeft = 0;
constexpr uint16_t kPlayerPlayTop = 24;
constexpr uint16_t kPlayerPlayRight = 7;
constexpr uint16_t kPlayerPlayBottom = 31;
constexpr uint16_t kPlayerPlayTipY = 27;
constexpr Bounds kPlayerWifiDisconnected = {118, 24, 8, 8};

// Compact Bluetooth transport view. The accepted STARTUP and PLAYER layouts
// above are intentionally independent from this dedicated page.
constexpr Bounds kBtTransportArtist = {0, 0, 128, 12};
constexpr Bounds kBtTransportArtistText = {0, 2, 128, 8};
constexpr Bounds kBtTransportTitle = {0, 14, 128, 8};
constexpr Bounds kBtTransportControls = {0, 24, 128, 8};
constexpr Bounds kBtTransportPrevious = {0, 24, 6, 8};
constexpr Bounds kBtTransportPlayback = {31, 24, 66, 8};
constexpr Bounds kBtTransportNext = {121, 24, 6, 8};

constexpr uint8_t kPlayerSpeakerWidth = 7;
constexpr uint8_t kPlayerSourceMaxCharacters = 7;
constexpr uint8_t kPlayerBluetoothTextWidth = 2 * kGlyphWidth;
constexpr uint8_t kStationScaleX = 2;
constexpr uint8_t kStationScaleY = 1;
constexpr uint8_t kStationCellWidth = kGlyphWidth * kStationScaleX;
constexpr uint8_t kStationCellHeight = kRowHeight * kStationScaleY;
constexpr uint8_t kArtistCellWidth = kGlyphWidth;
constexpr uint8_t kArtistCellHeight = kRowHeight;

constexpr uint16_t right(Bounds bounds) {
  return bounds.left + bounds.width;
}

constexpr uint16_t bottom(Bounds bounds) {
  return bounds.top + bounds.height;
}

constexpr uint16_t verticalGap(Bounds upper, Bounds lower) {
  return lower.top - bottom(upper);
}

constexpr uint16_t textWidth(uint16_t glyphs) {
  return glyphs * kGlyphWidth;
}

constexpr uint16_t centeredLeft(uint16_t left, uint16_t rightExclusive,
                                uint16_t width) {
  return width >= rightExclusive - left
             ? left
             : static_cast<uint16_t>(left +
                   (rightExclusive - left - width) / 2);
}

constexpr uint16_t centeredTextLeft(Bounds bounds, uint16_t glyphs) {
  return centeredLeft(bounds.left, right(bounds), textWidth(glyphs));
}

constexpr uint8_t playerWifiBarHeight(uint8_t index) {
  return static_cast<uint8_t>((index + 1) * 2);
}

constexpr Bounds playerWifiBar(uint8_t index) {
  return Bounds{static_cast<uint16_t>(kPlayerWifi.left + index * 3),
                static_cast<uint16_t>(kHeight - playerWifiBarHeight(index)),
                2, playerWifiBarHeight(index)};
}

constexpr uint16_t playerVolumeTextWidth(uint8_t volume, bool muted) {
  return muted ? kGlyphWidth
               : (volume >= 100 ? 3 : volume >= 10 ? 2 : 1) * kGlyphWidth;
}

constexpr uint16_t playerVolumeTextLeft(uint8_t volume, bool muted) {
  return centeredLeft(kPlayerVolume.left, right(kPlayerVolume),
                      playerVolumeTextWidth(volume, muted));
}

constexpr uint16_t playerBluetoothTextLeft(uint16_t volumeRight) {
  return centeredLeft(volumeRight, kPlayerWifi.left,
                      kPlayerBluetoothTextWidth);
}

constexpr Bounds kVolumePrimary = {0, 0, 128, 16};
constexpr Bounds kVolumeFooter = {0, 24, 128, 8};
constexpr uint8_t kVolumePrimaryGlyphWidth = 12;
constexpr uint8_t kVolumePrimaryMaxCharacters = 4;

static_assert(bottom(kStartupLogoBand) == kStartupProgressBand.top &&
                  bottom(kStartupProgressBand) == kStartupSsidBand.top &&
                  bottom(kStartupSsidBand) == kHeight,
              "OLED 128x32 STARTUP must use exact 16/8/8-pixel bands");
static_assert(contains(kStartupLogoBand, kStartupLogo) &&
                  contains(kStartupProgressBand, kStartupProgress) &&
                  contains(kStartupSsidBand, kStartupSsid),
              "OLED 128x32 STARTUP content must fit its assigned band");
static_assert(fits(kPlayerStation) && fits(kPlayerStationText) &&
                  fits(kPlayerArtist) && fits(kPlayerStatus),
              "OLED 128x32 PLAYER layout must fit the physical panel");
static_assert(contains(kPlayerStation, kPlayerStationText) &&
                  kStationCellHeight <= kPlayerStationText.height &&
                  kArtistCellHeight <= kPlayerArtist.height &&
                  verticalGap(kPlayerStation, kPlayerArtist) == 2 &&
                  verticalGap(kPlayerArtist, kPlayerStatus) == 2,
              "OLED 128x32 text rows must fit with exact 2 px gaps");
static_assert(contains(kPlayerStatus, kPlayerPlayback) &&
                  contains(kPlayerStatus, kPlayerSource) &&
                  contains(kPlayerStatus, kPlayerSpeaker) &&
                  contains(kPlayerStatus, kPlayerVolume) &&
                  contains(kPlayerStatus, kPlayerBluetoothBand) &&
                  contains(kPlayerStatus, kPlayerWifi),
              "OLED 128x32 PLAYER status slots must fit the bottom row");
static_assert(kPlayerPlayback.left + kPlayerPlayback.width <=
                      kPlayerSource.left &&
                  right(kPlayerSource) == kPlayerSpeaker.left &&
                  right(kPlayerSpeaker) < kPlayerVolume.left &&
                  right(kPlayerVolume) == kPlayerBluetoothBand.left &&
                  right(kPlayerBluetoothBand) == kPlayerWifi.left,
              "OLED 128x32 PLAYER status slots must not overlap");
static_assert(contains(kPlayerPlayback, kPlayerPlay) &&
                  contains(kPlayerPlayback, kPlayerPause) &&
                  contains(kPlayerPlayback, kPlayerStop),
              "OLED 128x32 playback icons must fit their slot");
static_assert(kPlayerSourceMaxCharacters * kGlyphWidth <=
                  kPlayerSource.width,
              "OLED 128x32 source label must fit its slot");
static_assert(playerVolumeTextWidth(100, false) <= kPlayerVolume.width &&
                  playerVolumeTextWidth(100, true) <= kPlayerVolume.width &&
                  kPlayerBluetoothTextWidth <= kPlayerBluetoothBand.width,
              "OLED 128x32 volume and BT text must fit their slots");
static_assert(contains(kPlayerWifi, playerWifiBar(0)) &&
                  contains(kPlayerWifi, playerWifiBar(3)) &&
                  contains(kPlayerWifi, kPlayerWifiDisconnected),
              "OLED 128x32 Wi-Fi icons must fit their slot");
static_assert(fits(kBtTransportArtist) && fits(kBtTransportArtistText) &&
                  fits(kBtTransportTitle) && fits(kBtTransportControls) &&
                  contains(kBtTransportArtist, kBtTransportArtistText) &&
                  verticalGap(kBtTransportArtist, kBtTransportTitle) == 2 &&
                  verticalGap(kBtTransportTitle, kBtTransportControls) == 2 &&
                  contains(kBtTransportControls, kBtTransportPrevious) &&
                  contains(kBtTransportControls, kBtTransportPlayback) &&
                  contains(kBtTransportControls, kBtTransportNext) &&
                  right(kBtTransportPrevious) <= kBtTransportPlayback.left &&
                  right(kBtTransportPlayback) <= kBtTransportNext.left,
              "OLED 128x32 BT transport rows must fit without overlap");
static_assert(fits(kVolumePrimary) && fits(kVolumeFooter),
              "OLED 128x32 VOL layout must fit the physical panel");
static_assert(kVolumePrimaryGlyphWidth * kVolumePrimaryMaxCharacters <=
                  kVolumePrimary.width,
              "OLED 128x32 VOL value must fit the physical panel");

}  // namespace oled128x32
}  // namespace voxone

#endif
