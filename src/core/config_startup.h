#ifndef VOXONE_CONFIG_STARTUP_H
#define VOXONE_CONFIG_STARTUP_H
#include "config_format.h"
#include "volume_map.h"
#include <cstring>

namespace voxone {
namespace config_format {

struct ConfigStartupResult {
  ConfigRecordStatus status;
  bool btEnabled;
  bool migratingVolume;
  // CONFIG-2B.1 never requests a persistence operation.
  bool needsWrite;
};

// Covers Config::init callbacks (including station recovery), with restoration
// on all returns. It does not change persistence outside startup loading.
class ConfigStartupReadOnlyScope {
 public:
  explicit ConfigStartupReadOnlyScope(bool& flag) : flag_(flag), previous_(flag) { flag_ = true; }
  ~ConfigStartupReadOnlyScope() { flag_ = previous_; }
  ConfigStartupReadOnlyScope(const ConfigStartupReadOnlyScope&) = delete;
  ConfigStartupReadOnlyScope& operator=(const ConfigStartupReadOnlyScope&) = delete;
 private:
  bool& flag_;
  bool previous_;
};

inline void copyStartupString(char* output, const char* input, std::size_t capacity) {
  if (!capacity) return;
  const std::size_t length = std::strlen(input);
  const std::size_t count = length < capacity ? length : capacity - 1;
  std::memcpy(output, input, count);
  output[count] = '\0';
}

// Shared by startup RAM defaults and the existing legacy factory reset.
// The caller supplies profile/device values; this function has no storage API.
template <typename Runtime>
void buildConfigDefaults(Runtime& store, const char* ntp1, const char* ntp2,
                         const char* mdns, uint16_t audioBuffer) {
  store.config_set = kLegacyConfigMagic;
  store.version = kConfigV6;
  store.volume = 12;
  store.balance = 0;
  store.trebble = 0;
  store.middle = 0;
  store.bass = 0;
  store.lastStation = 0;
  store.countStation = 0;
  store.lastSSID = 0;
  store.audioinfo = false;
  store.smartstart = 2;
  store.tzHour = 3;
  store.tzMin = 0;
  store.timezoneOffset = 0;

  store.vumeter=false;
  store.softapdelay=0;
  store.flipscreen=false;
  store.invertdisplay=false;
  store.numplaylist=false;
  store.fliptouch=false;
  store.dbgtouch=false;
  store.dspon=true;
  store.brightness=100;
  store.contrast=55;
  copyStartupString(store.sntp1, ntp1, sizeof(store.sntp1));
  copyStartupString(store.sntp2, ntp2, sizeof(store.sntp2));
  memset(store.reservedWeather, 0, sizeof(store.reservedWeather));
  store._reserved = 0;
  store.lastSdStation = 0;
  store.sdsnuffle = false;
  store.volsteps = 1;
  store.encacc = 200;
  store.play_mode = 0;
  store.irtlp = 35;
  store.btnpullup = true;
  store.btnlongpress = 200;
  store.btnclickticks = 300;
  store.btnpressticks = 500;
  store.encpullup = false;
  store.enchalf = false;
  store.enc2pullup = false;
  store.enc2half = false;
  store.forcemono = false;
  store.i2sinternal = false;
  store.rotate90 = false;
  store.screensaverEnabled = false;
  store.screensaverTimeout = 20;
  store.screensaverBlank = false;
  copyStartupString(store.mdnsname, mdns, sizeof(store.mdnsname));
  store.skipPlaylistUpDown = false;
  store.screensaverPlayingEnabled = false;
  store.screensaverPlayingTimeout = 5;
  store.screensaverPlayingBlank = false;
  store.abuff = audioBuffer;
  store.reservedTelnet = false;
  store.watchdog = true;
  store.timeSyncInterval = 60;    //min
  store.timeSyncIntervalRTC = 24; //hour
  store.reservedWeatherSyncInterval = 0;
  store.maximumVolume = 100;
  store.startupMode = 0;
  store.startupFixedVolume = 20;
  store.lastUserVolume = volumeRawToUser(store.volume);
}

// EEPROM area adapter: format code chooses the exact record length.
// The input is immutable; no writer/commit callback exists in this API.
inline const char* configStartupMessage(ConfigRecordStatus status) {
  switch (status) {
    case ConfigRecordStatus::LOADED_V7: return "loaded v7";
    case ConfigRecordStatus::MIGRATED_V6: return "migrated v6 in RAM";
    case ConfigRecordStatus::MIGRATED_V5: return "migrated v5 in RAM";
    case ConfigRecordStatus::DEFAULTS_REQUIRED: return "defaults: invalid/unsupported legacy";
    case ConfigRecordStatus::UNSUPPORTED_NEWER: return "unsupported newer format; defaults in RAM";
    case ConfigRecordStatus::INVALID_ARGUMENT: return "defaults: read unavailable";
    case ConfigRecordStatus::INVALID_LENGTH: return "defaults: invalid length";
    case ConfigRecordStatus::INVALID_MAGIC: return "defaults: invalid magic";
    case ConfigRecordStatus::INVALID_BOOL: return "defaults: invalid bool";
    case ConfigRecordStatus::INVALID_CRC: return "defaults: invalid CRC";
  }
  return "defaults: unknown status";
}

template <typename Runtime, typename Defaults>
ConfigStartupResult loadStartupConfig(const uint8_t* input, std::size_t size,
                                     bool supportsBt, Runtime& runtime,
                                     Defaults defaults) {
  config_v7_t loaded{};
  const ConfigRecordStatus status = loadConfigArea(input, size, supportsBt, loaded);
  const bool success = status == ConfigRecordStatus::LOADED_V7 ||
      status == ConfigRecordStatus::MIGRATED_V5 ||
      status == ConfigRecordStatus::MIGRATED_V6;
  if (success) {
    runtime.config_set = loaded.fields.config_set;
    runtime.version = loaded.fields.version;
    runtime.volume = loaded.fields.volume;
    runtime.balance = loaded.fields.balance;
    runtime.trebble = loaded.fields.trebble;
    runtime.middle = loaded.fields.middle;
    runtime.bass = loaded.fields.bass;
    runtime.lastStation = loaded.fields.lastStation;
    runtime.countStation = loaded.fields.countStation;
    runtime.lastSSID = loaded.fields.lastSSID;
    runtime.audioinfo = loaded.fields.audioinfo;
    runtime.smartstart = loaded.fields.smartstart;
    runtime.tzHour = loaded.fields.tzHour;
    runtime.tzMin = loaded.fields.tzMin;
    runtime.timezoneOffset = loaded.fields.timezoneOffset;
    runtime.vumeter = loaded.fields.vumeter;
    runtime.softapdelay = loaded.fields.softapdelay;
    runtime.flipscreen = loaded.fields.flipscreen;
    runtime.invertdisplay = loaded.fields.invertdisplay;
    runtime.numplaylist = loaded.fields.numplaylist;
    runtime.fliptouch = loaded.fields.fliptouch;
    runtime.dbgtouch = loaded.fields.dbgtouch;
    runtime.dspon = loaded.fields.dspon;
    runtime.brightness = loaded.fields.brightness;
    runtime.contrast = loaded.fields.contrast;
    std::memcpy(runtime.sntp1, loaded.fields.sntp1, sizeof(runtime.sntp1));
    std::memcpy(runtime.sntp2, loaded.fields.sntp2, sizeof(runtime.sntp2));
    std::memcpy(runtime.reservedWeather, loaded.fields.reservedWeather, sizeof(runtime.reservedWeather));
    runtime._reserved = loaded.fields._reserved;
    runtime.lastSdStation = loaded.fields.lastSdStation;
    runtime.sdsnuffle = loaded.fields.sdsnuffle;
    runtime.volsteps = loaded.fields.volsteps;
    runtime.encacc = loaded.fields.encacc;
    runtime.play_mode = loaded.fields.play_mode;
    runtime.irtlp = loaded.fields.irtlp;
    runtime.btnpullup = loaded.fields.btnpullup;
    runtime.btnlongpress = loaded.fields.btnlongpress;
    runtime.btnclickticks = loaded.fields.btnclickticks;
    runtime.btnpressticks = loaded.fields.btnpressticks;
    runtime.encpullup = loaded.fields.encpullup;
    runtime.enchalf = loaded.fields.enchalf;
    runtime.enc2pullup = loaded.fields.enc2pullup;
    runtime.enc2half = loaded.fields.enc2half;
    runtime.forcemono = loaded.fields.forcemono;
    runtime.i2sinternal = loaded.fields.i2sinternal;
    runtime.rotate90 = loaded.fields.rotate90;
    runtime.screensaverEnabled = loaded.fields.screensaverEnabled;
    runtime.screensaverTimeout = loaded.fields.screensaverTimeout;
    runtime.screensaverBlank = loaded.fields.screensaverBlank;
    runtime.screensaverPlayingEnabled = loaded.fields.screensaverPlayingEnabled;
    runtime.screensaverPlayingTimeout = loaded.fields.screensaverPlayingTimeout;
    runtime.screensaverPlayingBlank = loaded.fields.screensaverPlayingBlank;
    std::memcpy(runtime.mdnsname, loaded.fields.mdnsname, sizeof(runtime.mdnsname));
    runtime.skipPlaylistUpDown = loaded.fields.skipPlaylistUpDown;
    runtime.abuff = loaded.fields.abuff;
    runtime.reservedTelnet = loaded.fields.reservedTelnet;
    runtime.watchdog = loaded.fields.watchdog;
    runtime.timeSyncInterval = loaded.fields.timeSyncInterval;
    runtime.timeSyncIntervalRTC = loaded.fields.timeSyncIntervalRTC;
    runtime.reservedWeatherSyncInterval = loaded.fields.reservedWeatherSyncInterval;
    runtime.maximumVolume = loaded.fields.maximumVolume;
    runtime.startupMode = loaded.fields.startupMode;
    runtime.startupFixedVolume = loaded.fields.startupFixedVolume;
    runtime.lastUserVolume = loaded.fields.lastUserVolume;
    // Runtime remains the legacy model. Never persist v7 headers as raw v6.
    runtime.config_set = kLegacyConfigMagic;
    runtime.version = kConfigV6;
    // Legacy records have no CRC; bound strings before runtime C string calls.
    runtime.sntp1[sizeof(runtime.sntp1) - 1] = '\0';
    runtime.sntp2[sizeof(runtime.sntp2) - 1] = '\0';
    runtime.mdnsname[sizeof(runtime.mdnsname) - 1] = '\0';
  } else {
    defaults(runtime);
  }
  return {status, supportsBt && (!success || loaded.btEnabled != 0),
          status == ConfigRecordStatus::MIGRATED_V5, false};
}

}  // namespace config_format
}  // namespace voxone
#endif
