#ifndef VOXONE_CONFIG_FORMAT_H
#define VOXONE_CONFIG_FORMAT_H

#include <cstddef>
#include <cstdint>

namespace voxone {
namespace config_format {

// Frozen v5 prefix, confirmed at f245821e (v0.2.0) and e96f3d49.
// Historical weather/telnet slots are retained only as opaque legacy data.
struct config_v5_t {
  uint16_t config_set;
  uint16_t version;
  uint8_t volume;
  int8_t balance;
  int8_t trebble;
  int8_t middle;
  int8_t bass;
  uint16_t lastStation;
  uint16_t countStation;
  uint8_t lastSSID;
  bool audioinfo;
  uint8_t smartstart;
  int8_t tzHour;
  int8_t tzMin;
  uint16_t timezoneOffset;
  bool vumeter;
  uint8_t softapdelay;
  bool flipscreen;
  bool invertdisplay;
  bool numplaylist;
  bool reservedInput0;  // Retired local-input byte; serialized offset is frozen.
  bool reservedInput1;
  bool dspon;
  uint8_t brightness;
  uint8_t contrast;
  char sntp1[35];
  char sntp2[35];
  uint8_t reservedWeather[79];
  uint16_t _reserved;
  uint16_t reservedSdStation;
  bool reservedSdFlags;
  uint8_t volsteps;
  uint16_t encacc;
  uint8_t reservedPlayMode;
  uint8_t irtlp;
  bool btnpullup;
  uint16_t btnlongpress;
  uint16_t btnclickticks;
  uint16_t btnpressticks;
  bool encpullup;
  bool enchalf;
  bool enc2pullup;
  bool enc2half;
  bool forcemono;
  bool i2sinternal;
  bool rotate90;
  bool screensaverEnabled;
  uint16_t screensaverTimeout;
  bool screensaverBlank;
  bool screensaverPlayingEnabled;
  uint16_t screensaverPlayingTimeout;
  bool screensaverPlayingBlank;
  char mdnsname[24];
  bool skipPlaylistUpDown;
  uint16_t abuff;
  bool reservedTelnet;
  bool watchdog;
  uint16_t timeSyncInterval;
  uint16_t timeSyncIntervalRTC;
  uint16_t reservedWeatherSyncInterval;
};
static_assert(sizeof(bool) == 1, "legacy EEPROM requires one-byte bool");
static_assert(sizeof(config_v5_t) == 250, "v5 EEPROM size changed");
static_assert(alignof(config_v5_t) == 2, "v5 EEPROM alignment changed");
static_assert(offsetof(config_v5_t, config_set) == 0, "v5 config_set offset changed");
static_assert(offsetof(config_v5_t, version) == 2, "v5 version offset changed");
static_assert(offsetof(config_v5_t, volume) == 4, "v5 volume offset changed");
static_assert(offsetof(config_v5_t, timezoneOffset) == 20, "v5 timezone offset changed");
static_assert(offsetof(config_v5_t, reservedInput0) == 27, "v5 reserved input 0 offset changed");
static_assert(offsetof(config_v5_t, reservedInput1) == 28, "v5 reserved input 1 offset changed");
static_assert(offsetof(config_v5_t, sntp1) == 32, "v5 SNTP1 offset changed");
static_assert(offsetof(config_v5_t, sntp2) == 67, "v5 SNTP2 offset changed");
static_assert(offsetof(config_v5_t, reservedWeather) == 102, "v5 weather offset changed");
static_assert(offsetof(config_v5_t, reservedSdStation) == 184, "v5 reserved SD station offset changed");
static_assert(offsetof(config_v5_t, reservedSdFlags) == 186, "v5 reserved SD flags offset changed");
static_assert(offsetof(config_v5_t, reservedPlayMode) == 190, "v5 reserved play mode offset changed");
static_assert(offsetof(config_v5_t, btnlongpress) == 194, "v5 button offset changed");
static_assert(offsetof(config_v5_t, screensaverTimeout) == 208, "v5 screen offset changed");
static_assert(offsetof(config_v5_t, mdnsname) == 215, "v5 mDNS offset changed");
static_assert(offsetof(config_v5_t, lastStation) == 10, "v5 lastStation offset changed");
static_assert(offsetof(config_v5_t, _reserved) == 182, "v5 _reserved offset changed");
static_assert(offsetof(config_v5_t, encacc) == 188, "v5 encacc offset changed");
static_assert(offsetof(config_v5_t, abuff) == 240, "v5 abuff offset changed");
static_assert(offsetof(config_v5_t, reservedTelnet) == 242, "v5 reservedTelnet offset changed");
static_assert(offsetof(config_v5_t, watchdog) == 243, "v5 watchdog offset changed");
static_assert(offsetof(config_v5_t, timeSyncInterval) == 244, "v5 timeSyncInterval offset changed");
static_assert(offsetof(config_v5_t, timeSyncIntervalRTC) == 246, "v5 timeSyncIntervalRTC offset changed");
static_assert(offsetof(config_v5_t, reservedWeatherSyncInterval) == 248, "v5 reservedWeatherSyncInterval offset changed");

// Frozen EEPROM v6 layout. This type describes old bytes only; runtime Config
// uses config_t while persistence uses the explicit serialized v7 format.
struct config_v6_t {
  uint16_t config_set;
  uint16_t version;
  uint8_t volume;
  int8_t balance;
  int8_t trebble;
  int8_t middle;
  int8_t bass;
  uint16_t lastStation;
  uint16_t countStation;
  uint8_t lastSSID;
  bool audioinfo;
  uint8_t smartstart;
  int8_t tzHour;
  int8_t tzMin;
  uint16_t timezoneOffset;
  bool vumeter;
  uint8_t softapdelay;
  bool flipscreen;
  bool invertdisplay;
  bool numplaylist;
  bool reservedInput0;  // Retired local-input byte; serialized offset is frozen.
  bool reservedInput1;
  bool dspon;
  uint8_t brightness;
  uint8_t contrast;
  char sntp1[35];
  char sntp2[35];
  uint8_t reservedWeather[79];
  uint16_t _reserved;
  uint16_t reservedSdStation;
  bool reservedSdFlags;
  uint8_t volsteps;
  uint16_t encacc;
  uint8_t reservedPlayMode;
  uint8_t irtlp;
  bool btnpullup;
  uint16_t btnlongpress;
  uint16_t btnclickticks;
  uint16_t btnpressticks;
  bool encpullup;
  bool enchalf;
  bool enc2pullup;
  bool enc2half;
  bool forcemono;
  bool i2sinternal;
  bool rotate90;
  bool screensaverEnabled;
  uint16_t screensaverTimeout;
  bool screensaverBlank;
  bool screensaverPlayingEnabled;
  uint16_t screensaverPlayingTimeout;
  bool screensaverPlayingBlank;
  char mdnsname[24];
  bool skipPlaylistUpDown;
  uint16_t abuff;
  bool reservedTelnet;
  bool watchdog;
  uint16_t timeSyncInterval;
  uint16_t timeSyncIntervalRTC;
  uint16_t reservedWeatherSyncInterval;
  uint8_t maximumVolume;
  uint8_t startupMode;
  uint8_t startupFixedVolume;
  uint8_t lastUserVolume;
};

static_assert(sizeof(bool) == 1, "v6 EEPROM requires one-byte bool");
static_assert(sizeof(config_v6_t) == 254, "v6 EEPROM size changed");
static_assert(alignof(config_v6_t) == 2, "v6 EEPROM alignment changed");
static_assert(offsetof(config_v6_t, config_set) == 0, "v6 magic offset changed");
static_assert(offsetof(config_v6_t, version) == 2, "v6 version offset changed");
static_assert(offsetof(config_v6_t, lastStation) == 10, "v6 station offset changed");
static_assert(offsetof(config_v6_t, timezoneOffset) == 20, "v6 timezone offset changed");
static_assert(offsetof(config_v6_t, reservedInput0) == 27, "v6 reserved input 0 offset changed");
static_assert(offsetof(config_v6_t, reservedInput1) == 28, "v6 reserved input 1 offset changed");
static_assert(offsetof(config_v6_t, _reserved) == 182, "v6 marker offset changed");
static_assert(offsetof(config_v6_t, reservedSdStation) == 184, "v6 reserved SD station offset changed");
static_assert(offsetof(config_v6_t, reservedSdFlags) == 186, "v6 reserved SD flags offset changed");
static_assert(offsetof(config_v6_t, reservedPlayMode) == 190, "v6 reserved play mode offset changed");
static_assert(offsetof(config_v6_t, encacc) == 188, "v6 encoder offset changed");
static_assert(offsetof(config_v6_t, btnlongpress) == 194, "v6 button offset changed");
static_assert(offsetof(config_v6_t, screensaverTimeout) == 208, "v6 screen offset changed");
static_assert(offsetof(config_v6_t, abuff) == 240, "v6 audio buffer offset changed");
static_assert(offsetof(config_v6_t, timeSyncInterval) == 244, "v6 time offset changed");
static_assert(offsetof(config_v6_t, lastUserVolume) == 253, "v6 tail offset changed");

// In-memory model only. The v7 EEPROM representation is the explicitly
// serialized 255-byte record below, never a dump of this C++ object.
struct config_v7_t {
  config_v6_t fields;
  uint8_t btEnabled;
  uint32_t crc32;
};

constexpr uint16_t kLegacyConfigMagic = 4262;
constexpr uint16_t kConfigV7Magic = 0xc7a7;
static_assert(kConfigV7Magic != kLegacyConfigMagic, "downgrade must reject v7");
constexpr uint16_t kConfigV5 = 5;
constexpr uint16_t kConfigV6 = 6;
constexpr uint16_t kConfigV7 = 7;
constexpr std::size_t kConfigV7SerializedSize = 255;
constexpr std::size_t kConfigV7ReservedSdStationOffset = 181;
constexpr std::size_t kConfigV7ReservedSdFlagsOffset = 183;
constexpr std::size_t kConfigV7ReservedPlayModeOffset = 187;
constexpr std::size_t kConfigV7BtEnabledOffset = 250;
constexpr std::size_t kConfigV7CrcOffset = 251;
constexpr std::size_t kConfigEepromCapacity = 268;  // Addresses 500..767.
static_assert(kConfigV7SerializedSize <= kConfigEepromCapacity,
              "v7 record exceeds EEPROM config area");
static_assert(kConfigV7ReservedSdStationOffset == 181,
              "v7 reserved SD station offset changed");
static_assert(kConfigV7ReservedSdFlagsOffset == 183,
              "v7 reserved SD flags offset changed");
static_assert(kConfigV7ReservedPlayModeOffset == 187,
              "v7 reserved play mode offset changed");
static_assert(kConfigV7CrcOffset + sizeof(uint32_t) == kConfigV7SerializedSize,
              "v7 CRC must end the record");

// CRC-32/ISO-HDLC: initial/final XOR 0xffffffff, reflected polynomial
// 0xedb88320. The CRC covers every serialized byte before the four-byte CRC.
uint32_t calculateConfigV7Crc(const config_v7_t& value);
bool validateConfigV7(const config_v7_t& value);

// Exact-size, little-endian format. Bool fields, including btEnabled, are
// encoded as one byte (0 or 1). Output is unchanged on failure.
bool serializeConfigV7(const config_v7_t& value, uint8_t* output,
                       std::size_t outputSize);
bool deserializeConfigV7(const uint8_t* input, std::size_t inputSize,
                         config_v7_t& output);

// Pure conversion; no EEPROM access. Output is unchanged for invalid v6.
bool migrateConfigV6ToV7(const config_v6_t& source, bool supportsBt,
                         config_v7_t& output);

bool migrateConfigV5ToV7(const config_v5_t& source, bool supportsBt,
                         config_v7_t& output);

enum class ConfigRecordStatus {
  LOADED_V7, MIGRATED_V5, MIGRATED_V6,
  DEFAULTS_REQUIRED, UNSUPPORTED_NEWER,
  INVALID_ARGUMENT, INVALID_LENGTH, INVALID_MAGIC, INVALID_BOOL, INVALID_CRC
};

// Pure byte parsers. Exact record length is required (not the whole EEPROM
// region). Only success statuses modify output. No defaults or writes occur.
// New magic with any version other than 7 is UNSUPPORTED_NEWER, even if the
// record uses an unknown length. The caller must preserve it without writing.
ConfigRecordStatus parseConfigV7(const uint8_t* input, std::size_t inputSize,
                                 config_v7_t& output);
ConfigRecordStatus loadConfigRecord(const uint8_t* input, std::size_t inputSize,
                                    bool supportsBt, config_v7_t& output);

// A config-area buffer may include unused bytes after a known record.
// Truncated records still fail; detection and parsing stay in this layer.
ConfigRecordStatus loadConfigArea(const uint8_t* input, std::size_t size,
                                  bool supportsBt, config_v7_t& output);

}  // namespace config_format
}  // namespace voxone

#endif
