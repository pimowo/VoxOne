#ifndef VOXONE_TEMPORARY_AUDIO_STATE_H
#define VOXONE_TEMPORARY_AUDIO_STATE_H

#include <atomic>
#include <stdint.h>

// Only Player begins/finishes requests. Network events may signal completion.
// Base source, station and radio intent remain exclusively in SourceManager.
class TemporaryAudioState {
 public:
  uint32_t begin() {
    generation_ = (generation_ % 0x7fffffffU) + 1;
    state_.store(generation_ << 1);
    restoring_.store(false);
    return generation_;
  }
  uint32_t token() const { return state_.load() >> 1; }
  bool active() const { return token() != 0; }
  bool busy() const { return active() || restoring_.load(); }
  bool terminal() const { return (state_.load() & 1U) != 0; }
  bool signal(uint32_t token) {
    if (!token) return false;
    uint32_t expected = token << 1;
    return state_.compare_exchange_strong(expected, expected | 1U);
  }
  bool finish(uint32_t token) {
    if (!token || token != this->token()) return false;
    restoring_.store(true); // No gap for the network recovery observer.
    state_.store(0);
    return true;
  }
  // Consume restoration once, using CURRENT base intent. Wait only for network.
  bool takeRadioRestore(bool radioAllowed, bool networkReady, bool blocked) {
    if (active() || !restoring_.load()) return false;
    if (blocked || !radioAllowed) {
      restoring_.store(false);
      return false;
    }
    return networkReady && restoring_.exchange(false);
  }

 private:
  uint32_t generation_ = 0;
  std::atomic<uint32_t> state_{0}; // token << 1 | terminal
  std::atomic<bool> restoring_{false};
};

inline bool bluetoothOwnsAudio(bool selected, bool temporaryActive) {
  return selected && !temporaryActive;
}

#endif
