#include "Arduino.h"
#include "core/options.h"
#include "core/config.h"
#include "core/serialcli.h"
#include "core/player.h"
#include "core/display.h"
#include "core/network.h"
#include "core/netserver.h"
#include "core/controls.h"
//#include "core/mqtt.h"
#include "core/optionschecker.h"
#include "core/timekeeper.h"
#include "core/bt_link.h"
#include "core/bt_audio_input.h"
#include "core/bt_audio_input_state.h"
#include "core/bt_runtime.h"
#include "core/source_manager.h"
#include "core/dac_mute.h"
#include "core/system_operation_state.h"
#include "core/nvs_diagnostics.h"
#include "core/dsp_runtime.h"
#include "core/ui_state.h"
#include <esp_heap_caps.h>
#include <esp_timer.h>

#if USE_OTA
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#include <NetworkUdp.h>
#else
#include <WiFiUdp.h>
#endif
#include <ArduinoOTA.h>
#endif

#if DSP_HSPI
SPIClass  SPI2(HSPI);
#endif

extern __attribute__((weak)) void yoradio_on_setup();

namespace {
constexpr uint32_t kEnduranceReportIntervalMs = 60000;
uint32_t enduranceLastReportMs = 0;
uint32_t enduranceMaximumLoopUs = 0;

void reportEnduranceHealth(uint32_t nowMs) {
  if (nowMs - enduranceLastReportMs < kEnduranceReportIntervalMs) return;

  constexpr uint32_t internalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  constexpr uint32_t psramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  const uint32_t psramTotal = ESP.getPsramSize();
  const uint32_t psramFree = psramTotal ? heap_caps_get_free_size(psramCaps) : 0;
  const uint32_t psramMinimum =
      psramTotal ? heap_caps_get_minimum_free_size(psramCaps) : 0;
  const bool wifiConnected = network.status == CONNECTED;

  Serial.printf(
      "##[ENDURANCE]# uptime=%llu heap=%lu minHeap=%lu largestHeap=%lu "
      "psram=%lu minPsram=%lu loopMaxUs=%lu loopStackHwm=%lu "
      "displayStackHwm=%lu audioBuffer=%lu playerRunning=%u wifi=%u rssi=%d\n",
      static_cast<unsigned long long>(esp_timer_get_time() / 1000000LL),
      static_cast<unsigned long>(heap_caps_get_free_size(internalCaps)),
      static_cast<unsigned long>(heap_caps_get_minimum_free_size(internalCaps)),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(internalCaps)),
      static_cast<unsigned long>(psramFree),
      static_cast<unsigned long>(psramMinimum),
      static_cast<unsigned long>(enduranceMaximumLoopUs),
      static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)),
      static_cast<unsigned long>(displayTaskStackHighWaterMark()),
      static_cast<unsigned long>(player.inBufferFilled()),
      player.isRunning() ? 1U : 0U, wifiConnected ? 1U : 0U,
      wifiConnected ? WiFi.RSSI() : -127);

  enduranceMaximumLoopUs = 0;
  enduranceLastReportMs = nowMs;
}
}  // namespace

#if USE_OTA
void setupOTA(){
  if(strlen(config.store.mdnsname)>0)
    ArduinoOTA.setHostname(config.store.mdnsname);
#ifdef OTA_PASS
  ArduinoOTA.setPassword(OTA_PASS);
#endif
  ArduinoOTA
    .onStart([]() {
      player.sendCommand({PR_STOP, 0});
      transitionUiMode(UPDATING);
      serialCli.printf("Start OTA updating %s\n", ArduinoOTA.getCommand() == U_FLASH?"firmware":"filesystem");
    })
    .onEnd([]() {
      serialCli.printf("\nEnd OTA update, Rebooting...\n");
      ESP.restart();
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      serialCli.printf("Progress OTA: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      serialCli.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        serialCli.printf("Auth Failed\n");
      } else if (error == OTA_BEGIN_ERROR) {
        serialCli.printf("Begin Failed\n");
      } else if (error == OTA_CONNECT_ERROR) {
        serialCli.printf("Connect Failed\n");
      } else if (error == OTA_RECEIVE_ERROR) {
        serialCli.printf("Receive Failed\n");
      } else if (error == OTA_END_ERROR) {
        serialCli.printf("End Failed\n");
      }
    });
  ArduinoOTA.begin();
}
#endif

void setup() {
  Serial.begin(115200);
#if defined(VOXONE_PROFILE_A0)
  dacMute.begin();
#endif
#if defined(VOXONE_PROFILE_A0) && defined(RGB_BUILTIN) && \
    defined(PIN_NEOPIXEL) && PIN_NEOPIXEL == 48
  neopixelWrite(RGB_BUILTIN, 0, 0, 0);
#endif
  if(REAL_LEDBUILTIN!=255) pinMode(REAL_LEDBUILTIN, OUTPUT);
  if (yoradio_on_setup) yoradio_on_setup();
  config.init();
  btRuntime.start();
#if defined(VOXONE_PROFILE_A0)
  voxone::dsp::initDspRuntime({config.store.bass, config.store.middle,
                              config.store.trebble});
#endif
  display.init();
  player.init();
  network.begin();
  #if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
    if (btRuntime.physicalStarted()) {
      btLink.begin();
      sourceManagerBegin();
    }
  #endif
  #if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE && VOXONE_BT_I2S_RX_ENABLED
    if (btRuntime.physicalStarted()) btAudioInput.begin();
  #endif
  logNvsStats();
  if (network.status != CONNECTED) {
    netserver.begin();
    initControls();
    display.putRequest(DSP_START);
    while(!display.ready()) delay(10);
    return;
  }
  config.initRadioPlaylist();
  netserver.begin();
  initControls();
  display.putRequest(DSP_START);
  while(!display.ready()) delay(10);
  #if USE_OTA
    setupOTA();
  #endif
  player.lockOutput=false;
  // Preserve the selected station, but always enter the base source in STOP.
}

void loop() {
  const uint32_t loopStartedUs = micros();
  displayMode_e systemMode;
  if (uiSystemModeRequest(updateProgress(), systemMode))
    transitionUiMode(systemMode);
  timekeeper.loop1();
  serialCli.loop();
  #if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
    if (btRuntime.physicalStarted()) {
      btLink.loop();
      // STATUS_BEGIN clears session fields before STATUS_END completes them.
      // Do not let consumers treat that partial snapshot as a disconnect.
      if (!btLink.firmwareUpdateInProgress() &&
          !btLink.hasIncompleteOnlineSnapshot()) sourceManagerLoop();
    }
    netserver.serviceBtFirmwareUpdate();
  #endif
  netserver.serviceUpdateRestart();
  #if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE && VOXONE_BT_I2S_RX_ENABLED
    if (btRuntime.physicalStarted() &&
        !btLink.hasIncompleteOnlineSnapshot())
      btAudioInput.loop(btLink.state(),
                        bluetoothOwnsAudio(bluetoothSourceSelected(), player.temporaryActive()),
                        player.getSampleRate(), millis());
  #endif
  if (network.status == CONNECTED || player.temporaryBusy()) {
    player.loop();
#if USE_OTA
    ArduinoOTA.handle();
#endif
  }
#if defined(VOXONE_PROFILE_A0)
  const BtLinkState& bt = btLink.state();
  const BtRuntimeStatus btStatus = btRuntime.status(bt.runtimeAvailable,
                                                    bt.connected);
  const DacPlaybackState dacPlayback = dacPlaybackForSource(
      bluetoothOwnsAudio(bluetoothSourceSelected(), player.temporaryActive()), player.isRunning(),
      bluetoothAudioOutputAllowed() &&
          !btLink.hasIncompleteOnlineSnapshot() && btStatus.btOnline &&
          btStatus.btConnected && btAudioDesiredRate(bt) != 0,
      bt.playback);
  dacMute.update(dacPlayback, systemUpdateAudioBlocked());
#endif
  loopControls();
  #ifdef NETSERVER_LOOP1
  netserver.loop();
  #endif
  const uint32_t loopElapsedUs = micros() - loopStartedUs;
  if (loopElapsedUs > enduranceMaximumLoopUs)
    enduranceMaximumLoopUs = loopElapsedUs;
  reportEnduranceHealth(millis());
}

#include "core/audiohandlers.h"
