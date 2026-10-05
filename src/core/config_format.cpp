#include "config_format.h"

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
  Reader(const uint8_t* data, std::size_t size)
      : bytes(data), capacity(size), position(0), valid(true) {}
  const uint8_t* bytes;
  std::size_t capacity;
  std::size_t position;
  bool valid;

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
  B(numplaylist); B(fliptouch); B(dbgtouch); B(dspon);
  U8(brightness); U8(contrast);
  w.chars(f.sntp1, sizeof(f.sntp1));
  w.chars(f.sntp2, sizeof(f.sntp2));
  w.raw(f.reservedWeather, sizeof(f.reservedWeather));
  U16(_reserved); U16(lastSdStation); B(sdsnuffle);
  U8(volsteps); U16(encacc); U8(play_mode); U8(irtlp);
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

void readFields(Reader& r, config_v6_t& f) {
#define U8(name) f.name = r.u8()
#define I8(name) f.name = r.i8()
#define B(name) f.name = r.boolean()
#define U16(name) f.name = r.u16()
  U16(config_set); U16(version);
  U8(volume); I8(balance); I8(trebble); I8(middle); I8(bass);
  U16(lastStation); U16(countStation); U8(lastSSID); B(audioinfo);
  U8(smartstart); I8(tzHour); I8(tzMin); U16(timezoneOffset);
  B(vumeter); U8(softapdelay); B(flipscreen); B(invertdisplay);
  B(numplaylist); B(fliptouch); B(dbgtouch); B(dspon);
  U8(brightness); U8(contrast);
  r.chars(f.sntp1, sizeof(f.sntp1));
  r.chars(f.sntp2, sizeof(f.sntp2));
  r.raw(f.reservedWeather, sizeof(f.reservedWeather));
  U16(_reserved); U16(lastSdStation); B(sdsnuffle);
  U8(volsteps); U16(encacc); U8(play_mode); U8(irtlp);
  B(btnpullup); U16(btnlongpress); U16(btnclickticks);
  U16(btnpressticks); B(encpullup); B(enchalf);
  B(enc2pullup); B(enc2half); B(forcemono); B(i2sinternal);
  B(rotate90); B(screensaverEnabled); U16(screensaverTimeout);
  B(screensaverBlank); B(screensaverPlayingEnabled);
  U16(screensaverPlayingTimeout); B(screensaverPlayingBlank);
  r.chars(f.mdnsname, sizeof(f.mdnsname));
  B(skipPlaylistUpDown); U16(abuff); B(reservedTelnet); B(watchdog);
  U16(timeSyncInterval); U16(timeSyncIntervalRTC);
  U16(reservedWeatherSyncInterval); U8(maximumVolume);
  U8(startupMode); U8(startupFixedVolume); U8(lastUserVolume);
#undef U8
#undef I8
#undef B
#undef U16
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
  return value.fields.config_set == kConfigMagic &&
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
  if (!input || inputSize != kConfigV7SerializedSize) return false;
  config_v7_t decoded{};
  Reader reader{input, inputSize};
  readFields(reader, decoded.fields);
  decoded.btEnabled = reader.u8();
  decoded.crc32 = reader.u32();
  if (!reader.valid || reader.position != kConfigV7SerializedSize ||
      !validateConfigV7(decoded)) return false;
  output = decoded;
  return true;
}

bool migrateConfigV6ToV7(const config_v6_t& source, bool supportsBt,
                         config_v7_t& output) {
  if (source.config_set != kConfigMagic || source.version != kConfigV6)
    return false;
  config_v7_t migrated{};
  migrated.fields = source;
  migrated.fields.version = kConfigV7;
  migrated.btEnabled = supportsBt ? 1 : 0;
  if (!computeCrc(migrated, migrated.crc32)) return false;
  output = migrated;
  return true;
}

}  // namespace config_format
}  // namespace voxone
