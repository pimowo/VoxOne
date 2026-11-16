#include <Arduino.h>
#include <cerrno>
#include <cstdlib>
#include "options.h"
#include "commandhandler.h"
#include "player.h"
#include "display.h"
#include "netserver.h"
#include "config.h"
#include "serialcli.h"
#include "ui_timeout_config.h"
#include "source_manager.h"
#include "web_transport.h"
#include "update_progress.h"
#include "ui_state.h"
#include "audio_balance.h"

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

bool CommandHandler::exec(const char *command, const char *value, uint32_t cid,
                          bool durableConfig) {
  if (updateLockActive() &&
      !strEquals(command, "getsystem") &&
      !strEquals(command, "getwebstatus") &&
      !strEquals(command, "getrssi") &&
      !strEquals(command, "getscreen")) return false;
  if (cid != 0 && strEquals(command, "source")) {
    SourceId target;
    if (!parseSourceId(value, target) ||
        !sourceManagerSourceAvailable(target)) return false;
    return sourceManagerRequestSource(target);
  }
  if (strEquals(command, "webtransport")) {
    bool bluetoothSelected = false;
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
    bluetoothSelected = bluetoothSourceSelected();
#endif
    switch (webTransportAction(bluetoothSelected, value)) {
      case WebTransportAction::RadioPrevious: return player.prev();
      case WebTransportAction::RadioToggle: return player.toggle();
      case WebTransportAction::RadioNext: return player.next();
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
  if (strEquals(command, "start"))    { return player.sendCommand({PR_PLAY, config.lastStation()}); }
  if (strEquals(command, "stop"))     { return player.sendCommand({PR_STOP, 0}); }
  if (strEquals(command, "playurl") || strEquals(command, "ha_play")) {
    const size_t length = value ? strlen(value) : 0;
    voxone::source_media::RequestStorage storage{};
    if (!voxone::source_media::parseRequest(value, length, storage))
      return false;
    const auto request = storage.view();
    if (!sourceManagerSourceAvailable(SourceId::HomeAssistant)) return false;
    return player.requestSourceMedia(request);
  }
  if (strEquals(command, "toggle"))   { return player.toggle(); }
  if (strEquals(command, "prev"))     { return player.prev(); }
  if (strEquals(command, "next"))     { return player.next(); }
  if (voxone::audio_balance::isBalanceCommand(command)) {
    int8_t balance;
    if (!voxone::audio_balance::parseValue(value, balance)) return false;
    return config.setBalance(balance);
  }
  if (strEquals(command, "playstation")) {
    int id = atoi(value);
    if (id < 1) id = 1;
    return player.sendCommand({PR_PLAY, id});
  }
  if (strEquals(command, "vol100")) {
    long user;
    if (parseVolumeValue(value, user) && user >= 0 && user <= 100) {
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
      if (bluetoothSourceSelected() && bluetoothTransportAvailable())
        sourceManagerSetBluetoothVolume(static_cast<uint8_t>(user));
      else
#endif
        player.setUserVol(static_cast<uint8_t>(user));
    }
    return true;
  }
  if (strEquals(command, "lineoutenabled")) {
    if (!value || (strcmp(value, "0") != 0 && strcmp(value, "1") != 0))
      return false;
    return config.setLineOutEnabled(value[0] == '1', durableConfig);
  }
  if (strEquals(command, "lineoutvolume")) {
    long lineOutVolume;
    if (!parseVolumeValue(value, lineOutVolume) || lineOutVolume < 0 ||
        lineOutVolume > 100)
      return false;
    return config.setLineOutVolume(static_cast<uint8_t>(lineOutVolume), durableConfig);
  }
  if (strEquals(command, "mute")) {
    if (value && (strcmp(value, "0") == 0 || strcmp(value, "1") == 0))
      player.setMuted(value[0] == '1');
    return true;
  }
  if (strEquals(command, "maximumvolume")) {
    long maximum;
    if (parseVolumeValue(value, maximum) && maximum >= 1 && maximum <= 100) {
      if (durableConfig)
        return config.setMaximumVolume(static_cast<uint8_t>(maximum));
      player.requestMaximumVolume(static_cast<uint8_t>(maximum));
    }
    return true;
  }
  if (strEquals(command, "startupmode")) {
    long mode;
    if (parseVolumeValue(value, mode) && mode >= STARTUP_LAST && mode <= STARTUP_FIXED) {
      if (!config.setStartupMode(static_cast<uint8_t>(mode))) return false;
      netserver.requestOnChange(VOLUME, 0);
    }
    return true;
  }
  if (strEquals(command, "startupfixedvolume")) {
    long user;
    if (parseVolumeValue(value, user) && user >= 0 && user <= 100) {
      if (!config.setStartupFixedVolume(static_cast<uint8_t>(user))) return false;
      netserver.requestOnChange(VOLUME, 0);
    }
    return true;
  }
  if (strEquals(command, "dim"))       { int d=atoi(value); config.setRuntimeBrightness((uint8_t)(d < 0 ? 0 : (d > 100 ? 100 : d))); return config.setBrightness(true); }
  if (strEquals(command, "getindex"))  { netserver.requestOnChange(GETINDEX, cid); return true; }
  if (strEquals(command, "getrssi"))   { netserver.requestOnChange(NRSSI, cid); return true; }
  
  if (strEquals(command, "getsystem"))  { netserver.requestOnChange(GETSYSTEM, cid); return true; }
  if (strEquals(command, "getwebstatus")) { netserver.requestOnChange(WEBSTATUS, cid); return true; }
  if (strEquals(command, "getscreen"))  { netserver.requestOnChange(GETSCREEN, cid); return true; }
  if (strEquals(command, "encoderuireversed")) {
    if (!value || (strcmp(value, "0") != 0 && strcmp(value, "1") != 0))
      return false;
    return config.setEncoderUiReversed(value[0] == '1');
  }
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
  if (strEquals(command, "flipscreen")) {
    if (!config.saveValue(&config.store.flipscreen, static_cast<bool>(atoi(value)))) return false;
    display.flip();
    transitionUiMode(CLEAR);
    transitionUiMode(PLAYER);
    netserver.requestOnChange(GETSCREEN, 0);
    return true;
  }
  if (strEquals(command, "timeint"))    { uint16_t minutes; if (!parseTimeInterval(value, 1, 10080, minutes)) return false; return config.saveValue(&config.store.timeSyncInterval, minutes); }
  if (strEquals(command, "timeintrtc")) { uint16_t hours; if (!parseTimeInterval(value, 1, 1000, hours)) return false; return config.saveValue(&config.store.timeSyncIntervalRTC, hours); }

  if (strEquals(command, "reboot"))  { requestSystemRestart(); return true; }

  if (strEquals(command, "vumeter")) {
    if (!value || (strcmp(value, "0") != 0 && strcmp(value, "1") != 0) ||
        !config.setVuMeter(value[0] == '1', durableConfig))
      return false;
    display.putRequest(SHOWVUMETER);
    netserver.requestOnChange(GETSYSTEM, 0);
    return true;
  }
  if (strEquals(command, "hadiscovery")) {
    if (!config.saveValue(&config.store.haDiscoveryEnabled, static_cast<bool>(atoi(value)))) return false;
    netserver.requestOnChange(GETSYSTEM, 0);
    return true;
  }
  return false;
}
