#ifndef VOXONE_MUTE_STATE_H
#define VOXONE_MUTE_STATE_H

#include <atomic>
#include <stdint.h>

static_assert(ATOMIC_CHAR_LOCK_FREE == 2,
              "Audio mute snapshot must be lock-free");

struct MuteOutputSnapshot {
  uint8_t userVolume;
  bool muted;
  bool silent;
};

// Runtime output mute. The saved USER volume remains independent of this state.
class MuteState {
 public:
  MuteOutputSnapshot outputSnapshot() const {
    const uint8_t state = outputState_.load(std::memory_order_acquire);
    const uint8_t userVolume = state & kVolumeMask;
    const bool muted = (state & kMutedMask) != 0;
    return {userVolume, muted, muted || userVolume == 0};
  }
  bool active() const { return outputSnapshot().muted; }
  void set(bool active) {
    uint8_t current = outputState_.load(std::memory_order_relaxed);
    uint8_t desired;
    do {
      desired = static_cast<uint8_t>(
          (current & kVolumeMask) | (active ? kMutedMask : 0));
    } while (!outputState_.compare_exchange_weak(
        current, desired, std::memory_order_release,
        std::memory_order_relaxed));
  }
  void setUserVolume(uint8_t userVolume) {
    if (userVolume > 100) userVolume = 100;
    uint8_t current = outputState_.load(std::memory_order_relaxed);
    uint8_t desired;
    do {
      desired = static_cast<uint8_t>(
          (current & kMutedMask) | userVolume);
    } while (!outputState_.compare_exchange_weak(
        current, desired, std::memory_order_release,
        std::memory_order_relaxed));
  }
  bool toggle() {
    uint8_t current = outputState_.load(std::memory_order_relaxed);
    uint8_t desired;
    do {
      desired = current ^ kMutedMask;
    } while (!outputState_.compare_exchange_weak(
        current, desired, std::memory_order_release,
        std::memory_order_relaxed));
    return (desired & kMutedMask) != 0;
  }
  uint8_t outputVolume(uint8_t volume) const { return active() ? 0 : volume; }
  bool outputSilent() const { return outputSnapshot().silent; }
  bool outputSilent(uint8_t userVolume) const { return active() || userVolume == 0; }
  uint8_t stepUserVolume(uint8_t user, int8_t direction) {
    int next = static_cast<int>(user) + (direction > 0 ? 1 : -1);
    if (next < 0) next = 0;
    if (next > 100) next = 100;
    const uint8_t nextUser = static_cast<uint8_t>(next);
    outputState_.store(nextUser, std::memory_order_release);
    return nextUser;
  }

 private:
  static constexpr uint8_t kMutedMask = 0x80;
  static constexpr uint8_t kVolumeMask = 0x7f;
  std::atomic<uint8_t> outputState_{0};
};

#endif
