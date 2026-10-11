#ifndef VOXONE_WIFI_REASSOC_POLICY_H
#define VOXONE_WIFI_REASSOC_POLICY_H

#include <stdint.h>

namespace voxone::wifi {

class ReassocPolicy {
 public:
  bool arm(uint32_t nowMs) {
    if (pending_) return false;
    pending_ = true;
    attempts_ = 0;
    dueAtMs_ = nowMs + delayForAttempt(0);
    return true;
  }

  bool takeDue(uint32_t nowMs, bool staActive, bool networkReady) {
    if (networkReady) {
      reset();
      return false;
    }
    if (!staActive || !pending_ ||
        static_cast<int32_t>(nowMs - dueAtMs_) < 0)
      return false;

    if (attempts_ < kDelayCount) ++attempts_;
    dueAtMs_ = nowMs + delayForAttempt(attempts_);
    return true;
  }

  void connected() { reset(); }

  void reset() {
    pending_ = false;
    attempts_ = 0;
    dueAtMs_ = 0;
  }

  bool pending() const { return pending_; }
  uint8_t attempts() const { return attempts_; }
  uint32_t dueAtMs() const { return dueAtMs_; }

  static uint32_t delayForAttempt(uint8_t attempts) {
    static constexpr uint32_t delays[] = {
        1000, 2000, 5000, 10000, 15000, 30000};
    const uint8_t index =
        attempts < kDelayCount ? attempts : kDelayCount - 1;
    return delays[index];
  }

 private:
  static constexpr uint8_t kDelayCount = 6;
  uint32_t dueAtMs_ = 0;
  uint8_t attempts_ = 0;
  bool pending_ = false;
};

}  // namespace voxone::wifi

#endif
