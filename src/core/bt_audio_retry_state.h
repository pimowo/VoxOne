#ifndef VOXONE_BT_AUDIO_RETRY_STATE_H
#define VOXONE_BT_AUDIO_RETRY_STATE_H

#include <stdint.h>

// Bounds consecutive I2S1 restart failures. A disconnect, format change or
// successfully received PCM resets the budget.
class BtAudioRetryState {
 public:
  static constexpr uint8_t MaxAttempts = 5;
  static constexpr uint32_t BaseDelayMs = 1000;

  void reset() {
    attempts_ = 0;
    retryPending_ = false;
    exhausted_ = false;
    dueAtMs_ = 0;
  }

  void started() { retryPending_ = false; }

  void failed(uint32_t nowMs) {
    if (attempts_ < MaxAttempts) ++attempts_;
    if (attempts_ >= MaxAttempts) {
      retryPending_ = false;
      exhausted_ = true;
      return;
    }
    retryPending_ = true;
    const uint32_t delay = BaseDelayMs << (attempts_ - 1U);
    dueAtMs_ = nowMs + delay;
  }

  bool canAttempt(uint32_t nowMs) const {
    if (exhausted_) return false;
    return !retryPending_ ||
           static_cast<int32_t>(nowMs - dueAtMs_) >= 0;
  }

  uint8_t attempts() const { return attempts_; }
  bool pending() const { return retryPending_; }
  bool exhausted() const { return exhausted_; }
  uint32_t dueAtMs() const { return dueAtMs_; }

 private:
  uint8_t attempts_ = 0;
  bool retryPending_ = false;
  bool exhausted_ = false;
  uint32_t dueAtMs_ = 0;
};

#endif
