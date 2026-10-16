#ifndef VOXONE_SECURITY_RECOVERY_CONFIRMATION_H
#define VOXONE_SECURITY_RECOVERY_CONFIRMATION_H

#include <stdint.h>

namespace voxone {
namespace security_recovery {

constexpr uint32_t kRecoveryConfirmationDebounceMs = 75U;
constexpr uint32_t kRecoveryConfirmationHoldMs = 1000U;
constexpr uint32_t kRecoveryConfirmationReleaseTimeoutMs = 30000U;

enum class ConfirmationKeyState : uint8_t {
  Idle,
  AwaitRelease,
  Armed,
  Debounce,
  Hold,
  Accepted,
  StuckLow,
};

class ConfirmationKeyDetector {
 public:
  ConfirmationKeyState begin(bool keyLow, uint32_t nowMs);
  ConfirmationKeyState update(bool keyLow, uint32_t nowMs);
  ConfirmationKeyState state() const { return state_; }
  bool finished() const;

 private:
  void enter(ConfirmationKeyState state, uint32_t nowMs);
  bool elapsed(uint32_t nowMs, uint32_t intervalMs) const;

  ConfirmationKeyState state_ = ConfirmationKeyState::Idle;
  uint32_t stateStartedMs_ = 0;
};

}  // namespace security_recovery
}  // namespace voxone

#endif
