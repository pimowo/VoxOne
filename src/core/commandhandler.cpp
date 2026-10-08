#include <Arduino.h>
#include <cerrno>
#include <cstdlib>
#include "options.h"
#include "commandhandler.h"
#include "player.h"
#include "display.h"
#include "netserver.h"
#include "config.h"
#include "controls.h"
#include "serialcli.h"
#include "volume_map.h"
#include "ui_timeout_config.h"
#include "source_manager.h"
#include "web_transport.h"

#if DSP_MODEL==DSP_DUMMY
#define DUMMYDISPLAY
#endif

CommandHandler cmd;

static bool parseVolumeValue(const char* value, long& parsed) {
  if (!value) return false;
  const char* digits = value;
  if (*digits == '+' || *digits == '-') ++digits;
  if (*digits < '0' || *digits > '9') return false;
  errno = 0;
  char* end = nullptr;
  parsed = strtol(value, &end, 10);
  return errno != ERANGE && *end == '\0';
}

static bool parseTimeInterval(const char* value, long minimum, long maximum, uint16_t& parsed) {
  long candidate;
  if (!parseVolumeValue(value, candidate) || candidate < minimum || candidate > maximum) return false;
  parsed = static_cast<uint16_t>(candidate);
  return true;
}

bool CommandHandler::exec(const char *command, const char *value, uint32_t cid) {
  if (strEquals(command, "webtransport")) {
    bool bluetoothSelected = false;
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
    bluetoothSelected = bluetoothSourceSelected();
#endif
    switch (webTransportAction(bluetoothSelected, value)) {
      case WebTransportAction::RadioPrevious: player.prev(); return true;
      case WebTransportAction::RadioToggle: player.toggle(); return true;
      case WebTransportAction::RadioNext: player.next(); return true;
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
      case WebTransportAction::BluetoothPrevious:
        sourceManagerTransport(BtTransportInput::Previous); return true;
      case WebTransportAction::BluetoothToggle:
        sourceManagerTransport(BtTransportInput::Toggle); return true;
      case WebTransportAction::BluetoothNext:
        sourceManagerTransport(BtTransportInput::Next); return true;
#endif
      default: return false;
    }
  }
  if (strEquals(command, "start"))    { player.sendCommand({PR_PLAY, config.lastStation()}); return true; }
  if (strEquals(command, "stop"))     { player.sendCommand({PR_STOP, 0}); return true; }
  if (strEquals(command, "toggle"))   { player.toggle(); return true; }
  if (strEquals(command, "prev"))     { player.prev(); return true; }
  if (strEquals(command, "next"))     { player.next(); return true; }
  if (strEquals(command, "volm"))     { player.stepVol(false); return true; }
  if (strEquals(command, "volp"))     { player.stepVol(true); return true; }
  if (strEquals(command, "reset") && cid==0)    { config.reset(); return true; }
  if (strEquals(command, "ballance")) { config.setBalance(atoi(value)); return true; }
  if (strEquals(command, "playstation") || strEquals(command, "play")){ 
    int id = atoi(value);
    if (id < 1) id = 1;
    uint16_t cs = config.playlistLength();
    if (id > cs) id = cs;
    player.sendCommand({PR_PLAY, id});
    return true;
  }
  if (strEquals(command, "vol") || strEquals(command, "volume")) {
    long raw;
    if (parseVolumeValue(value, raw)) {
      if (raw < 0) raw = 0;
      if (raw > 254) raw = 254;
      player.setVol(static_cast<uint8_t>(raw));
    }
    return true;
  }
  if (strEquals(command, "vol100")) {
    long user;
    if (parseVolumeValue(value, user) && user >= 0 && user <= 100)
      player.setUserVol(static_cast<uint8_t>(user));
    return true;
  }
  if (strEquals(command, "maximumvolume")) {
    long maximum;
    if (parseVolumeValue(value, maximum) && maximum >= 1 && maximum <= 100)
      player.requestMaximumVolume(static_cast<uint8_t>(maximum));
    return true;
  }
  if (strEquals(command, "startupmode")) {
    long mode;
    if (parseVolumeValue(value, mode) && mode >= STARTUP_LAST && mode <= STARTUP_FIXED) {
      config.setStartupMode(static_cast<uint8_t>(mode));
      netserver.requestOnChange(VOLUME, 0);
    }
    return true;
  }
  if (strEquals(command, "startupfixedvolume")) {
    long user;
    if (parseVolumeValue(value, user) && user >= 0 && user <= 100) {
      config.setStartupFixedVolume(static_cast<uint8_t>(user));
      netserver.requestOnChange(VOLUME, 0);
    }
    return true;
  }
  if (strEquals(command, "dspon"))     { config.setDspOn(atoi(value)!=0); return true; }
  if (strEquals(command, "dim"))       { int d=atoi(value); config.store.brightness = (uint8_t)(d < 0 ? 0 : (d > 100 ? 100 : d)); config.setBrightness(true); return true; }
  if (strEquals(command, "clearspiffs")){ config.spiffsCleanup(); return true; }
  /*********************************************/
  /****************** WEBSOCKET ****************/
  /*********************************************/
  if (strEquals(command, "getindex"))  { netserver.requestOnChange(GETINDEX, cid); return true; }
  if (strEquals(command, "getrssi"))   { netserver.requestOnChange(NRSSI, cid); return true; }
  
  if (strEquals(command, "getsystem"))  { netserver.requestOnChange(GETSYSTEM, cid); return true; }
  if (strEquals(command, "getwebstatus")) { netserver.requestOnChange(WEBSTATUS, cid); return true; }
  if (strEquals(command, "getscreen"))  { netserver.requestOnChange(GETSCREEN, cid); return true; }
  if (strEquals(command, "stationlisttimeout") || strEquals(command, "bttransporttimeout")) {
    uint8_t seconds;
    if (!parseUiTimeout(value, seconds)) return false;
    UiTimeoutSettings settings = uiTimeoutConfig();
    if (strEquals(command, "stationlisttimeout")) settings.stationListSeconds = seconds;
    else settings.btTransportSeconds = seconds;
    if (!uiTimeoutSave(settings)) return false;
    display.putRequest(RESETIDLE);
    netserver.requestOnChange(GETSCREEN, 0);
    return true;
  }
  if (strEquals(command, "numplaylist"))  { config.saveValue(&config.store.numplaylist, static_cast<bool>(atoi(value))); display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER); return true; }
  if (strEquals(command, "flipscreen")) {
    config.saveValue(&config.store.flipscreen, static_cast<bool>(atoi(value)));
    display.flip();
    display.putRequest(NEWMODE, CLEAR);
    display.putRequest(NEWMODE, PLAYER);
    netserver.requestOnChange(GETSCREEN, 0);
    return true;
  }
  if (strEquals(command, "brightness"))   { config.store.brightness = static_cast<uint8_t>(atoi(value)); config.setBrightness(true); return true; }
  if (strEquals(command, "screenon"))     { config.setDspOn(static_cast<bool>(atoi(value))); return true; }
  if (strEquals(command, "contrast"))     { config.saveValue(&config.store.contrast, static_cast<uint8_t>(atoi(value))); display.setContrast(); return true; }
  if (strEquals(command, "screensaverenabled")){ config.enableScreensaver(static_cast<bool>(atoi(value))); return true; }
  if (strEquals(command, "screensavertimeout")){ config.setScreensaverTimeout(static_cast<uint16_t>(atoi(value))); return true; }
  if (strEquals(command, "screensaverblank"))  { config.setScreensaverBlank(static_cast<bool>(atoi(value))); return true; }
  if (strEquals(command, "screensaverplayingenabled")){ config.setScreensaverPlayingEnabled(static_cast<bool>(atoi(value))); return true; }
  if (strEquals(command, "screensaverplayingtimeout")){ config.setScreensaverPlayingTimeout(static_cast<uint16_t>(atoi(value))); return true; }
  if (strEquals(command, "screensaverplayingblank"))  { config.setScreensaverPlayingBlank(static_cast<bool>(atoi(value))); return true; }
  if (strEquals(command, "abuff")){ config.saveValue(&config.store.abuff, static_cast<uint16_t>(atoi(value))); return true; }

  
  if (strEquals(command, "tzh"))        { config.saveValue(&config.store.tzHour, static_cast<int8_t>(atoi(value))); return true; }
  if (strEquals(command, "tzm"))        { config.saveValue(&config.store.tzMin, static_cast<int8_t>(atoi(value))); return true; }
  if (strEquals(command, "sntp2"))      { config.saveValue(config.store.sntp2, value, 35, false); return true; }
  if (strEquals(command, "sntp1"))      { config.setSntpOne(value); return true; }
  if (strEquals(command, "timeint"))    { uint16_t minutes; if (!parseTimeInterval(value, 1, 10080, minutes)) return false; config.saveValue(&config.store.timeSyncInterval, minutes); return true; }
  if (strEquals(command, "timeintrtc")) { uint16_t hours; if (!parseTimeInterval(value, 1, 1000, hours)) return false; config.saveValue(&config.store.timeSyncIntervalRTC, hours); return true; }
  
  if (strEquals(command, "volsteps"))         { config.saveValue(&config.store.volsteps, static_cast<uint8_t>(atoi(value))); return true; }
  if (strEquals(command, "encacc"))  { setEncAcceleration(static_cast<uint16_t>(atoi(value))); return true; }
  if (strEquals(command, "oneclickswitching")){ config.saveValue(&config.store.skipPlaylistUpDown, static_cast<bool>(atoi(value))); return true; }
  
  if (strEquals(command, "balance")) { config.setBalance(static_cast<uint8_t>(atoi(value))); return true; }
  if (strEquals(command, "reboot"))  { requestSystemRestart(); return true; }
  if (strEquals(command, "boot"))    { requestSystemRestart(); return true; }
  if (strEquals(command, "format"))  { SPIFFS.format(); ESP.restart(); return true; }
  if (strEquals(command, "reset"))  { config.resetSystem(value, cid); return true; }

  if (strEquals(command, "smartstart")){ uint8_t ss = atoi(value) == 1 ? 1 : 2; if (!player.isRunning() && ss == 1) ss = 0; config.setSmartStart(ss); return true; }

  if (strEquals(command, "vumeter"))   { config.saveValue(&config.store.vumeter, static_cast<bool>(atoi(value))); display.putRequest(SHOWVUMETER); netserver.requestOnChange(GETSYSTEM, 0); return true; }
  if (strEquals(command, "softap"))    { config.saveValue(&config.store.softapdelay, static_cast<uint8_t>(atoi(value))); return true; }
  if (strEquals(command, "mdnsname"))  { config.saveValue(config.store.mdnsname, value, MDNS_LENGTH); return true; }
  return false;
}

