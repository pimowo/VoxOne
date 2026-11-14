#ifndef VOXONE_CAROUSEL_MARQUEE_H
#define VOXONE_CAROUSEL_MARQUEE_H

#include <stdint.h>

namespace voxone {
namespace carousel {

class MarqueeState {
 public:
  static constexpr uint16_t kInitialDelayMs = 1000;
  static constexpr uint16_t kFrameIntervalMs = 40;
  static constexpr uint8_t kGapGlyphs = 3;

  void reset(uint16_t textWidth, uint16_t viewportWidth,
             uint16_t glyphWidth, uint32_t now) {
    offset_ = 0;
    active_ = viewportWidth > 0 && textWidth > viewportWidth;
    cycleWidth_ = active_
        ? static_cast<uint16_t>(textWidth + glyphWidth * kGapGlyphs) : 0;
    nextFrame_ = now + kInitialDelayMs;
  }

  bool step(uint32_t now) {
    if (!active_ || !deadlineReached(now, nextFrame_)) return false;
    ++offset_;
    if (offset_ >= cycleWidth_) {
      offset_ = 0;
      nextFrame_ = now + kInitialDelayMs;
    } else {
      nextFrame_ = now + kFrameIntervalMs;
    }
    return true;
  }

  bool active() const { return active_; }
  uint16_t offset() const { return offset_; }
  uint16_t cycleWidth() const { return cycleWidth_; }

 private:
  static bool deadlineReached(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
  }

  bool active_ = false;
  uint16_t offset_ = 0;
  uint16_t cycleWidth_ = 0;
  uint32_t nextFrame_ = 0;
};

}  // namespace carousel
}  // namespace voxone

#endif
