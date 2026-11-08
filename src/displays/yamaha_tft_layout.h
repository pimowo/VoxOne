#ifndef VOXONE_YAMAHA_TFT_LAYOUT_H
#define VOXONE_YAMAHA_TFT_LAYOUT_H

#include <stdint.h>

namespace voxone {
namespace yamaha_tft {

constexpr int16_t kWidth = 480;
constexpr int16_t kHeight = 320;
constexpr uint16_t kBackground = 0x0000;  // #000000
constexpr uint16_t kAmber = 0xfd80;        // #FFB000 in RGB565
constexpr int16_t kTopBarBottom = 54;
constexpr int16_t kContentTop = 70;
constexpr int16_t kVolumeTop = 244;
constexpr uint8_t kSignalBars = 4;

inline uint8_t boundedSignalLevel(uint8_t level) {
  return level > kSignalBars ? kSignalBars : level;
}

}  // namespace yamaha_tft
}  // namespace voxone

#endif
