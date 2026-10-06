#include "Arduino.h"
#include "core/options.h"
#include "core/config.h"
#include "pluginsManager/pluginsManager.h"
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
#ifdef USE_NEXTION
#include "displays/nextion.h"
#endif

#if USE_OTA
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#include <NetworkUdp.h>
#else
#include <WiFiUdp.h>
#endif
#include <ArduinoOTA.h>
#endif

#if DSP_HSPI || TS_HSPI || VS_HSPI
SPIClass  SPI2(HSPI);
#endif

extern __attribute__((weak)) void yoradio_on_setup();

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
      display.putRequest(NEWMODE, UPDATING);
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
#if defined(VOXONE_PROFILE_SALON)
  dacMute.begin();
#endif
#if defined(VOXONE_PROFILE_SALON) && defined(RGB_BUILTIN) && \
    defined(PIN_NEOPIXEL) && PIN_NEOPIXEL == 48
  neopixelWrite(RGB_BUILTIN, 0, 0, 0);
#endif
  if(REAL_LEDBUILTIN!=255) pinMode(REAL_LEDBUILTIN, OUTPUT);
  if (yoradio_on_setup) yoradio_on_setup();
  pm.on_setup();
  config.init();
  btRuntime.start();
#if defined(VOXONE_PROFILE_SALON)
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
  if (network.status != CONNECTED && network.status!=SDREADY) {
    netserver.begin();
    initControls();
    display.putRequest(DSP_START);
    while(!display.ready()) delay(10);
    return;
  }
  if(SDC_CS!=255) {
    display.putRequest(WAITFORSD, 0);
    Serial.print("##[BOOT]#\tSD search\t");
  }
  config.initPlaylistMode();
  netserver.begin();
  initControls();
  display.putRequest(DSP_START);
  while(!display.ready()) delay(10);
  #if USE_OTA
    setupOTA();
  #endif
  if (config.getMode()==PM_SDCARD) player.initHeaders(config.station.url);
  player.lockOutput=false;
  // Preserve the selected station, but always enter the base source in STOP.
  pm.on_end_setup();
}

void loop() {
  timekeeper.loop1();
  serialCli.loop();
  #if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
    if (btRuntime.physicalStarted()) {
      btLink.loop();
      // STATUS_BEGIN clears session fields before STATUS_END completes them.
      // Do not let consumers treat that partial snapshot as a disconnect.
      if (!btLink.hasIncompleteOnlineSnapshot()) sourceManagerLoop();
    }
  #endif
  #if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE && VOXONE_BT_I2S_RX_ENABLED
    if (btRuntime.physicalStarted() &&
        !btLink.hasIncompleteOnlineSnapshot())
      btAudioInput.loop(btLink.state(), bluetoothSourceSelected(),
                        player.getSampleRate(), millis());
  #endif
  if (network.status == CONNECTED || network.status==SDREADY) {
    player.loop();
#if USE_OTA
    ArduinoOTA.handle();
#endif
  }
#if defined(VOXONE_PROFILE_SALON)
  const BtLinkState& bt = btLink.state();
  const BtRuntimeStatus btStatus = btRuntime.status(bt.runtimeAvailable,
                                                    bt.connected);
  const DacPlaybackState dacPlayback = dacPlaybackForSource(
      bluetoothSourceSelected(), player.isRunning(),
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
}

#include "core/audiohandlers.h"
