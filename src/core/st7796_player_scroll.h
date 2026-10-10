#ifndef VOXONE_ST7796_PLAYER_SCROLL_H
#define VOXONE_ST7796_PLAYER_SCROLL_H

#include <stdint.h>

inline bool st7796PlayerScrollReady(bool playerPageReady, bool playerMode,
                                   bool schedulerEnabled, const void* meta,
                                   const void* title1, const void* title2) {
  return playerPageReady && playerMode && schedulerEnabled && meta && title1 && title2;
}

class St7796PlayerScroll {
 public:
  static constexpr uint32_t kStartDelayMs = 2500;  // Existing ST7796 480x320 timing.
  static constexpr uint32_t kStepIntervalMs = 50;
  static constexpr uint8_t kStepPixels = 2;

  enum class Action : uint8_t { None, Start, Step };
  struct Event {
    Action action;
    int8_t row;
  };

  void enter() {
    enabled_ = true;
    active_ = -1;
    next_ = 0;
    moving_ = false;
  }

  void leave() {
    enabled_ = false;
    active_ = -1;
    next_ = 0;
    moving_ = false;
  }

  void textChanged(uint8_t row) {
    if (!enabled_ || row >= 3) return;
    active_ = -1;
    next_ = row;
    moving_ = false;
  }

  void cycleFinished() {
    if (active_ < 0) return;
    next_ = static_cast<uint8_t>((active_ + 1) % 3);
    active_ = -1;
    moving_ = false;
  }

  Event tick(uint32_t now, const bool needsScroll[3]) {
    if (!enabled_) return {Action::None, -1};
    if (active_ >= 0 && !needsScroll[active_]) {
      next_ = static_cast<uint8_t>((active_ + 1) % 3);
      active_ = -1;
    }
    if (active_ < 0) {
      for (uint8_t offset = 0; offset < 3; ++offset) {
        const uint8_t row = static_cast<uint8_t>((next_ + offset) % 3);
        if (!needsScroll[row]) continue;
        active_ = row;
        turnAt_ = now;
        moving_ = false;
        return {Action::Start, active_};
      }
      return {Action::None, -1};
    }
    if (!moving_) {
      if (static_cast<uint32_t>(now - turnAt_) < kStartDelayMs)
        return {Action::None, active_};
      moving_ = true;
      stepAt_ = now;
      return {Action::None, active_};
    }
    if (static_cast<uint32_t>(now - stepAt_) < kStepIntervalMs)
      return {Action::None, active_};
    stepAt_ = now;
    return {Action::Step, active_};
  }

  int8_t active() const { return active_; }
  bool enabled() const { return enabled_; }

 private:
  bool enabled_ = false;
  bool moving_ = false;
  int8_t active_ = -1;
  uint8_t next_ = 0;
  uint32_t turnAt_ = 0;
  uint32_t stepAt_ = 0;
};

#endif
