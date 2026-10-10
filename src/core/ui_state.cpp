#include "ui_state.h"

#include <Arduino.h>

#include "options.h"
#include "config.h"
#include "display.h"
#include "network.h"
#include "source_manager.h"
#include "ui_timeout_config.h"
#include "update_display_view.h"

namespace {
portMUX_TYPE uiStateMux = portMUX_INITIALIZER_UNLOCKED;

bool bluetoothTransportAvailableForUi() {
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  return bluetoothSourceSelected() && bluetoothTransportAvailable();
#else
  return false;
#endif
}

uint32_t returnTimeoutForMode(displayMode_e mode) {
  if (mode == VOL) {
#if DSP_MODEL==DSP_ST7789_76
    return 10;
#else
    return 3;
#endif
  }
  if (mode == STATIONS) return uiTimeoutConfig().stationListSeconds;
  if (mode == BT_TRANSPORT) return uiTimeoutConfig().btTransportSeconds;
  return 0;
}
}  // namespace

UiState uiState;

bool requestUiMode(displayMode_e requested) {
  const UpdateProgressSnapshot update = updateProgress();
  const UiModeContext context{network.status == CONNECTED, update.locked,
                              bluetoothTransportAvailableForUi()};
  const uint16_t stationCount =
      requested == STATIONS ? config.playlistLength() : 0;
  const uint16_t initialStation =
      requested == STATIONS ? config.lastStation() : 0;
  const uint32_t returnSeconds = returnTimeoutForMode(requested);
  const uint32_t now = returnSeconds != 0 ? millis() : 0;

  portENTER_CRITICAL(&uiStateMux);
  const bool changed = uiState.requestMode(requested, context);
  if (changed) {
    if (requested == STATIONS)
      uiState.beginStationSelection(initialStation, stationCount);
    if (returnSeconds != 0)
      uiState.armReturnToPlayer(now, returnSeconds, requested);
  }
  portEXIT_CRITICAL(&uiStateMux);
  return changed;
}

bool transitionUiMode(displayMode_e requested) {
  if (!requestUiMode(requested)) return false;
  display.queueModeRender(requested);
  return true;
}

void resetUiReturnTimeout() {
  const uint32_t now = millis();
  portENTER_CRITICAL(&uiStateMux);
  uiState.resetReturnToPlayer(now);
  portEXIT_CRITICAL(&uiStateMux);
}

void armUiReturnTimeout(uint32_t seconds) {
  const uint32_t now = millis();
  portENTER_CRITICAL(&uiStateMux);
  uiState.armReturnToPlayer(now, seconds, uiState.mode());
  portEXIT_CRITICAL(&uiStateMux);
}

void normalizeUiStationSelection() {
  const uint16_t stationCount = config.playlistLength();
  portENTER_CRITICAL(&uiStateMux);
  uiState.beginStationSelection(uiState.selectedStation(), stationCount);
  portEXIT_CRITICAL(&uiStateMux);
}

bool uiReturnToPlayerDue(uint32_t now) {
  portENTER_CRITICAL(&uiStateMux);
  const bool due = uiState.returnToPlayerDue(now);
  portEXIT_CRITICAL(&uiStateMux);
  return due;
}

bool uiSystemModeRequest(const UpdateProgressSnapshot& update,
                         displayMode_e& requested) {
  if (updateScreenOwnsDisplay(update) && uiState.mode() != UPDATING) {
    requested = UPDATING;
    return true;
  }
  if (uiState.mode() == UPDATING && updateScreenReturnsToPlayer(update)) {
    requested = PLAYER;
    return true;
  }
  return false;
}
