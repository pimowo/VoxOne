#include "ui_input.h"

#include "options.h"
#include "config.h"
#include "display.h"
#include "dlna_menu_controller.h"
#include "network.h"
#include "player.h"
#include "source_manager.h"
#include "source_menu_controller.h"
#include "ui_menu.h"
#include "ui_state.h"
#include "../hardware/hardware_descriptor.h"

#include <esp_log.h>

namespace {

constexpr char kUiInputTag[] = "UiInput";

bool dispatchMenuInput(UiInputEvent event, int8_t steps) {
  if (!uiMenu.visible()) return false;

  using voxone::ui_menu::MenuAction;
  using voxone::ui_menu::MenuCommand;
  const char* selectedLabel = uiMenu.selectedLabel();
  MenuAction action = MenuAction::NONE;
  if (event == UiInputEvent::Left) {
    action = uiMenu.handle(MenuCommand::PREVIOUS, steps);
  } else if (event == UiInputEvent::Right) {
    action = uiMenu.handle(MenuCommand::NEXT, steps);
  } else if (event == UiInputEvent::Ok) {
    action = uiMenu.handle(MenuCommand::SELECT);
  } else if (event == UiInputEvent::Double) {
    if (voxone::dlna_menu::back()) return true;
    action = uiMenu.handle(MenuCommand::BACK);
  } else if (event == UiInputEvent::Long ||
             event == UiInputEvent::VeryLong) {
    action = uiMenu.handle(MenuCommand::CLOSE);
  }

  if (action == MenuAction::SELECTED) {
    if (uiMenu.menuId() == voxone::ui_menu::MenuId::DLNA)
      voxone::dlna_menu::select();
    else if (uiMenu.menuId() == voxone::ui_menu::MenuId::SOURCE)
      voxone::source_menu::select();
    else
      ESP_LOGI(kUiInputTag, "Selected: %s", selectedLabel);
  }
  if (action == MenuAction::SETTING_CHANGED)
    config.previewMenuSettings(uiMenu.settings());
  if (action == MenuAction::ENTERED_SUBMENU) {
    ESP_LOGI(kUiInputTag, "Opened submenu: %s", selectedLabel);
    if (uiMenu.menuId() == voxone::ui_menu::MenuId::DLNA)
      voxone::dlna_menu::entered();
    else if (uiMenu.menuId() == voxone::ui_menu::MenuId::SOURCE)
      voxone::source_menu::entered();
  }
  if (action == MenuAction::BACKED)
    ESP_LOGI(kUiInputTag, "Back to main menu");
  if (action == MenuAction::CLOSED) {
    config.discardMenuTransaction();
    transitionUiMode(PLAYER);
  }
  if (action == MenuAction::SAVE_REQUESTED) {
    if (config.commitMenuTransaction()) {
      uiMenu.finishTransaction();
      transitionUiMode(PLAYER);
    } else {
      ESP_LOGE(kUiInputTag, "Menu settings persistence failed");
    }
  }
  if (action == MenuAction::DISCARD_REQUESTED) {
    config.discardMenuTransaction();
    uiMenu.finishTransaction();
    transitionUiMode(PLAYER);
  }
  return true;
}

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
  const auto runtime = config.runtimeSnapshot();
  return {uiState.mode(), network.status == CONNECTED,
          capabilities.hasLocalDisplay(), capabilities.supportsVoxOneBt,
          bluetoothSelected, bluetoothConnected,
          capabilities.supportsBtTransportUi &&
              display.supportsBluetoothTransportView(),
          sourceManagerActiveSource(),
          runtime.encoderUiReversed, runtime.lineOutEnabled};
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
  if (config.runtimeSnapshot().lineOutEnabled) return;
  const int8_t direction = signedSteps(action, steps);
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  if (voxone::hardware::hardwareCapabilities().supportsVoxOneBt &&
      bluetoothSourceSelected()) {
    const bool wasMuted = player.isMuted();
    const int8_t step = wasMuted ? (direction > 0 ? 1 : -1) : direction;
    if (sourceManagerStepBluetoothVolume(step)) {
      player.setMuted(false);
      transitionUiMode(VOL);
      display.putRequest(DRAWVOL);
    } else if (wasMuted) {
      player.stepUserVol(step);
      transitionUiMode(VOL);
    }
    return;
  }
#endif
  if (voxone::hardware::hardwareCapabilities().hasLocalDisplay())
    transitionUiMode(VOL);
  player.stepUserVol(direction);
}

void moveStation(bool next) {
  display.resetQueue();
  const uint16_t count = config.playlistLength();
  const uint16_t item = uiState.moveStationSelection(next ? 1 : -1, count);
  resetUiReturnTimeout();
  display.putRequest(DRAWPLAYLIST, item);
}

void navigateCurrentSource(SourceId source, bool next) {
  if (source == SourceId::Bluetooth) {
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
    sourceManagerTransport(next ? BtTransportInput::Next
                                : BtTransportInput::Previous);
#endif
    return;
  }
  if (source == SourceId::Dlna) {
    if (uiMenu.open(voxone::ui_menu::MenuId::DLNA)) {
      config.beginMenuTransaction();
      voxone::dlna_menu::entered();
      if (!transitionUiMode(MENU)) {
        config.discardMenuTransaction();
        uiMenu.close();
      }
    }
    return;
  }
  if (source == SourceId::Aux) return;
  // WEB and HA share Player's ordered-media navigation. HA is temporary
  // audio, so an explicit next/previous gesture hands control back to the
  // selected WEB station in that direction.
  if (next) player.next();
  else player.prev();
}

}  // namespace

void dispatchUiInput(UiInputEvent event, int8_t steps,
                     UiInputControl control) {
  if (dispatchMenuInput(event, steps)) return;
  const UiInputDecision decision =
      resolveUiInput(currentUiInputContext(), event, control);
  if (decision.cancelNumberEntry) {
    uiState.clearPendingStationNumber();
    transitionUiMode(PLAYER);
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
    case UiInputAction::StationSelect: {
      // The playlist can change after STATIONS was opened. Revalidate against
      // a fresh length snapshot before committing the selected station.
      normalizeUiStationSelection();
      const uint16_t selected = uiState.selectedStation();
      transitionUiMode(PLAYER);
      if (selected != 0) display.putRequest(CLOSEPLAYLIST, selected);
      break;
    }
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
      if (uiState.mode() == BT_TRANSPORT) display.putRequest(RESETIDLE);
      sourceManagerTransport(BtTransportInput::Toggle);
#endif
      break;
    case UiInputAction::SourcePrevious:
      navigateCurrentSource(sourceManagerActiveSource(), false);
      break;
    case UiInputAction::SourceNext:
      navigateCurrentSource(sourceManagerActiveSource(), true);
      break;
    case UiInputAction::CycleSource:
      sourceManagerRequestNextSource();
      break;
    case UiInputAction::OpenDlnaBrowser:
      if (uiMenu.open(voxone::ui_menu::MenuId::DLNA)) {
        config.beginMenuTransaction();
        voxone::dlna_menu::entered();
        if (!transitionUiMode(MENU)) {
          config.discardMenuTransaction();
          uiMenu.close();
        }
      }
      break;
    case UiInputAction::OpenVolume:
      transitionUiMode(VOL);
      display.putRequest(DRAWVOL);
      break;
    case UiInputAction::OpenStations:
      transitionUiMode(STATIONS);
      break;
    case UiInputAction::OpenBluetoothTransport:
      transitionUiMode(BT_TRANSPORT);
      break;
    case UiInputAction::ShowPlayer:
    case UiInputAction::WakePlayer:
      transitionUiMode(PLAYER);
      break;
    case UiInputAction::None:
      break;
  }
}
