#ifndef VOXONE_POWER_MANAGER_H
#define VOXONE_POWER_MANAGER_H

#include <atomic>
#include <stdint.h>

namespace voxone {
namespace power {

enum class ButtonGesture : uint8_t { None, ShortPress, LongPress };

class ButtonGestureDetector {
 public:
  static constexpr uint32_t kDebounceMs = 50;
  static constexpr uint32_t kShortPressMaximumMs = 1000;
  static constexpr uint32_t kLongPressMs = 3000;

  void reset(bool pressed, uint32_t nowMs) {
    candidatePressed_ = pressed;
    stablePressed_ = pressed;
    candidateSinceMs_ = nowMs;
    pressStartedMs_ = nowMs;
    longReported_ = false;
  }

  ButtonGesture update(bool pressed, uint32_t nowMs) {
    if (pressed != candidatePressed_) {
      candidatePressed_ = pressed;
      candidateSinceMs_ = nowMs;
    }
    if (candidatePressed_ != stablePressed_ &&
        nowMs - candidateSinceMs_ >= kDebounceMs) {
      stablePressed_ = candidatePressed_;
      if (stablePressed_) {
        pressStartedMs_ = nowMs;
        longReported_ = false;
      } else {
        const uint32_t heldMs = nowMs - pressStartedMs_;
        if (!longReported_ && heldMs < kShortPressMaximumMs)
          return ButtonGesture::ShortPress;
      }
    }
    if (stablePressed_ && !longReported_ &&
        nowMs - pressStartedMs_ >= kLongPressMs) {
      longReported_ = true;
      return ButtonGesture::LongPress;
    }
    return ButtonGesture::None;
  }

 private:
  bool candidatePressed_ = false;
  bool stablePressed_ = false;
  bool longReported_ = false;
  uint32_t candidateSinceMs_ = 0;
  uint32_t pressStartedMs_ = 0;
};

}  // namespace power
}  // namespace voxone

class PowerManager {
 public:
  void begin();
  void loop();
  void requestWakeup();
  bool standby() const { return standby_.load(std::memory_order_acquire); }

 private:
  void beginShutdown();
  void enterStandby();
  void wakeup();

  voxone::power::ButtonGestureDetector button_;
  std::atomic<bool> wakeRequested_{false};
  std::atomic<bool> standby_{false};
  bool shutdownNotice_ = false;
};

extern PowerManager powerManager;

#endif
