#include "../src/core/config_format.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace voxone::config_format;

namespace {

config_v6_t legacyFixture() {
  config_v6_t value{};
  // Make every non-bool field and every fixed array observable in the test.
  std::memset(&value, 0x5a, sizeof(value));
  value.config_set = kConfigMagic;
  value.version = kConfigV6;
  value.audioinfo = true;
  value.vumeter = false;
  value.flipscreen = true;
  value.invertdisplay = false;
  value.numplaylist = true;
  value.fliptouch = false;
  value.dbgtouch = true;
  value.dspon = true;
  value.sdsnuffle = false;
  value.btnpullup = true;
  value.encpullup = false;
  value.enchalf = true;
  value.enc2pullup = false;
  value.enc2half = true;
  value.forcemono = false;
  value.i2sinternal = true;
  value.rotate90 = false;
  value.screensaverEnabled = true;
  value.screensaverBlank = false;
  value.screensaverPlayingEnabled = true;
  value.screensaverPlayingBlank = false;
  value.skipPlaylistUpDown = true;
  value.reservedTelnet = false;
  value.watchdog = true;
  return value;
}

void assertFieldsEqual(const config_v6_t& a, const config_v6_t& b) {
#define CHECK(name) assert(a.name == b.name)
  CHECK(config_set); CHECK(version); CHECK(volume); CHECK(balance);
  CHECK(trebble); CHECK(middle); CHECK(bass); CHECK(lastStation);
  CHECK(countStation); CHECK(lastSSID); CHECK(audioinfo); CHECK(smartstart);
  CHECK(tzHour); CHECK(tzMin); CHECK(timezoneOffset); CHECK(vumeter);
  CHECK(softapdelay); CHECK(flipscreen); CHECK(invertdisplay);
  CHECK(numplaylist); CHECK(fliptouch); CHECK(dbgtouch); CHECK(dspon);
  CHECK(brightness); CHECK(contrast);
  assert(std::memcmp(a.sntp1, b.sntp1, sizeof(a.sntp1)) == 0);
  assert(std::memcmp(a.sntp2, b.sntp2, sizeof(a.sntp2)) == 0);
  assert(std::memcmp(a.reservedWeather, b.reservedWeather,
                     sizeof(a.reservedWeather)) == 0);
  CHECK(_reserved); CHECK(lastSdStation); CHECK(sdsnuffle);
  CHECK(volsteps); CHECK(encacc); CHECK(play_mode); CHECK(irtlp);
  CHECK(btnpullup); CHECK(btnlongpress); CHECK(btnclickticks);
  CHECK(btnpressticks); CHECK(encpullup); CHECK(enchalf);
  CHECK(enc2pullup); CHECK(enc2half); CHECK(forcemono); CHECK(i2sinternal);
  CHECK(rotate90); CHECK(screensaverEnabled); CHECK(screensaverTimeout);
  CHECK(screensaverBlank); CHECK(screensaverPlayingEnabled);
  CHECK(screensaverPlayingTimeout); CHECK(screensaverPlayingBlank);
  assert(std::memcmp(a.mdnsname, b.mdnsname, sizeof(a.mdnsname)) == 0);
  CHECK(skipPlaylistUpDown); CHECK(abuff); CHECK(reservedTelnet);
  CHECK(watchdog); CHECK(timeSyncInterval); CHECK(timeSyncIntervalRTC);
  CHECK(reservedWeatherSyncInterval); CHECK(maximumVolume);
  CHECK(startupMode); CHECK(startupFixedVolume); CHECK(lastUserVolume);
#undef CHECK
}

uint32_t referenceCrc32(const uint8_t* bytes, std::size_t size) {
  uint32_t crc = 0xffffffffu;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 1u) ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
  }
  return crc ^ 0xffffffffu;
}

void writeCrc(uint8_t* bytes) {
  const uint32_t crc = referenceCrc32(bytes, kConfigV7CrcOffset);
  for (unsigned i = 0; i < 4; ++i)
    bytes[kConfigV7CrcOffset + i] = static_cast<uint8_t>(crc >> (8 * i));
}

}  // namespace

int main() {
  static_assert(sizeof(config_v6_t) == 254, "v6 size");
  static_assert(offsetof(config_v6_t, config_set) == 0, "v6 magic");
  static_assert(offsetof(config_v6_t, version) == 2, "v6 version");
  static_assert(offsetof(config_v6_t, lastUserVolume) == 253, "v6 tail");
  static_assert(kConfigV7SerializedSize == 255, "v7 wire size");
  static_assert(kConfigV7SerializedSize <= 268, "v7 EEPROM capacity");

  const config_v6_t legacy = legacyFixture();
  config_v7_t enabled{};
  assert(migrateConfigV6ToV7(legacy, true, enabled));
  assert(enabled.fields.version == kConfigV7);
  assert(enabled.btEnabled == 1);
  assert(validateConfigV7(enabled));
  config_v6_t expected = legacy;
  expected.version = kConfigV7;
  assertFieldsEqual(expected, enabled.fields);

  config_v7_t disabled{};
  assert(migrateConfigV6ToV7(legacy, false, disabled));
  assert(disabled.btEnabled == 0);
  assert(validateConfigV7(disabled));
  assertFieldsEqual(expected, disabled.fields);

  uint8_t bytes[kConfigV7SerializedSize]{};
  assert(serializeConfigV7(enabled, bytes, sizeof(bytes)));
  assert(bytes[0] == 0xa6 && bytes[1] == 0x10);  // Magic 4262, little endian.
  assert(bytes[2] == 7 && bytes[3] == 0);
  assert(bytes[kConfigV7BtEnabledOffset] == 1);
  assert(enabled.crc32 == referenceCrc32(bytes, kConfigV7CrcOffset));

  config_v7_t decoded{};
  assert(deserializeConfigV7(bytes, sizeof(bytes), decoded));
  assertFieldsEqual(enabled.fields, decoded.fields);
  assert(decoded.btEnabled == 1 && decoded.crc32 == enabled.crc32);
  uint8_t again[kConfigV7SerializedSize]{};
  assert(serializeConfigV7(decoded, again, sizeof(again)));
  assert(std::memcmp(bytes, again, sizeof(bytes)) == 0);

  uint8_t corrupted[kConfigV7SerializedSize]{};
  std::memcpy(corrupted, bytes, sizeof(bytes));
  corrupted[4] ^= 1;
  assert(!deserializeConfigV7(corrupted, sizeof(corrupted), decoded));
  assertFieldsEqual(enabled.fields, decoded.fields);  // Failure did not mutate output.
  corrupted[4] ^= 1;
  corrupted[kConfigV7BtEnabledOffset] = 2;
  writeCrc(corrupted);
  assert(!deserializeConfigV7(corrupted, sizeof(corrupted), decoded));
  std::memcpy(corrupted, bytes, sizeof(bytes));
  corrupted[2] = 8;
  writeCrc(corrupted);
  assert(!deserializeConfigV7(corrupted, sizeof(corrupted), decoded));
  assert(!deserializeConfigV7(bytes, sizeof(bytes) - 1, decoded));
  assert(!deserializeConfigV7(nullptr, sizeof(bytes), decoded));
  assert(!serializeConfigV7(enabled, again, sizeof(again) - 1));
  assert(!serializeConfigV7(enabled, nullptr, sizeof(again)));

  config_v6_t invalid = legacy;
  invalid.version = 5;
  assert(!migrateConfigV6ToV7(invalid, true, decoded));
  assertFieldsEqual(enabled.fields, decoded.fields);
  invalid = legacy;
  invalid.config_set = 0;
  assert(!migrateConfigV6ToV7(invalid, true, decoded));
  enabled.btEnabled = 2;
  assert(!validateConfigV7(enabled));
  assert(!serializeConfigV7(enabled, again, sizeof(again)));

  std::puts("PASS config_format_native");
}
