#ifndef VOXONE_SOURCE_MEDIA_STATE_H
#define VOXONE_SOURCE_MEDIA_STATE_H

#include <atomic>
#include <stdint.h>

// Player owns the lifecycle of URL/file playback. Network and decoder events
// may only signal the current generation; cleanup stays on the main loop.
class SourceMediaState {
 public:
  uint32_t begin() {
    generation_ = (generation_ % 0x7fffffffU) + 1;
    state_.store(generation_ << 1);
    radioMetadataAllowed_.store(false);
    return generation_;
  }
  uint32_t token() const { return state_.load() >> 1; }
  bool active() const { return token() != 0; }
  bool busy() const { return active(); }
  bool terminal() const { return (state_.load() & 1U) != 0; }
  bool signal(uint32_t token) {
    if (!token) return false;
    uint32_t expected = token << 1;
    return state_.compare_exchange_strong(expected, expected | 1U);
  }
  bool finish(uint32_t token) {
    if (!token || token != this->token()) return false;
    state_.store(0);
    return true;
  }
  // Decoder callbacks with a generation token belong to source media and
  // must never overwrite the selected RADIO station. After a stopped/BT
  // restore the quarantine deliberately remains active: the stopped decoder
  // has no legitimate metadata left to publish. A real RADIO connection
  // explicitly opens the gate immediately before connecttohost().
  bool radioMetadataAllowed(uint32_t callbackToken) const {
    return callbackToken == 0 && radioMetadataAllowed_.load();
  }
  void allowRadioMetadata() { radioMetadataAllowed_.store(true); }
 private:
  uint32_t generation_ = 0;
  std::atomic<uint32_t> state_{0}; // token << 1 | terminal
  std::atomic<bool> radioMetadataAllowed_{true};
};

inline bool bluetoothOwnsAudio(bool selected, bool sourceMediaActive) {
  return selected && !sourceMediaActive;
}

#endif
