#ifndef VOXONE_BT_VOLUME_H
#define VOXONE_BT_VOLUME_H

#include <stdint.h>
#include <math.h>

inline uint8_t btUserToAbsolute(uint8_t user) {
  if (user > 100) user = 100;
  return static_cast<uint8_t>(lroundf(127.0f * powf(user / 100.0f, 0.85f)));
}

inline uint8_t btAbsoluteToUser(uint8_t absolute) {
  if (absolute > 127) absolute = 127;
  return static_cast<uint8_t>(lroundf(100.0f * powf(absolute / 127.0f, 1.0f / 0.85f)));
}

// Tracks one outstanding master command, not a separate BT user volume.
class BtVolumeSync {
 public:
  static constexpr uint32_t Retry2Ms = 300;
  static constexpr uint32_t Retry3Ms = 800;
  static constexpr uint32_t Retry4Ms = 1500;
  static constexpr uint32_t FinishMs = 1800;

  void connect(uint8_t user, uint32_t nowMs) {
    connected_ = true;
    awaitingConfirmation_ = true;
    connectMs_ = nowMs;
    nextRetry_ = 0;
    sentUserCommand(user);
  }

  void disconnect() {
    connected_ = false;
    awaitingConfirmation_ = false;
    nextRetry_ = 0;
    connectMs_ = 0;
    targetAbsolute_ = 0;
    lastCommandUser_ = 0;
  }

  bool pending() const { return awaitingConfirmation_; }

  bool needsUserCommand(uint8_t user) const {
    return connected_ && user != lastCommandUser_;
  }

  // An encoder command replaces the target without extending the retry budget.
  void sentUserCommand(uint8_t user) {
    lastCommandUser_ = user;
    targetAbsolute_ = btUserToAbsolute(user);
  }

  bool retryDue(uint32_t nowMs, uint8_t& absolute) {
    if (!connected_ || !awaitingConfirmation_) return false;
    const uint32_t elapsed = nowMs - connectMs_;
    if (elapsed >= FinishMs) {
      awaitingConfirmation_ = false;
      return false;
    }
    uint8_t dueIndex = nextRetry_;
    while (dueIndex < 2 && elapsed >= retryTime(dueIndex + 1))
      ++dueIndex;
    if (dueIndex >= 3 || elapsed < retryTime(dueIndex)) return false;
    nextRetry_ = dueIndex + 1;
    absolute = targetAbsolute_;
    return true;
  }

  bool acceptPhoneVolume(uint8_t absolute, uint32_t nowMs) {
    if (!connected_) return false;
    if (awaitingConfirmation_ && nowMs - connectMs_ >= FinishMs)
      awaitingConfirmation_ = false;
    if (awaitingConfirmation_) {
      // A stale phone value leaves the master target and retry schedule intact.
      if (btAbsoluteToUser(absolute) != lastCommandUser_) return false;
      awaitingConfirmation_ = false;
    }
    lastCommandUser_ = btAbsoluteToUser(absolute);
    return true;
  }

 private:
  static uint32_t retryTime(uint8_t index) {
    switch (index) {
      case 0: return Retry2Ms;
      case 1: return Retry3Ms;
      default: return Retry4Ms;
    }
  }

  bool connected_ = false;
  bool awaitingConfirmation_ = false;
  uint8_t lastCommandUser_ = 0;
  uint8_t targetAbsolute_ = 0;
  uint8_t nextRetry_ = 0;
  uint32_t connectMs_ = 0;
};

#endif
