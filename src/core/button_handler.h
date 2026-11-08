#ifndef VOXONE_BUTTON_HANDLER_H
#define VOXONE_BUTTON_HANDLER_H

#include <stdint.h>

namespace voxone {
namespace power {

enum class ButtonEvent : uint8_t {
  BUTTON_NONE,
  BUTTON_CLICK,
  BUTTON_DOUBLE_CLICK,
  BUTTON_LONG_PRESS,
};

class ButtonHandler {
 public:
  static constexpr uint32_t kDebounceMs = 50;
  static constexpr uint32_t kClickMaximumMs = 400;
  static constexpr uint32_t kDoubleClickIntervalMs = 400;
  static constexpr uint32_t kLongPressMs = 3000;

  void reset(bool pressed, uint32_t nowMs);
  ButtonEvent update(bool pressed, uint32_t nowMs);

 private:
  bool candidatePressed_ = false;
  bool stablePressed_ = false;
  bool longPressReported_ = false;
  bool clickPending_ = false;
  bool secondClickEligible_ = false;
  uint32_t candidateSinceMs_ = 0;
  uint32_t pressStartedMs_ = 0;
  uint32_t firstClickReleasedMs_ = 0;
};

}  // namespace power
}  // namespace voxone

#endif
