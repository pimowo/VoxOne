#ifndef VOXONE_RETURN_PLAYER_TIMEOUT_H
#define VOXONE_RETURN_PLAYER_TIMEOUT_H

#include <cstdint>
#include "common.h"

class ReturnPlayerTimeout {
 public:
  void arm(uint32_t now, uint32_t seconds) {
    armForMode(now, seconds, PLAYER, false);
  }

  void armForMode(uint32_t now, uint32_t seconds, displayMode_e mode) {
    armForMode(now, seconds, mode, true);
  }

  void cancel() { pending_ = false; }

  bool poll(uint32_t now, displayMode_e currentMode) {
    if (!pending_) return false;
    if (modeBound_ && currentMode != mode_) {
      cancel();
      return false;
    }
    if (static_cast<uint32_t>(now - startedAt_) < delayMs_) return false;
    cancel();
    return true;
  }

 private:
  void armForMode(uint32_t now, uint32_t seconds, displayMode_e mode, bool bound) {
    if (seconds == 0) {
      cancel();
      return;
    }
    startedAt_ = now;
    delayMs_ = seconds > UINT32_MAX / 1000UL ? UINT32_MAX : seconds * 1000UL;
    mode_ = mode;
    modeBound_ = bound;
    pending_ = true;
  }

  uint32_t startedAt_ = 0;
  uint32_t delayMs_ = 0;
  displayMode_e mode_ = PLAYER;
  bool modeBound_ = false;
  bool pending_ = false;
};

#endif
