#include "../src/core/config_startup.h"
#include "../src/core/bt_runtime.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace voxone::config_format;
using S = ConfigRecordStatus;

// Deliberately different field layout verifies named mapping, not blob copying.
struct Runtime {
  uint8_t lastUserVolume;
  uint8_t startupFixedVolume;
  uint8_t startupMode;
  uint8_t maximumVolume;
  uint16_t reservedWeatherSyncInterval;
  uint16_t timeSyncIntervalRTC;
  uint16_t timeSyncInterval;
  bool watchdog;
  bool reservedTelnet;
  uint16_t abuff;
  bool skipPlaylistUpDown;
  char mdnsname[24];
  bool screensaverPlayingBlank;
  uint16_t screensaverPlayingTimeout;
  bool screensaverPlayingEnabled;
  bool screensaverBlank;
  uint16_t screensaverTimeout;
  bool screensaverEnabled;
  bool rotate90;
  bool i2sinternal;
  bool forcemono;
  bool enc2half;
  bool enc2pullup;
  bool enchalf;
  bool encpullup;
  uint16_t btnpressticks;
  uint16_t btnclickticks;
  uint16_t btnlongpress;
  bool btnpullup;
  uint8_t irtlp;
  uint8_t play_mode;
  uint16_t encacc;
  uint8_t volsteps;
  bool sdsnuffle;
  uint16_t lastSdStation;
  uint16_t _reserved;
  uint8_t reservedWeather[79];
  char sntp2[35];
  char sntp1[35];
  uint8_t contrast;
  uint8_t brightness;
  bool dspon;
  bool dbgtouch;
  bool fliptouch;
  bool numplaylist;
  bool invertdisplay;
  bool flipscreen;
  uint8_t softapdelay;
  bool vumeter;
  uint16_t timezoneOffset;
  int8_t tzMin;
  int8_t tzHour;
  uint8_t smartstart;
  bool audioinfo;
  uint8_t lastSSID;
  uint16_t countStation;
  uint16_t lastStation;
  int8_t bass;
  int8_t middle;
  int8_t trebble;
  int8_t balance;
  uint8_t volume;
  uint16_t version;
  uint16_t config_set;
};
template <typename A, typename B>
void equalFields(const A& a, const B& b) {
  assert(a.config_set == b.config_set);
  assert(a.version == b.version);
  assert(a.volume == b.volume);
  assert(a.balance == b.balance);
  assert(a.trebble == b.trebble);
  assert(a.middle == b.middle);
  assert(a.bass == b.bass);
  assert(a.lastStation == b.lastStation);
  assert(a.countStation == b.countStation);
  assert(a.lastSSID == b.lastSSID);
  assert(a.audioinfo == b.audioinfo);
  assert(a.smartstart == b.smartstart);
  assert(a.tzHour == b.tzHour);
  assert(a.tzMin == b.tzMin);
  assert(a.timezoneOffset == b.timezoneOffset);
  assert(a.vumeter == b.vumeter);
  assert(a.softapdelay == b.softapdelay);
  assert(a.flipscreen == b.flipscreen);
  assert(a.invertdisplay == b.invertdisplay);
  assert(a.numplaylist == b.numplaylist);
  assert(a.fliptouch == b.fliptouch);
  assert(a.dbgtouch == b.dbgtouch);
  assert(a.dspon == b.dspon);
  assert(a.brightness == b.brightness);
  assert(a.contrast == b.contrast);
  assert(std::memcmp(a.sntp1, b.sntp1, sizeof(a.sntp1)) == 0);
  assert(std::memcmp(a.sntp2, b.sntp2, sizeof(a.sntp2)) == 0);
  assert(std::memcmp(a.reservedWeather, b.reservedWeather, sizeof(a.reservedWeather)) == 0);
  assert(a._reserved == b._reserved);
  assert(a.lastSdStation == b.lastSdStation);
  assert(a.sdsnuffle == b.sdsnuffle);
  assert(a.volsteps == b.volsteps);
  assert(a.encacc == b.encacc);
  assert(a.play_mode == b.play_mode);
  assert(a.irtlp == b.irtlp);
  assert(a.btnpullup == b.btnpullup);
  assert(a.btnlongpress == b.btnlongpress);
  assert(a.btnclickticks == b.btnclickticks);
  assert(a.btnpressticks == b.btnpressticks);
  assert(a.encpullup == b.encpullup);
  assert(a.enchalf == b.enchalf);
  assert(a.enc2pullup == b.enc2pullup);
  assert(a.enc2half == b.enc2half);
  assert(a.forcemono == b.forcemono);
  assert(a.i2sinternal == b.i2sinternal);
  assert(a.rotate90 == b.rotate90);
  assert(a.screensaverEnabled == b.screensaverEnabled);
  assert(a.screensaverTimeout == b.screensaverTimeout);
  assert(a.screensaverBlank == b.screensaverBlank);
  assert(a.screensaverPlayingEnabled == b.screensaverPlayingEnabled);
  assert(a.screensaverPlayingTimeout == b.screensaverPlayingTimeout);
  assert(a.screensaverPlayingBlank == b.screensaverPlayingBlank);
  assert(std::memcmp(a.mdnsname, b.mdnsname, sizeof(a.mdnsname)) == 0);
  assert(a.skipPlaylistUpDown == b.skipPlaylistUpDown);
  assert(a.abuff == b.abuff);
  assert(a.reservedTelnet == b.reservedTelnet);
  assert(a.watchdog == b.watchdog);
  assert(a.timeSyncInterval == b.timeSyncInterval);
  assert(a.timeSyncIntervalRTC == b.timeSyncIntervalRTC);
  assert(a.reservedWeatherSyncInterval == b.reservedWeatherSyncInterval);
  assert(a.maximumVolume == b.maximumVolume);
  assert(a.startupMode == b.startupMode);
  assert(a.startupFixedVolume == b.startupFixedVolume);
  assert(a.lastUserVolume == b.lastUserVolume);
}
template <typename T>
void defaults(T& value) {
  buildConfigDefaults(value, "0.pl.pool.ntp.org", "1.pl.pool.ntp.org", "VoxOne-123456", 7);
}
void put16(uint8_t* bytes, unsigned offset, uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset+1] = static_cast<uint8_t>(value >> 8);
}
// Raw legacy fixture: explicit historical offsets, opaque nonzero padding.
void legacy(uint8_t* bytes, uint16_t version) {
  std::memset(bytes, 0, 268);
  put16(bytes, 0, 4262); put16(bytes, 2, version);
  bytes[4] = 103; bytes[5] = 0xfd; bytes[8] = 5;
  bytes[9] = bytes[19] = bytes[181] = bytes[193] = 0xee;
  put16(bytes, 10, 23); put16(bytes, 12, 40);
  bytes[15] = 1; bytes[29] = 1; bytes[30] = 76;
  std::memcpy(bytes+32, "custom.ntp.test", 16);
  std::memcpy(bytes+67, "second.ntp.test", 16);
  std::memcpy(bytes+215, "VoxOne-test", 12);
  put16(bytes, 188, 450); put16(bytes, 244, 75);
  bytes[250] = 85; bytes[251] = 1; bytes[252] = 30; bytes[253] = 47;
}
ConfigStartupResult run(const uint8_t* data, std::size_t size, bool supports,
                        Runtime& runtime, S expected, bool useDefaults) {
  uint8_t before[268]{};
  if (data) std::memcpy(before, data, size);
  unsigned defaultsCalls = 0;
  const auto result = loadStartupConfig(data, size, supports, runtime,
    [&](Runtime& value) { ++defaultsCalls; defaults(value); });
  assert(result.status == expected);
  assert(defaultsCalls == (useDefaults ? 1u : 0u));
  if (data) assert(std::memcmp(before, data, size) == 0);
  if (useDefaults) {
    Runtime expectedDefaults{}; defaults(expectedDefaults);
    equalFields(runtime, expectedDefaults);
    assert(result.btEnabled == supports && !result.migratingVolume);
  }
  // Same handoff as Config::init before main's btRuntime.start().
  BtRuntime bt(supports);
  assert(bt.configureBeforeStart(result.btEnabled));
  assert(bt.start() == result.btEnabled);
  assert(bt.status(true, true).btEnabled == result.btEnabled);
  assert(configStartupMessage(result.status)[0] != '\0');
  return result;
}
int main() {
  Runtime runtime{};
  uint8_t area[268]{};
  for (uint16_t version : {5, 6}) {
    legacy(area, version);
    config_v7_t golden{};
    const S status = version == 5 ? S::MIGRATED_V5 : S::MIGRATED_V6;
    assert(loadConfigRecord(area, version == 5 ? 250 : 254, true, golden) == status);
    golden.fields.config_set = kConfigV7Magic; golden.fields.version = 7;
    for (bool supports : {false, true}) {
      auto result = run(area, sizeof(area), supports, runtime, status, false);
      equalFields(runtime, golden.fields);
      assert(result.btEnabled == supports);
      assert(result.migratingVolume == (version == 5));
      if (version == 5) {
        assert(runtime.maximumVolume == 100 && runtime.startupMode == 0);
        assert(runtime.startupFixedVolume == 20 && runtime.lastUserVolume == 50);
      } else {
        assert(runtime.maximumVolume == 85 && runtime.startupMode == 1);
        assert(runtime.startupFixedVolume == 30 && runtime.lastUserVolume == 47);
      }
    }
    for (std::size_t length : {std::size_t(3), std::size_t(249)})
      run(area, length, true, runtime, S::INVALID_LENGTH, true);
    area[15] = 2;
    run(area, sizeof(area), true, runtime, S::INVALID_BOOL, true);
  }
  config_v6_t v6{}; defaults(v6);
  v6.lastStation = 123; v6.volume = 111; v6.lastUserVolume = 53;
  v6.config_set = kLegacyConfigMagic; v6.version = 6;
  config_v7_t v7{};
  assert(migrateConfigV6ToV7(v6, true, v7));
  v6.config_set = kConfigV7Magic; v6.version = 7;
  for (uint8_t enabled : {0, 1}) {
    v7.btEnabled = enabled; v7.crc32 = calculateConfigV7Crc(v7);
    assert(serializeConfigV7(v7, area, 255));
    std::memset(area+255, 0xa5, 13);  // Area slack is not part of the CRC.
    for (bool supports : {false, true}) {
      const auto result = run(area, sizeof(area), supports, runtime, S::LOADED_V7, false);
      equalFields(runtime, v6);
      assert(result.btEnabled == (supports && enabled));
      assert(result.storedBtEnabled == (enabled != 0));
      assert(!result.migratingVolume);
    }
  }
  area[4] ^= 1;
  run(area, sizeof(area), true, runtime, S::INVALID_CRC, true);
  assert(serializeConfigV7(v7, area, 255));
  area[250] = 2;
  run(area, sizeof(area), true, runtime, S::INVALID_BOOL, true);
  assert(serializeConfigV7(v7, area, 255));
  area[14] = 2;
  run(area, sizeof(area), false, runtime, S::INVALID_BOOL, true);
  assert(serializeConfigV7(v7, area, 255));
  for (std::size_t length : {std::size_t(0), std::size_t(3), std::size_t(254)})
    run(area, length, true, runtime, S::INVALID_LENGTH, true);
  for (uint16_t version : {0, 6, 8, 65535}) {
    put16(area, 2, version);
    run(area, sizeof(area), true, runtime, S::UNSUPPORTED_NEWER, true);
  }
  for (uint16_t version : {0, 1, 2, 3, 4, 7, 65535}) {
    legacy(area, version);
    run(area, sizeof(area), true, runtime, S::DEFAULTS_REQUIRED, true);
  }
  put16(area, 0, 0xffff);
  run(area, sizeof(area), false, runtime, S::DEFAULTS_REQUIRED, true);
  run(nullptr, 268, true, runtime, S::INVALID_ARGUMENT, true);
  bool readOnly = false;
  {
    ConfigStartupReadOnlyScope guard(readOnly);
    assert(readOnly);
    { ConfigStartupReadOnlyScope nested(readOnly); assert(readOnly); }
    assert(readOnly);
  }
  assert(!readOnly);
  std::puts("PASS config_startup_native");
}
