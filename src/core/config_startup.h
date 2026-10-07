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
  bool storedBtEnabled;
};

// Named field mapping works for the runtime model and the frozen wire model.
template <typename Source, typename Target>
void copyConfigFields(const Source& source, Target& target) {
  target.config_set = source.config_set;
  target.version = source.version;
  target.volume = source.volume;
  target.balance = source.balance;
  target.trebble = source.trebble;
  target.middle = source.middle;
  target.bass = source.bass;
  target.lastStation = source.lastStation;
  target.countStation = source.countStation;
  target.lastSSID = source.lastSSID;
  target.audioinfo = source.audioinfo;
  target.smartstart = source.smartstart;
  target.tzHour = source.tzHour;
  target.tzMin = source.tzMin;
  target.timezoneOffset = source.timezoneOffset;
  target.vumeter = source.vumeter;
  target.softapdelay = source.softapdelay;
  target.flipscreen = source.flipscreen;
  target.invertdisplay = source.invertdisplay;
  target.numplaylist = source.numplaylist;
  target.reservedInput0 = source.reservedInput0;
  target.reservedInput1 = source.reservedInput1;
  target.dspon = source.dspon;
  target.brightness = source.brightness;
  target.contrast = source.contrast;
  std::memcpy(target.sntp1, source.sntp1, sizeof(target.sntp1));
  std::memcpy(target.sntp2, source.sntp2, sizeof(target.sntp2));
  std::memcpy(target.reservedWeather, source.reservedWeather, sizeof(target.reservedWeather));
  target._reserved = source._reserved;
  target.reservedSdStation = source.reservedSdStation;
  target.reservedSdFlags = source.reservedSdFlags;
  target.volsteps = source.volsteps;
  target.encacc = source.encacc;
  target.reservedPlayMode = source.reservedPlayMode;
  target.irtlp = source.irtlp;
  target.btnpullup = source.btnpullup;
  target.btnlongpress = source.btnlongpress;
  target.btnclickticks = source.btnclickticks;
  target.btnpressticks = source.btnpressticks;
  target.encpullup = source.encpullup;
  target.enchalf = source.enchalf;
  target.enc2pullup = source.enc2pullup;
  target.enc2half = source.enc2half;
  target.forcemono = source.forcemono;
  target.i2sinternal = source.i2sinternal;
  target.rotate90 = source.rotate90;
  target.screensaverEnabled = source.screensaverEnabled;
  target.screensaverTimeout = source.screensaverTimeout;
  target.screensaverBlank = source.screensaverBlank;
  target.screensaverPlayingEnabled = source.screensaverPlayingEnabled;
  target.screensaverPlayingTimeout = source.screensaverPlayingTimeout;
  target.screensaverPlayingBlank = source.screensaverPlayingBlank;
  std::memcpy(target.mdnsname, source.mdnsname, sizeof(target.mdnsname));
  target.skipPlaylistUpDown = source.skipPlaylistUpDown;
  target.abuff = source.abuff;
  target.reservedTelnet = source.reservedTelnet;
  target.watchdog = source.watchdog;
  target.timeSyncInterval = source.timeSyncInterval;
  target.timeSyncIntervalRTC = source.timeSyncIntervalRTC;
  target.reservedWeatherSyncInterval = source.reservedWeatherSyncInterval;
  target.maximumVolume = source.maximumVolume;
  target.startupMode = source.startupMode;
  target.startupFixedVolume = source.startupFixedVolume;
  target.lastUserVolume = source.lastUserVolume;
}

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

// Shared by startup RAM defaults and the v7 factory reset.
// The caller supplies profile/device values; this function has no storage API.
template <typename Runtime>
void buildConfigDefaults(Runtime& store, const char* ntp1, const char* ntp2,
                         const char* mdns, uint16_t audioBuffer) {
  store.config_set = kConfigV7Magic;
  store.version = kConfigV7;
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
  store.reservedInput0=false;
  store.reservedInput1=false;
  store.dspon=true;
  store.brightness=100;
  store.contrast=55;
  copyStartupString(store.sntp1, ntp1, sizeof(store.sntp1));
  copyStartupString(store.sntp2, ntp2, sizeof(store.sntp2));
  memset(store.reservedWeather, 0, sizeof(store.reservedWeather));
  store._reserved = 0;
  store.reservedSdStation = 0;
  store.reservedSdFlags = false;
  store.volsteps = 1;
  store.encacc = 200;
  store.reservedPlayMode = 0;
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
    copyConfigFields(loaded.fields, runtime);
    runtime.config_set = kConfigV7Magic;
    runtime.version = kConfigV7;
    // Legacy records have no CRC; bound strings before runtime C string calls.
    runtime.sntp1[sizeof(runtime.sntp1) - 1] = '\0';
    runtime.sntp2[sizeof(runtime.sntp2) - 1] = '\0';
    runtime.mdnsname[sizeof(runtime.mdnsname) - 1] = '\0';
  } else {
    defaults(runtime);
  }
  return {status, supportsBt && (!success || loaded.btEnabled != 0),
          status == ConfigRecordStatus::MIGRATED_V5,
          success ? loaded.btEnabled != 0 : supportsBt};
}

}  // namespace config_format
}  // namespace voxone
#endif
