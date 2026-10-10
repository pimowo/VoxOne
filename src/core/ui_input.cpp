#include "ui_input.h"

#include "options.h"
#include "config.h"
#include "display.h"
#include "network.h"
#include "player.h"
#include "source_manager.h"
#include "../hardware/hardware_descriptor.h"

namespace {

UiInputContext currentUiInputContext() {
  const auto& capabilities = voxone::hardware::hardwareCapabilities();
  bool bluetoothSelected = false;
  bool bluetoothConnected = false;
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  if (capabilities.supportsVoxOneBt) {
    bluetoothSelected = bluetoothSourceSelected();
    bluetoothConnected = bluetoothTransportAvailable();
  }
#endif
  return {display.mode(), network.status == CONNECTED,
          capabilities.hasLocalDisplay(), capabilities.supportsVoxOneBt,
          bluetoothSelected, bluetoothConnected};
}

int8_t signedSteps(UiInputAction action, int8_t steps) {
  int magnitude = steps;
  if (magnitude < 0) magnitude = -magnitude;
  if (magnitude == 0) magnitude = 1;
  if (magnitude > 127) magnitude = 127;
  return action == UiInputAction::VolumeDown
             ? static_cast<int8_t>(-magnitude)
             : static_cast<int8_t>(magnitude);
}

void stepVolume(UiInputAction action, int8_t steps) {
  const int8_t direction = signedSteps(action, steps);
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  if (voxone::hardware::hardwareCapabilities().supportsVoxOneBt &&
      bluetoothSourceSelected()) {
    const bool wasMuted = player.isMuted();
    const int8_t step = wasMuted ? (direction > 0 ? 1 : -1) : direction;
    if (sourceManagerStepBluetoothVolume(step)) {
      player.setMuted(false);
      display.putRequest(NEWMODE, VOL);
      display.putRequest(DRAWVOL);
    } else if (wasMuted) {
      player.stepUserVol(step);
      display.putRequest(NEWMODE, VOL);
    }
    return;
  }
#endif
  if (voxone::hardware::hardwareCapabilities().hasLocalDisplay())
    display.putRequest(NEWMODE, VOL);
  player.stepUserVol(direction);
}

void moveStation(bool next) {
  display.resetQueue();
  int item = next ? display.currentPlItem + 1 : display.currentPlItem - 1;
  const uint16_t count = config.playlistLength();
  if (item < 1) item = count;
  if (item > count) item = 1;
  display.currentPlItem = item;
  display.putRequest(DRAWPLAYLIST, item);
}

}  // namespace

void dispatchUiInput(UiInputEvent event, int8_t steps) {
  const UiInputDecision decision = resolveUiInput(currentUiInputContext(), event);
  if (decision.cancelNumberEntry) {
    display.numOfNextStation = 0;
    display.putRequest(NEWMODE, PLAYER);
  }

  switch (decision.action) {
    case UiInputAction::VolumeDown:
    case UiInputAction::VolumeUp:
      stepVolume(decision.action, steps);
      break;
    case UiInputAction::StationPrevious:
      moveStation(false);
      break;
    case UiInputAction::StationNext:
      moveStation(true);
      break;
    case UiInputAction::StationSelect:
      display.putRequest(NEWMODE, PLAYER);
      display.putRequest(CLOSEPLAYLIST, display.currentPlItem);
      break;
    case UiInputAction::TogglePlayback:
      player.toggle();
      break;
    case UiInputAction::ToggleMute:
      player.toggleMute();
      break;
    case UiInputAction::BluetoothPrevious:
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
      display.putRequest(RESETIDLE);
      sourceManagerTransport(BtTransportInput::Previous);
#endif
      break;
    case UiInputAction::BluetoothNext:
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
      display.putRequest(RESETIDLE);
      sourceManagerTransport(BtTransportInput::Next);
#endif
      break;
    case UiInputAction::BluetoothToggle:
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
      if (display.mode() == BT_TRANSPORT) display.putRequest(RESETIDLE);
      sourceManagerTransport(BtTransportInput::Toggle);
#endif
      break;
    case UiInputAction::CycleSource:
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
      cycleNextSource();
      display.putRequest(NEWMODE, PLAYER);
#endif
      break;
    case UiInputAction::OpenStations:
      display.putRequest(NEWMODE, STATIONS);
      break;
    case UiInputAction::OpenBluetoothTransport:
      display.putRequest(NEWMODE, BT_TRANSPORT);
      break;
    case UiInputAction::ShowPlayer:
    case UiInputAction::WakePlayer:
      display.putRequest(NEWMODE, PLAYER);
      break;
    case UiInputAction::None:
      break;
  }
}
