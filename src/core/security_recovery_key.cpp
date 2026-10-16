#include "security_recovery_key.h"

namespace voxone {
namespace security_recovery {

BootKeyDetector::BootKeyDetector(KeyTimings timings)
    : timings_(timings), state_(KeyState::Idle), stateStartedMs_(0) {}

void BootKeyDetector::enter(KeyState state, uint32_t nowMs) {
  state_ = state;
  stateStartedMs_ = nowMs;
}

bool BootKeyDetector::elapsed(uint32_t nowMs, uint32_t intervalMs) const {
  return static_cast<uint32_t>(nowMs - stateStartedMs_) >= intervalMs;
}

KeyState BootKeyDetector::begin(bool keyLow, uint32_t nowMs) {
  if (state_ != KeyState::Idle) return state_;
  enter(keyLow ? KeyState::Debounce : KeyState::Cancelled, nowMs);
  return state_;
}

KeyState BootKeyDetector::update(bool keyLow, uint32_t nowMs) {
  switch (state_) {
    case KeyState::Idle:
      return state_;
    case KeyState::Debounce:
      if (!keyLow)
        enter(KeyState::Cancelled, nowMs);
      else if (elapsed(nowMs, timings_.debounceMs))
        enter(KeyState::Hold, nowMs);
      break;
    case KeyState::Hold:
      if (!keyLow)
        enter(KeyState::Cancelled, nowMs);
      else if (elapsed(nowMs, timings_.holdMs))
        enter(KeyState::AwaitRelease, nowMs);
      break;
    case KeyState::AwaitRelease:
      if (!keyLow)
        enter(KeyState::Accepted, nowMs);
      else if (elapsed(nowMs, timings_.releaseTimeoutMs))
        enter(KeyState::StuckLow, nowMs);
      break;
    case KeyState::Accepted:
    case KeyState::Cancelled:
    case KeyState::StuckLow:
      break;
  }
  return state_;
}

bool BootKeyDetector::finished() const {
  return state_ == KeyState::Accepted || state_ == KeyState::Cancelled ||
         state_ == KeyState::StuckLow;
}

}  // namespace security_recovery
}  // namespace voxone
