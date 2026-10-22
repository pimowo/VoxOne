#ifndef VOXONE_SECURITY_RECOVERY_KEY_H
#define VOXONE_SECURITY_RECOVERY_KEY_H

#include <stdint.h>

namespace voxone {
namespace security_recovery {

enum class KeyState : uint8_t {
  Idle,
  Debounce,
  Accepted,
  Cancelled,
};

struct KeyTimings {
  uint32_t debounceMs;
};

constexpr KeyTimings kDefaultKeyTimings = {50U};

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
