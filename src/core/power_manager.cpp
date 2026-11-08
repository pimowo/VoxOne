#include "power_manager.h"

#include <Arduino.h>
#include <esp_log.h>

#include "options.h"
#include "config.h"
#include "display.h"
#include "player.h"
#include "ui_menu.h"
#include "ui_state.h"
#include "update_progress.h"

namespace {
constexpr char kTag[] = "PowerManager";

bool powerButtonPressed() {
  return POWER_BUTTON_PIN >= 0 && digitalRead(POWER_BUTTON_PIN) == LOW;
}

void setPowerRelay(bool enabled) {
  if (POWER_RELAY_PIN >= 0)
    digitalWrite(POWER_RELAY_PIN, enabled ? HIGH : LOW);
}
}  // namespace

PowerManager powerManager;

void PowerManager::begin() {
  if (POWER_RELAY_PIN >= 0) {
    pinMode(POWER_RELAY_PIN, OUTPUT);
    setPowerRelay(true);
  }
  if (POWER_BUTTON_PIN >= 0) pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
  button_.reset(powerButtonPressed(), millis());
}

void PowerManager::loop() {
  const uint32_t nowMs = millis();
  if (wakeRequested_.exchange(false, std::memory_order_acq_rel)) wakeup();

  if (POWER_BUTTON_PIN >= 0) {
    const auto event = button_.update(powerButtonPressed(), nowMs);
    if (!updateLockActive() && !shutdownNotice_) {
      if (event == voxone::power::ButtonEvent::BUTTON_CLICK) {
        if (uiMenu.visible()) {
          const char* selectedLabel = uiMenu.selectedLabel();
          const auto action =
              uiMenu.handle(voxone::ui_menu::MenuCommand::SELECT);
          if (action == voxone::ui_menu::MenuAction::ENTERED_SUBMENU)
            ESP_LOGI(kTag, "Opened submenu: %s", selectedLabel);
          else if (action == voxone::ui_menu::MenuAction::SELECTED)
            ESP_LOGI(kTag, "Selected: %s", selectedLabel);
        } else if (standby()) {
          wakeup();
        } else {
          player.toggleMute();
        }
      } else if (event == voxone::power::ButtonEvent::BUTTON_DOUBLE_CLICK) {
        if (uiMenu.visible()) {
          uiMenu.handle(voxone::ui_menu::MenuCommand::BACK);
        } else if (uiMenu.open()) {
          if (!transitionUiMode(MENU)) uiMenu.close();
          else ESP_LOGI(kTag, "Menu opened");
        }
      } else if (event == voxone::power::ButtonEvent::BUTTON_LONG_PRESS) {
        if (uiMenu.visible()) {
          uiMenu.handle(voxone::ui_menu::MenuCommand::CLOSE);
          transitionUiMode(PLAYER);
        } else if (!standby()) {
          beginShutdown();
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
                voxone::power_mode::apiName(mode), POWER_RELAY_PIN);
}

void PowerManager::wakeup() {
  if (!standby() && !shutdownNotice_) return;
  if (shutdownNotice_) shutdownNotice_ = false;
  display.cancelShutdownSequence();
  setPowerRelay(true);
  config.setDspOn(true, false);
  player.setMuted(false);
  standby_.store(false, std::memory_order_release);
  transitionUiMode(PLAYER);
  Serial.println("##[POWER]# awake");
}
