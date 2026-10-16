#ifndef VOXONE_SECURITY_RECOVERY_KEY_H
#define VOXONE_SECURITY_RECOVERY_KEY_H

#include <stdint.h>

namespace voxone {
namespace security_recovery {

enum class KeyState : uint8_t {
  Idle,
  Debounce,
  Hold,
  AwaitRelease,
  Accepted,
  Cancelled,
  StuckLow,
};

struct KeyTimings {
  uint32_t debounceMs;
  uint32_t holdMs;
  uint32_t releaseTimeoutMs;
};

constexpr KeyTimings kDefaultKeyTimings = {75U, 3000U, 10000U};

class BootKeyDetector {
 public:
  explicit BootKeyDetector(KeyTimings timings = kDefaultKeyTimings);

  KeyState begin(bool keyLow, uint32_t nowMs);
  KeyState update(bool keyLow, uint32_t nowMs);

  KeyState state() const { return state_; }
  bool finished() const;

 private:
  void enter(KeyState state, uint32_t nowMs);
  bool elapsed(uint32_t nowMs, uint32_t intervalMs) const;

  KeyTimings timings_;
  KeyState state_;
  uint32_t stateStartedMs_;
};

}  // namespace security_recovery
}  // namespace voxone

#endif
