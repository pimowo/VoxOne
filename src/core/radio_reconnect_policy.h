#ifndef VOXONE_RADIO_RECONNECT_POLICY_H
#define VOXONE_RADIO_RECONNECT_POLICY_H

#include <stdint.h>

namespace voxone {
namespace radio {

constexpr uint32_t kStreamInactivityTimeoutMs = 10000;

class ReconnectPolicy {
 public:
  void reset() {
    failures_ = 0;
    pending_ = false;
    dueAtMs_ = 0;
  }

  void cancel() { reset(); }

  bool schedule(uint32_t nowMs) {
    if (pending_) return false;
    dueAtMs_ = nowMs + delayForFailure(failures_);
    pending_ = true;
    if (failures_ < kDelayCount) ++failures_;
    return true;
  }

  bool takeDue(uint32_t nowMs, bool allowed, bool networkOnline) {
    if (!allowed) {
      cancel();
      return false;
    }
    if (!pending_ || !networkOnline ||
        static_cast<int32_t>(nowMs - dueAtMs_) < 0) return false;
    pending_ = false;
    return true;
  }

  bool pending() const { return pending_; }
  uint8_t failures() const { return failures_; }
  uint32_t dueAtMs() const { return dueAtMs_; }

  static uint32_t delayForFailure(uint8_t failures) {
    static constexpr uint32_t delays[] = {1000, 2000, 5000, 10000, 15000, 30000};
    const uint8_t index = failures < kDelayCount ? failures : kDelayCount - 1;
    return delays[index];
  }

 private:
  static constexpr uint8_t kDelayCount = 6;
  uint32_t dueAtMs_ = 0;
  uint8_t failures_ = 0;
  bool pending_ = false;
};

inline bool streamIsDead(bool socketConnected, bool bufferedFrameAvailable,
                         uint32_t nowMs, uint32_t lastDataMs) {
  if (bufferedFrameAvailable) return false;
  return !socketConnected ||
         static_cast<uint32_t>(nowMs - lastDataMs) >= kStreamInactivityTimeoutMs;
}

}  // namespace radio
}  // namespace voxone

#endif
