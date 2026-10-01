#ifndef VOXONE_MUTE_STATE_H
#define VOXONE_MUTE_STATE_H

#include <atomic>
#include <stdint.h>

// Runtime output mute. The saved USER volume remains independent of this state.
class MuteState {
 public:
  bool active() const { return active_.load(); }
  void set(bool active) { active_.store(active); }
  bool toggle() {
    const bool next = !active();
    set(next);
    return next;
  }
  uint8_t outputVolume(uint8_t volume) const { return active() ? 0 : volume; }
  bool outputSilent(uint8_t userVolume) const { return active() || userVolume == 0; }
  uint8_t stepUserVolume(uint8_t user, int8_t direction) {
    int next = static_cast<int>(user) + (direction > 0 ? 1 : -1);
    if (next < 0) next = 0;
    if (next > 100) next = 100;
    set(false);
    return static_cast<uint8_t>(next);
  }

 private:
  std::atomic<bool> active_{false};
};

#endif
