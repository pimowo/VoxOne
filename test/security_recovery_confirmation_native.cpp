#include "../src/core/security_recovery_confirmation.h"

#include <cassert>
#include <cstdint>

using namespace voxone::security_recovery;

int main() {
  ConfirmationKeyDetector released;
  assert(released.begin(false, 0) == ConfirmationKeyState::Armed);
  assert(released.update(true, 1) == ConfirmationKeyState::Debounce);
  assert(released.update(false, 50) == ConfirmationKeyState::Armed);
  assert(released.update(true, 100) == ConfirmationKeyState::Debounce);
  assert(released.update(true, 174) == ConfirmationKeyState::Debounce);
  assert(released.update(true, 175) == ConfirmationKeyState::Hold);
  assert(released.update(true, 1174) == ConfirmationKeyState::Hold);
  assert(released.update(true, 1175) == ConfirmationKeyState::Accepted);
  assert(released.finished());
  assert(released.update(false, 2000) == ConfirmationKeyState::Accepted);

  ConfirmationKeyDetector heldAtStart;
  assert(heldAtStart.begin(true, 10) ==
         ConfirmationKeyState::AwaitRelease);
  assert(heldAtStart.update(true, 10 + 29999U) ==
         ConfirmationKeyState::AwaitRelease);
  assert(heldAtStart.update(false, 10 + 29999U) ==
         ConfirmationKeyState::Armed);
  assert(heldAtStart.update(true, 30010U) ==
         ConfirmationKeyState::Debounce);

  ConfirmationKeyDetector stuck;
  assert(stuck.begin(true, 20) == ConfirmationKeyState::AwaitRelease);
  assert(stuck.update(true, 20 + kRecoveryConfirmationReleaseTimeoutMs) ==
         ConfirmationKeyState::StuckLow);
  assert(stuck.finished());
  assert(stuck.update(false, 20 + kRecoveryConfirmationReleaseTimeoutMs + 1U) ==
         ConfirmationKeyState::StuckLow);

  ConfirmationKeyDetector releasedDuringHold;
  releasedDuringHold.begin(false, 0);
  releasedDuringHold.update(true, 1);
  releasedDuringHold.update(true, 1 + kRecoveryConfirmationDebounceMs);
  assert(releasedDuringHold.update(false, 500) ==
         ConfirmationKeyState::Armed);

  const uint32_t wrapStart = UINT32_MAX - 50U;
  ConfirmationKeyDetector wrap;
  wrap.begin(false, wrapStart);
  wrap.update(true, wrapStart + 1U);
  assert(wrap.update(true, wrapStart + 1U +
                               kRecoveryConfirmationDebounceMs) ==
         ConfirmationKeyState::Hold);
  assert(wrap.update(true, wrapStart + 1U +
                               kRecoveryConfirmationDebounceMs +
                               kRecoveryConfirmationHoldMs) ==
         ConfirmationKeyState::Accepted);
}
