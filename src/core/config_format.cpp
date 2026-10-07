#include "config_format.h"
#include "volume_map.h"

#include <cstring>

namespace voxone {
namespace config_format {
namespace {

struct Writer {
  Writer(uint8_t* data, std::size_t size, std::size_t start = 0)
      : bytes(data), capacity(size), position(start), valid(start <= size) {}
  uint8_t* bytes;
  std::size_t capacity;
  std::size_t position;
  bool valid;

  void u8(uint8_t value) {
    if (position >= capacity) { valid = false; return; }
    bytes[position++] = value;
  }
  void i8(int8_t value) { u8(static_cast<uint8_t>(value)); }
  void boolean(bool value) { u8(value ? 1 : 0); }
  void u16(uint16_t value) {
    u8(static_cast<uint8_t>(value));
    u8(static_cast<uint8_t>(value >> 8));
  }
  void u32(uint32_t value) {
    u16(static_cast<uint16_t>(value));
    u16(static_cast<uint16_t>(value >> 16));
  }
  void raw(const uint8_t* data, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) u8(data[i]);
  }
  void chars(const char* data, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
      uint8_t byte;
      std::memcpy(&byte, data + i, 1);
      u8(byte);
    }
  }
};

struct Reader {
  Reader(const uint8_t* data, std::size_t size, bool legacy = false)
      : bytes(data), capacity(size), position(0), valid(true), aligned(legacy) {}
  const uint8_t* bytes;
  std::size_t capacity;
  std::size_t position;
  bool valid;
  bool aligned;

  uint8_t u8() {
    if (position >= capacity) { valid = false; return 0; }
    return bytes[position++];
  }
  int8_t i8() {
    const int value = u8();
    return static_cast<int8_t>(value < 128 ? value : value - 256);
  }
  bool boolean() {
    const uint8_t value = u8();
    if (value > 1) valid = false;
    return value == 1;
  }
  uint16_t u16() {
    // v5/v6 contain four ABI padding bytes; never interpret them as fields.
    if (aligned && (position & 1u)) u8();
    const uint16_t low = u8();
    return static_cast<uint16_t>(low | (static_cast<uint16_t>(u8()) << 8));
  }
  uint32_t u32() {
    const uint32_t low = u16();
    return low | (static_cast<uint32_t>(u16()) << 16);
  }
  void raw(uint8_t* data, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) data[i] = u8();
  }
  void chars(char* data, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
      const uint8_t byte = u8();
      std::memcpy(data + i, &byte, 1);
    }
  }
};

// Both functions list every v6 field in its historical order. v7 omits the
// four ABI padding bytes, and persists every scalar in an explicit width.
void writeFields(Writer& w, const config_v6_t& f) {
#define U8(name) w.u8(f.name)
#define I8(name) w.i8(f.name)
#define B(name) w.boolean(f.name)
#define U16(name) w.u16(f.name)
  U16(config_set); U16(version);
  U8(volume); I8(balance); I8(trebble); I8(middle); I8(bass);
  U16(lastStation); U16(countStation); U8(lastSSID); B(audioinfo);
  U8(smartstart); I8(tzHour); I8(tzMin); U16(timezoneOffset);
  B(vumeter); U8(softapdelay); B(flipscreen); B(invertdisplay);
  B(numplaylist); B(reservedInput0); B(reservedInput1); B(dspon);
  U8(brightness); U8(contrast);
  w.chars(f.sntp1, sizeof(f.sntp1));
  w.chars(f.sntp2, sizeof(f.sntp2));
  w.raw(f.reservedWeather, sizeof(f.reservedWeather));
  U16(_reserved); U16(reservedSdStation); B(reservedSdFlags);
  U8(volsteps); U16(encacc); U8(reservedPlayMode); U8(irtlp);
  B(btnpullup); U16(btnlongpress); U16(btnclickticks);
  U16(btnpressticks); B(encpullup); B(enchalf);
  B(enc2pullup); B(enc2half); B(forcemono); B(i2sinternal);
  B(rotate90); B(screensaverEnabled); U16(screensaverTimeout);
  B(screensaverBlank); B(screensaverPlayingEnabled);
  U16(screensaverPlayingTimeout); B(screensaverPlayingBlank);
  w.chars(f.mdnsname, sizeof(f.mdnsname));
  B(skipPlaylistUpDown); U16(abuff); B(reservedTelnet); B(watchdog);
  U16(timeSyncInterval); U16(timeSyncIntervalRTC);
  U16(reservedWeatherSyncInterval); U8(maximumVolume);
  U8(startupMode); U8(startupFixedVolume); U8(lastUserVolume);
#undef U8
#undef I8
#undef B
#undef U16
}

template <typename Legacy>
void readPrefix(Reader& r, Legacy& f) {
#define U8(name) f.name = r.u8()
#define I8(name) f.name = r.i8()
#define B(name) f.name = r.boolean()
#define U16(name) f.name = r.u16()
  U16(config_set); U16(version);
  U8(volume); I8(balance); I8(trebble); I8(middle); I8(bass);
  U16(lastStation); U16(countStation); U8(lastSSID); B(audioinfo);
  U8(smartstart); I8(tzHour); I8(tzMin); U16(timezoneOffset);
  B(vumeter); U8(softapdelay); B(flipscreen); B(invertdisplay);
  B(numplaylist); B(reservedInput0); B(reservedInput1); B(dspon);
  U8(brightness); U8(contrast);
  r.chars(f.sntp1, sizeof(f.sntp1));
  r.chars(f.sntp2, sizeof(f.sntp2));
  r.raw(f.reservedWeather, sizeof(f.reservedWeather));
  U16(_reserved); U16(reservedSdStation); B(reservedSdFlags);
  U8(volsteps); U16(encacc); U8(reservedPlayMode); U8(irtlp);
  B(btnpullup); U16(btnlongpress); U16(btnclickticks);
  U16(btnpressticks); B(encpullup); B(enchalf);
  B(enc2pullup); B(enc2half); B(forcemono); B(i2sinternal);
  B(rotate90); B(screensaverEnabled); U16(screensaverTimeout);
  B(screensaverBlank); B(screensaverPlayingEnabled);
  U16(screensaverPlayingTimeout); B(screensaverPlayingBlank);
  r.chars(f.mdnsname, sizeof(f.mdnsname));
  B(skipPlaylistUpDown); U16(abuff); B(reservedTelnet); B(watchdog);
  U16(timeSyncInterval); U16(timeSyncIntervalRTC);
  U16(reservedWeatherSyncInterval);
#undef U8
#undef I8
#undef B
#undef U16
}

void readFields(Reader& r, config_v6_t& f) {
  readPrefix(r, f);
  f.maximumVolume = r.u8();
  f.startupMode = r.u8();
  f.startupFixedVolume = r.u8();
  f.lastUserVolume = r.u8();
}

uint32_t crc32(const uint8_t* data, std::size_t length) {
  uint32_t crc = 0xffffffffu;
  for (std::size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return crc ^ 0xffffffffu;
}

bool writePayload(const config_v7_t& value, uint8_t* bytes) {
  Writer writer{bytes, kConfigV7CrcOffset};
  writeFields(writer, value.fields);
  if (!writer.valid || writer.position != kConfigV7BtEnabledOffset)
    return false;
  writer.u8(value.btEnabled);
  return writer.valid && writer.position == kConfigV7CrcOffset;
}

bool computeCrc(const config_v7_t& value, uint32_t& result) {
  uint8_t bytes[kConfigV7CrcOffset]{};
  if (!writePayload(value, bytes)) return false;
  result = crc32(bytes, sizeof(bytes));
  return true;
}

}  // namespace

uint32_t calculateConfigV7Crc(const config_v7_t& value) {
  uint32_t result = 0;
  computeCrc(value, result);
  return result;
}

bool validateConfigV7(const config_v7_t& value) {
  uint32_t expected = 0;
  return value.fields.config_set == kConfigV7Magic &&
         value.fields.version == kConfigV7 && value.btEnabled <= 1 &&
         computeCrc(value, expected) && value.crc32 == expected;
}

bool serializeConfigV7(const config_v7_t& value, uint8_t* output,
                       std::size_t outputSize) {
  if (!output || outputSize != kConfigV7SerializedSize ||
      !validateConfigV7(value)) return false;
  uint8_t bytes[kConfigV7SerializedSize]{};
  if (!writePayload(value, bytes)) return false;
  Writer writer{bytes, sizeof(bytes), kConfigV7CrcOffset};
  writer.u32(value.crc32);
  if (!writer.valid || writer.position != sizeof(bytes)) return false;
  std::memcpy(output, bytes, sizeof(bytes));
  return true;
}

bool deserializeConfigV7(const uint8_t* input, std::size_t inputSize,
                         config_v7_t& output) {
  return parseConfigV7(input, inputSize, output) == ConfigRecordStatus::LOADED_V7;
}

ConfigRecordStatus parseConfigV7(const uint8_t* input, std::size_t inputSize,
                                 config_v7_t& output) {
  if (!input) return ConfigRecordStatus::INVALID_ARGUMENT;
  if (inputSize < 4) return ConfigRecordStatus::INVALID_LENGTH;
  Reader header{input, inputSize};
  if (header.u16() != kConfigV7Magic) return ConfigRecordStatus::INVALID_MAGIC;
  if (header.u16() != kConfigV7) return ConfigRecordStatus::UNSUPPORTED_NEWER;
  if (inputSize != kConfigV7SerializedSize) return ConfigRecordStatus::INVALID_LENGTH;
  config_v7_t decoded{};
  Reader reader{input, inputSize};
  readFields(reader, decoded.fields);
  decoded.btEnabled = reader.u8();
  decoded.crc32 = reader.u32();
  if (!reader.valid || decoded.btEnabled > 1) return ConfigRecordStatus::INVALID_BOOL;
  // Check the original bytes, not a normalized reserialization.
  if (decoded.crc32 != crc32(input, kConfigV7CrcOffset))
    return ConfigRecordStatus::INVALID_CRC;
  output = decoded;
  return ConfigRecordStatus::LOADED_V7;
}

bool migrateConfigV6ToV7(const config_v6_t& source, bool supportsBt,
                         config_v7_t& output) {
  if (source.config_set != kLegacyConfigMagic || source.version != kConfigV6)
    return false;
  config_v7_t migrated{};
  migrated.fields = source;
  migrated.fields.config_set = kConfigV7Magic;
  migrated.fields.version = kConfigV7;
  migrated.btEnabled = supportsBt ? 1 : 0;
  if (!computeCrc(migrated, migrated.crc32)) return false;
  output = migrated;
  return true;
}

bool migrateConfigV5ToV7(const config_v5_t& source, bool supportsBt,
                         config_v7_t& output) {
  if (source.config_set != kLegacyConfigMagic || source.version != kConfigV5)
    return false;
  config_v7_t migrated{};
  migrated.fields.config_set = source.config_set;
  migrated.fields.version = source.version;
  migrated.fields.volume = source.volume;
  migrated.fields.balance = source.balance;
  migrated.fields.trebble = source.trebble;
  migrated.fields.middle = source.middle;
  migrated.fields.bass = source.bass;
  migrated.fields.lastStation = source.lastStation;
  migrated.fields.countStation = source.countStation;
  migrated.fields.lastSSID = source.lastSSID;
  migrated.fields.audioinfo = source.audioinfo;
  migrated.fields.smartstart = source.smartstart;
  migrated.fields.tzHour = source.tzHour;
  migrated.fields.tzMin = source.tzMin;
  migrated.fields.timezoneOffset = source.timezoneOffset;
  migrated.fields.vumeter = source.vumeter;
  migrated.fields.softapdelay = source.softapdelay;
  migrated.fields.flipscreen = source.flipscreen;
  migrated.fields.invertdisplay = source.invertdisplay;
  migrated.fields.numplaylist = source.numplaylist;
  migrated.fields.reservedInput0 = source.reservedInput0;
  migrated.fields.reservedInput1 = source.reservedInput1;
  migrated.fields.dspon = source.dspon;
  migrated.fields.brightness = source.brightness;
  migrated.fields.contrast = source.contrast;
  std::memcpy(migrated.fields.sntp1, source.sntp1, sizeof(source.sntp1));
  std::memcpy(migrated.fields.sntp2, source.sntp2, sizeof(source.sntp2));
  std::memcpy(migrated.fields.reservedWeather, source.reservedWeather, sizeof(source.reservedWeather));
  migrated.fields._reserved = source._reserved;
  migrated.fields.reservedSdStation = source.reservedSdStation;
  migrated.fields.reservedSdFlags = source.reservedSdFlags;
  migrated.fields.volsteps = source.volsteps;
  migrated.fields.encacc = source.encacc;
  migrated.fields.reservedPlayMode = source.reservedPlayMode;
  migrated.fields.irtlp = source.irtlp;
  migrated.fields.btnpullup = source.btnpullup;
  migrated.fields.btnlongpress = source.btnlongpress;
  migrated.fields.btnclickticks = source.btnclickticks;
  migrated.fields.btnpressticks = source.btnpressticks;
  migrated.fields.encpullup = source.encpullup;
  migrated.fields.enchalf = source.enchalf;
  migrated.fields.enc2pullup = source.enc2pullup;
  migrated.fields.enc2half = source.enc2half;
  migrated.fields.forcemono = source.forcemono;
  migrated.fields.i2sinternal = source.i2sinternal;
  migrated.fields.rotate90 = source.rotate90;
  migrated.fields.screensaverEnabled = source.screensaverEnabled;
  migrated.fields.screensaverTimeout = source.screensaverTimeout;
  migrated.fields.screensaverBlank = source.screensaverBlank;
  migrated.fields.screensaverPlayingEnabled = source.screensaverPlayingEnabled;
  migrated.fields.screensaverPlayingTimeout = source.screensaverPlayingTimeout;
  migrated.fields.screensaverPlayingBlank = source.screensaverPlayingBlank;
  std::memcpy(migrated.fields.mdnsname, source.mdnsname, sizeof(source.mdnsname));
  migrated.fields.skipPlaylistUpDown = source.skipPlaylistUpDown;
  migrated.fields.abuff = source.abuff;
  migrated.fields.reservedTelnet = source.reservedTelnet;
  migrated.fields.watchdog = source.watchdog;
  migrated.fields.timeSyncInterval = source.timeSyncInterval;
  migrated.fields.timeSyncIntervalRTC = source.timeSyncIntervalRTC;
  migrated.fields.reservedWeatherSyncInterval = source.reservedWeatherSyncInterval;
  migrated.fields.config_set = kConfigV7Magic;
  migrated.fields.version = kConfigV7;
  // Defaults added for the historical v5-to-v6 transition. STARTUP_LAST is 0.
  migrated.fields.maximumVolume = 100;
  migrated.fields.startupMode = 0;
  migrated.fields.startupFixedVolume = 20;
  migrated.fields.lastUserVolume = volumeRawToUser(source.volume);
  migrated.btEnabled = supportsBt ? 1 : 0;
  if (!computeCrc(migrated, migrated.crc32)) return false;
  output = migrated;
  return true;
}

ConfigRecordStatus loadConfigRecord(const uint8_t* input, std::size_t inputSize,
                                    bool supportsBt, config_v7_t& output) {
  if (!input) return ConfigRecordStatus::INVALID_ARGUMENT;
  if (inputSize < 4) return ConfigRecordStatus::INVALID_LENGTH;
  Reader header{input, inputSize};
  const uint16_t magic = header.u16();
  const uint16_t version = header.u16();
  if (magic == kConfigV7Magic) return parseConfigV7(input, inputSize, output);
  if (magic != kLegacyConfigMagic) return ConfigRecordStatus::DEFAULTS_REQUIRED;
  if (version != kConfigV5 && version != kConfigV6)
    return ConfigRecordStatus::DEFAULTS_REQUIRED;
  const std::size_t expected = version == kConfigV5 ? 250 : 254;
  if (inputSize != expected) return ConfigRecordStatus::INVALID_LENGTH;
  // Explicit LE field reads validate bool bytes before constructing bools.
  // No raw blob is copied into a C++ object or reinterpreted as another version.
  Reader reader{input, inputSize, true};
  if (version == kConfigV5) {
    config_v5_t legacy{};
    readPrefix(reader, legacy);
    if (!reader.valid) return ConfigRecordStatus::INVALID_BOOL;
    migrateConfigV5ToV7(legacy, supportsBt, output);
    return ConfigRecordStatus::MIGRATED_V5;
  }
  config_v6_t legacy{};
  readFields(reader, legacy);
  if (!reader.valid) return ConfigRecordStatus::INVALID_BOOL;
  migrateConfigV6ToV7(legacy, supportsBt, output);
  return ConfigRecordStatus::MIGRATED_V6;
}

ConfigRecordStatus loadConfigArea(const uint8_t* input, std::size_t size,
                                  bool supportsBt, config_v7_t& output) {
  if (!input || size < 4) return loadConfigRecord(input, size, supportsBt, output);
  Reader header{input, size};
  const uint16_t magic = header.u16();
  const uint16_t version = header.u16();
  std::size_t recordSize = size;
  if (magic == kConfigV7Magic && version == kConfigV7) recordSize = kConfigV7SerializedSize;
  if (magic == kLegacyConfigMagic && version == kConfigV5) recordSize = 250;
  if (magic == kLegacyConfigMagic && version == kConfigV6) recordSize = 254;
  return loadConfigRecord(input, size < recordSize ? size : recordSize, supportsBt, output);
}

}  // namespace config_format
}  // namespace voxone
