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

constexpr Bounds kPlayerStation = {0, 0, 128, 8};
constexpr Bounds kPlayerArtist = {0, 8, 128, 8};
constexpr Bounds kPlayerTitle = {0, 16, 128, 8};
constexpr Bounds kPlayerStatus = {0, 24, 128, 8};

constexpr Bounds kVolumeLabel = {0, 0, 128, 8};
constexpr Bounds kVolumeValue = {0, 8, 128, 16};
constexpr Bounds kVolumeFooter = {0, 24, 128, 8};

static_assert(fits(kPlayerStation) && fits(kPlayerArtist) &&
                  fits(kPlayerTitle) && fits(kPlayerStatus),
              "OLED 128x32 PLAYER layout must fit the physical panel");
static_assert(fits(kVolumeLabel) && fits(kVolumeValue) &&
                  fits(kVolumeFooter),
              "OLED 128x32 VOL layout must fit the physical panel");

}  // namespace oled128x32
}  // namespace voxone

#endif
