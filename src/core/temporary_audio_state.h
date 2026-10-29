#ifndef VOXONE_TEMPORARY_AUDIO_STATE_H
#define VOXONE_TEMPORARY_AUDIO_STATE_H

#include <atomic>
#include <stdint.h>

// Only Player begins/finishes requests. Network events may signal completion.
// Base source, station and radio intent remain exclusively in SourceManager.
class TemporaryAudioState {
 public:
  uint32_t begin() {
    const bool replacing = busy();
    generation_ = (generation_ % 0x7fffffffU) + 1;
    state_.store(generation_ << 1);
    restoring_.store(false);
    radioMetadataAllowed_.store(false);
    if (!replacing) radioRestoreSuppressed_.store(false);
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
  bool suppressRadioRestore() {
    if (!busy()) return false;
    radioRestoreSuppressed_.store(true);
    return true;
  }
  // Decoder callbacks with a generation token belong to temporary audio and
  // must never overwrite the selected RADIO station. After a stopped/BT
  // restore the quarantine deliberately remains active: the stopped decoder
  // has no legitimate metadata left to publish. A real RADIO connection
  // explicitly opens the gate immediately before connecttohost().
  bool radioMetadataAllowed(uint32_t callbackToken) const {
    return callbackToken == 0 && radioMetadataAllowed_.load();
  }
  void allowRadioMetadata() { radioMetadataAllowed_.store(true); }
  // Consume restoration once, using CURRENT base intent. Wait only for network.
  bool takeRadioRestore(bool radioAllowed, bool networkReady, bool blocked) {
    if (active() || !restoring_.load()) return false;
    if (radioRestoreSuppressed_.exchange(false) || blocked || !radioAllowed) {
      restoring_.store(false);
      return false;
    }
    return networkReady && restoring_.exchange(false);
  }

 private:
  uint32_t generation_ = 0;
  std::atomic<uint32_t> state_{0}; // token << 1 | terminal
  std::atomic<bool> restoring_{false};
  std::atomic<bool> radioRestoreSuppressed_{false};
  std::atomic<bool> radioMetadataAllowed_{true};
};

inline bool bluetoothOwnsAudio(bool selected, bool temporaryActive) {
  return selected && !temporaryActive;
}

#endif
