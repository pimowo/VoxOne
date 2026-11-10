#include "power_manager.h"

#include <Arduino.h>
#include <esp_log.h>

#include "options.h"
#include "config.h"
#include "display.h"
#include "dlna_menu_controller.h"
#include "player.h"
#include "source_manager.h"
#include "source_menu_controller.h"
#include "ui_menu.h"
#include "ui_input.h"
#include "ui_state.h"
#include "update_progress.h"
#include "../hardware/hardware_descriptor.h"

namespace {
constexpr char kTag[] = "PowerManager";

bool powerButtonPressed() {
  const auto pin = voxone::hardware::currentHardware().power.button;
  return voxone::hardware::hasPin(pin) && digitalRead(pin) == LOW;
}

void setPowerRelay(bool enabled) {
  const auto pin = voxone::hardware::currentHardware().power.relay;
  if (voxone::hardware::hasPin(pin))
    digitalWrite(pin, enabled ? HIGH : LOW);
}

bool uiCarouselActive() {
  const displayMode_e mode = uiState.mode();
  return uiMenu.visible() || mode == MENU || mode == STATIONS ||
         mode == BT_TRANSPORT;
}

void cancelUiToPlayer() {
  config.discardMenuTransaction();
  if (uiMenu.visible()) uiMenu.close();
  transitionUiMode(PLAYER);
}
}  // namespace

PowerManager powerManager;

void PowerManager::begin() {
  const auto& pins = voxone::hardware::currentHardware().power;
  if (voxone::hardware::hasPin(pins.relay)) {
    pinMode(pins.relay, OUTPUT);
    setPowerRelay(true);
  }
  if (voxone::hardware::hasPin(pins.button))
    pinMode(pins.button, INPUT_PULLUP);
  button_.reset(powerButtonPressed(), millis());
}

void PowerManager::loop() {
  const uint32_t nowMs = millis();
  if (wakeRequested_.exchange(false, std::memory_order_acq_rel)) wakeup();

  if (voxone::hardware::hasPin(
          voxone::hardware::currentHardware().power.button)) {
    const auto event = button_.update(powerButtonPressed(), nowMs);
    if (!updateLockActive() && !shutdownNotice_) {
      if (event == voxone::power::ButtonEvent::BUTTON_CLICK) {
        if (standby()) {
          wakeup();
        } else if (uiCarouselActive()) {
          cancelUiToPlayer();
        } else {
          player.toggleMute();
        }
      } else if (event == voxone::power::ButtonEvent::BUTTON_DOUBLE_CLICK) {
        if (!standby() && uiState.mode() == PLAYER && uiMenu.open()) {
          config.beginMenuTransaction();
          if (!transitionUiMode(MENU)) {
            config.discardMenuTransaction();
            uiMenu.close();
          }
          else ESP_LOGI(kTag, "Menu opened");
        }
      } else if (event == voxone::power::ButtonEvent::BUTTON_LONG_PRESS) {
        if (!standby()) {
          if (uiState.mode() == PLAYER && !uiMenu.visible())
            beginShutdown();
          else
            dispatchUiInput(UiInputEvent::Long);
        }
      }
    }
  }

  if (shutdownNotice_ && display.shutdownSequenceComplete())
    enterStandby();
}

void PowerManager::requestWakeup() {
  wakeRequested_.store(true, std::memory_order_release);
}

void PowerManager::beginShutdown() {
  shutdownNotice_ = true;
  player.stopForShutdown();
  transitionUiMode(SHUTTING_DOWN);
  display.beginShutdownSequence();
  if (!config.persistV7())
    Serial.println("##[WARN]# POWER config persistence failed");
}

void PowerManager::enterStandby() {
  shutdownNotice_ = false;
  player.setMuted(true);
  const PowerMode mode = static_cast<PowerMode>(
      config.runtimeSnapshot().powerMode);
  if (mode == MODE_HARD_POWER) setPowerRelay(false);
  standby_.store(true, std::memory_order_release);
  Serial.printf("##[POWER]# standby mode=%s relay=%d\n",
                voxone::power_mode::apiName(mode),
                voxone::hardware::currentHardware().power.relay);
}

void PowerManager::wakeup() {
  if (!standby() && !shutdownNotice_) return;
  if (shutdownNotice_) shutdownNotice_ = false;
  display.cancelShutdownSequence();
  setPowerRelay(true);
  config.setDspOn(true, false);
  const uint8_t startupVolume = config.applyConfiguredStartupVolume();
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  sourceManagerApplyConfiguredBluetoothVolume();
#endif
  player.setMuted(false);
  player.resumeAfterShutdown();
  standby_.store(false, std::memory_order_release);
  transitionUiMode(PLAYER);
  Serial.printf("##[POWER]# awake startupVolume=%u\n",
                static_cast<unsigned>(startupVolume));
}
