#include "security_recovery_confirmation.h"

namespace voxone {
namespace security_recovery {

void ConfirmationKeyDetector::enter(ConfirmationKeyState state,
                                    uint32_t nowMs) {
  state_ = state;
  stateStartedMs_ = nowMs;
}

bool ConfirmationKeyDetector::elapsed(uint32_t nowMs,
                                      uint32_t intervalMs) const {
  return static_cast<uint32_t>(nowMs - stateStartedMs_) >= intervalMs;
}

ConfirmationKeyState ConfirmationKeyDetector::begin(bool keyLow,
                                                     uint32_t nowMs) {
  if (state_ != ConfirmationKeyState::Idle) return state_;
  enter(keyLow ? ConfirmationKeyState::AwaitRelease
               : ConfirmationKeyState::Armed,
        nowMs);
  return state_;
}

ConfirmationKeyState ConfirmationKeyDetector::update(bool keyLow,
                                                      uint32_t nowMs) {
  switch (state_) {
    case ConfirmationKeyState::Idle:
      break;
    case ConfirmationKeyState::AwaitRelease:
      if (!keyLow)
        enter(ConfirmationKeyState::Armed, nowMs);
      else if (elapsed(nowMs, kRecoveryConfirmationReleaseTimeoutMs))
        enter(ConfirmationKeyState::StuckLow, nowMs);
      break;
    case ConfirmationKeyState::Armed:
      if (keyLow) enter(ConfirmationKeyState::Debounce, nowMs);
      break;
    case ConfirmationKeyState::Debounce:
      if (!keyLow)
        enter(ConfirmationKeyState::Armed, nowMs);
      else if (elapsed(nowMs, kRecoveryConfirmationDebounceMs))
        enter(ConfirmationKeyState::Hold, nowMs);
      break;
    case ConfirmationKeyState::Hold:
      if (!keyLow)
        enter(ConfirmationKeyState::Armed, nowMs);
      else if (elapsed(nowMs, kRecoveryConfirmationHoldMs))
        enter(ConfirmationKeyState::Accepted, nowMs);
      break;
    case ConfirmationKeyState::Accepted:
    case ConfirmationKeyState::StuckLow:
      break;
  }
  return state_;
}

bool ConfirmationKeyDetector::finished() const {
  return state_ == ConfirmationKeyState::Accepted ||
         state_ == ConfirmationKeyState::StuckLow;
}

}  // namespace security_recovery
}  // namespace voxone
