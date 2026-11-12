#include "../src/core/bt_audio_retry_state.h"
#include "../src/core/bt_audio_route_state.h"

#include <cassert>
#include <cstdint>

int main() {
  BtAudioRetryState retry;
  assert(retry.canAttempt(0));

  uint32_t now = 100;
  for (uint8_t attempt = 1; attempt < BtAudioRetryState::MaxAttempts;
       ++attempt) {
    retry.failed(now);
    assert(retry.attempts() == attempt);
    assert(retry.pending() && !retry.exhausted());
    const uint32_t delay = BtAudioRetryState::BaseDelayMs << (attempt - 1U);
    assert(retry.dueAtMs() == now + delay);
    assert(!retry.canAttempt(now + delay - 1U));
    assert(retry.canAttempt(now + delay));
    now += delay;
  }
  retry.failed(now);
  assert(retry.attempts() == BtAudioRetryState::MaxAttempts);
  assert(!retry.pending() && retry.exhausted());
  assert(!retry.canAttempt(UINT32_MAX));

  retry.reset();
  retry.failed(UINT32_MAX - 500U);
  assert(!retry.canAttempt(498));
  assert(retry.canAttempt(499));
  retry.started();
  assert(retry.canAttempt(499));
  assert(retry.attempts() == 1);  // Reset only after real PCM arrives.
  retry.reset();
  assert(retry.attempts() == 0 && retry.canAttempt(0));

  assert(!btPcmStreamStarted(false, 0, 0));
  assert(btPcmStreamStarted(true, 1000, 1000));
  assert(btPcmStreamStarted(true, 1000,
                            1000 + kBtPcmActivityTimeoutMs));
  assert(!btPcmStreamStarted(true, 1000,
                             1001 + kBtPcmActivityTimeoutMs));
  assert(btPcmStreamStarted(true, UINT32_MAX - 50U, 25));
}
