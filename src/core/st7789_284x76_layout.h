#ifndef VOXONE_ST7789_284X76_LAYOUT_H
#define VOXONE_ST7789_284X76_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

namespace st7789_284x76 {

constexpr uint16_t width = 284;
constexpr uint16_t height = 76;
constexpr uint16_t dividerTop = 59;
constexpr uint16_t statusTop = 60;
constexpr uint16_t statusHeight = 16;

struct Rect {
  uint16_t left;
  uint16_t top;
  uint16_t width;
  uint16_t height;
};

constexpr Rect playback = {4, 63, 10, 10};
constexpr Rect source = {18, 62, 48, 12};
constexpr Rect volume = {72, 62, 42, 12};
constexpr Rect audio = {120, 62, 100, 12};
constexpr Rect bluetooth = {228, 62, 14, 12};
constexpr Rect wifi = {248, 62, 32, 12};

constexpr uint16_t updateTitleTop = 1;
constexpr uint16_t updateTargetTop = 12;
constexpr uint16_t updatePercentTop = 22;
constexpr uint16_t updateBarLeft = 22;
constexpr uint16_t updateBarTop = 43;
constexpr uint16_t updateBarWidth = 240;
constexpr uint16_t updateBarHeight = 9;
constexpr uint16_t updateBarInset = 2;
constexpr uint16_t updateBarInteriorWidth = updateBarWidth - 2 * updateBarInset;
constexpr uint16_t updateBarInteriorHeight = updateBarHeight - 2 * updateBarInset;
constexpr uint16_t updateIndeterminateWidth = 32;
constexpr uint16_t updateIndeterminateStep = 6;
constexpr uint16_t updateActivityTop = 60;

constexpr bool inside(const Rect& rect) {
  return rect.left + rect.width <= width && rect.top + rect.height <= height;
}

constexpr bool separatedHorizontally(const Rect& left, const Rect& right) {
  return left.left + left.width <= right.left;
}

inline uint16_t centeredTextLeft(const Rect& slot, size_t chars,
                                 uint16_t charWidth = 6) {
  const size_t textWidth = chars * charWidth;
  if (textWidth >= slot.width) return slot.left;
  return static_cast<uint16_t>(slot.left + (slot.width - textWidth) / 2);
}

static_assert(inside(playback) && inside(source) && inside(volume) &&
                  inside(audio) && inside(bluetooth) && inside(wifi),
              "ST7789 status geometry must remain inside 284x76");
static_assert(separatedHorizontally(playback, source) &&
                  separatedHorizontally(source, volume) &&
                  separatedHorizontally(volume, audio) &&
                  separatedHorizontally(audio, bluetooth) &&
                  separatedHorizontally(bluetooth, wifi),
              "ST7789 status slots must not overlap");
static_assert(updateBarLeft + updateBarWidth <= width &&
                  updateBarTop + updateBarHeight <= height,
              "ST7789 update bar must remain inside 284x76");

}  // namespace st7789_284x76

#endif
