#ifndef VOXONE_UI_STATE_H
#define VOXONE_UI_STATE_H

#include <atomic>
#include <stdint.h>

#include "common.h"
#include "return_player_timeout.h"
#include "update_progress.h"

struct UiModeContext {
  bool networkConnected;
  bool updateLocked;
  bool bluetoothTransportAvailable;
};

// Logical local-UI state. Rendering geometry and widget state deliberately do
// not belong here.
class UiState {
 public:
  UiState() : mode_(PLAYER), selectedStation_(0), pendingStationNumber_(0) {}

  displayMode_e mode() const { return mode_.load(); }
  uint16_t selectedStation() const { return selectedStation_.load(); }
  uint16_t pendingStationNumber() const {
    return pendingStationNumber_.load();
  }

  bool requestMode(displayMode_e requested, const UiModeContext& context) {
    const displayMode_e current = mode();
    if (context.updateLocked && requested != UPDATING) return false;
    if (requested != UPDATING && !context.networkConnected) return false;
    if (requested == BT_TRANSPORT && !context.bluetoothTransportAvailable)
      return false;
    if (requested == current) return false;
    cancelReturnToPlayer();
    mode_.store(requested);
    if (requested == PLAYER) clearPendingStationNumber();
    return true;
  }

  void beginStationSelection(uint16_t selected, uint16_t count) {
    selectedStation_.store(normalizeStation(selected, count));
  }

  uint16_t moveStationSelection(int8_t direction, uint16_t count) {
    if (count == 0) {
      selectedStation_.store(0);
      return 0;
    }
    uint16_t selected = normalizeStation(selectedStation(), count);
    if (direction > 0)
      selected = selected == count ? 1 : static_cast<uint16_t>(selected + 1);
    else if (direction < 0)
      selected = selected == 1 ? count : static_cast<uint16_t>(selected - 1);
    selectedStation_.store(selected);
    return selected;
  }

  void setPendingStationNumber(uint16_t station) {
    pendingStationNumber_.store(station);
  }
  void clearPendingStationNumber() { pendingStationNumber_.store(0); }

  void armReturnToPlayer(uint32_t now, uint32_t seconds,
                         displayMode_e expectedMode) {
    returnSeconds_ = seconds;
    returnMode_ = expectedMode;
    returnTimer_.armForMode(now, seconds, expectedMode);
  }

  void resetReturnToPlayer(uint32_t now) {
    if (returnSeconds_ == 0 || mode() != returnMode_) return;
    returnTimer_.armForMode(now, returnSeconds_, returnMode_);
  }

  void cancelReturnToPlayer() {
    returnTimer_.cancel();
    returnSeconds_ = 0;
    returnMode_ = PLAYER;
  }

  bool returnToPlayerDue(uint32_t now) {
    const bool due = returnTimer_.poll(now, mode());
    if (due) {
      returnSeconds_ = 0;
      returnMode_ = PLAYER;
    }
    return due;
  }

 private:
  static uint16_t normalizeStation(uint16_t selected, uint16_t count) {
    if (count == 0) return 0;
    return selected >= 1 && selected <= count ? selected : 1;
  }

  std::atomic<displayMode_e> mode_;
  std::atomic<uint16_t> selectedStation_;
  std::atomic<uint16_t> pendingStationNumber_;
  ReturnPlayerTimeout returnTimer_;
  uint32_t returnSeconds_ = 0;
  displayMode_e returnMode_ = PLAYER;
};

extern UiState uiState;

// Runtime policy around the neutral state object. These functions preserve
// current board/display timeout values without making Display their owner.
bool requestUiMode(displayMode_e requested);
bool transitionUiMode(displayMode_e requested);
void resetUiReturnTimeout();
void armUiReturnTimeout(uint32_t seconds);
void normalizeUiStationSelection();
bool uiReturnToPlayerDue(uint32_t now);
bool uiSystemModeRequest(const UpdateProgressSnapshot& update,
                         displayMode_e& requested);

#endif
