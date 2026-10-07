#include "../src/core/config_format.h"
#include "../src/core/volume_map.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace voxone::config_format;

namespace {

config_v6_t legacyFixture() {
  config_v6_t value{};
  // Make every non-bool field and every fixed array observable in the test.
  std::memset(&value, 0x5a, sizeof(value));
  value.config_set = kLegacyConfigMagic;
  value.version = kConfigV6;
  value.audioinfo = true;
  value.vumeter = false;
  value.flipscreen = true;
  value.invertdisplay = false;
  value.numplaylist = true;
  value.reservedInput0 = false;
  value.reservedInput1 = true;
  value.dspon = true;
  value.reservedSdFlags = false;
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
  CHECK(numplaylist); CHECK(reservedInput0); CHECK(reservedInput1); CHECK(dspon);
  CHECK(brightness); CHECK(contrast);
  assert(std::memcmp(a.sntp1, b.sntp1, sizeof(a.sntp1)) == 0);
  assert(std::memcmp(a.sntp2, b.sntp2, sizeof(a.sntp2)) == 0);
  assert(std::memcmp(a.reservedWeather, b.reservedWeather,
                     sizeof(a.reservedWeather)) == 0);
  CHECK(_reserved); CHECK(reservedSdStation); CHECK(reservedSdFlags);
  CHECK(volsteps); CHECK(encacc); CHECK(reservedPlayMode); CHECK(irtlp);
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

// Independent fixed-offset byte fixture, never a dump of a C++ struct.
// Offsets are the historical v5/v6 ABI; nonzero padding must be ignored.
void legacyBytes(uint8_t (&bytes)[254], config_v6_t& expected) {
  std::memset(bytes, 0xe3, sizeof(bytes));
  expected.config_set = 4262;
  bytes[0] = static_cast<uint8_t>(4262);
  bytes[1] = static_cast<uint8_t>(4262 >> 8);
  expected.version = 6;
  bytes[2] = static_cast<uint8_t>(6);
  bytes[3] = static_cast<uint8_t>(6 >> 8);
  expected.volume = 5;
  bytes[4] = static_cast<uint8_t>(5);
  expected.balance = -7;
  bytes[5] = static_cast<uint8_t>(-7);
  expected.trebble = -7;
  bytes[6] = static_cast<uint8_t>(-7);
  expected.middle = -7;
  bytes[7] = static_cast<uint8_t>(-7);
  expected.bass = -7;
  bytes[8] = static_cast<uint8_t>(-7);
  expected.lastStation = 1010;
  bytes[10] = static_cast<uint8_t>(1010);
  bytes[11] = static_cast<uint8_t>(1010 >> 8);
  expected.countStation = 1012;
  bytes[12] = static_cast<uint8_t>(1012);
  bytes[13] = static_cast<uint8_t>(1012 >> 8);
  expected.lastSSID = 15;
  bytes[14] = static_cast<uint8_t>(15);
  expected.audioinfo = 0;
  bytes[15] = static_cast<uint8_t>(0);
  expected.smartstart = 17;
  bytes[16] = static_cast<uint8_t>(17);
  expected.tzHour = -7;
  bytes[17] = static_cast<uint8_t>(-7);
  expected.tzMin = -7;
  bytes[18] = static_cast<uint8_t>(-7);
  expected.timezoneOffset = 1020;
  bytes[20] = static_cast<uint8_t>(1020);
  bytes[21] = static_cast<uint8_t>(1020 >> 8);
  expected.vumeter = 1;
  bytes[22] = static_cast<uint8_t>(1);
  expected.softapdelay = 24;
  bytes[23] = static_cast<uint8_t>(24);
  expected.flipscreen = 0;
  bytes[24] = static_cast<uint8_t>(0);
  expected.invertdisplay = 1;
  bytes[25] = static_cast<uint8_t>(1);
  expected.numplaylist = 1;
  bytes[26] = static_cast<uint8_t>(1);
  expected.reservedInput0 = 0;
  bytes[27] = static_cast<uint8_t>(0);
  expected.reservedInput1 = 1;
  bytes[28] = static_cast<uint8_t>(1);
  expected.dspon = 1;
  bytes[29] = static_cast<uint8_t>(1);
  expected.brightness = 31;
  bytes[30] = static_cast<uint8_t>(31);
  expected.contrast = 32;
  bytes[31] = static_cast<uint8_t>(32);
  for (std::size_t i = 0; i < 35; ++i) {
    bytes[32 + i] = static_cast<uint8_t>(33 + (i % 60));
    expected.sntp1[i] = static_cast<char>(bytes[32 + i]);
  }
  for (std::size_t i = 0; i < 35; ++i) {
    bytes[67 + i] = static_cast<uint8_t>(33 + (i % 60));
    expected.sntp2[i] = static_cast<char>(bytes[67 + i]);
  }
  for (std::size_t i = 0; i < 79; ++i) {
    bytes[102 + i] = static_cast<uint8_t>(33 + (i % 60));
    expected.reservedWeather[i] = static_cast<uint8_t>(bytes[102 + i]);
  }
  expected._reserved = 1182;
  bytes[182] = static_cast<uint8_t>(1182);
  bytes[183] = static_cast<uint8_t>(1182 >> 8);
  expected.reservedSdStation = 1184;
  bytes[184] = static_cast<uint8_t>(1184);
  bytes[185] = static_cast<uint8_t>(1184 >> 8);
  expected.reservedSdFlags = 0;
  bytes[186] = static_cast<uint8_t>(0);
  expected.volsteps = 8;
  bytes[187] = static_cast<uint8_t>(8);
  expected.encacc = 1188;
  bytes[188] = static_cast<uint8_t>(1188);
  bytes[189] = static_cast<uint8_t>(1188 >> 8);
  expected.reservedPlayMode = 11;
  bytes[190] = static_cast<uint8_t>(11);
  expected.irtlp = 12;
  bytes[191] = static_cast<uint8_t>(12);
  expected.btnpullup = 0;
  bytes[192] = static_cast<uint8_t>(0);
  expected.btnlongpress = 1194;
  bytes[194] = static_cast<uint8_t>(1194);
  bytes[195] = static_cast<uint8_t>(1194 >> 8);
  expected.btnclickticks = 1196;
  bytes[196] = static_cast<uint8_t>(1196);
  bytes[197] = static_cast<uint8_t>(1196 >> 8);
  expected.btnpressticks = 1198;
  bytes[198] = static_cast<uint8_t>(1198);
  bytes[199] = static_cast<uint8_t>(1198 >> 8);
  expected.encpullup = 1;
  bytes[200] = static_cast<uint8_t>(1);
  expected.enchalf = 0;
  bytes[201] = static_cast<uint8_t>(0);
  expected.enc2pullup = 1;
  bytes[202] = static_cast<uint8_t>(1);
  expected.enc2half = 1;
  bytes[203] = static_cast<uint8_t>(1);
  expected.forcemono = 0;
  bytes[204] = static_cast<uint8_t>(0);
  expected.i2sinternal = 1;
  bytes[205] = static_cast<uint8_t>(1);
  expected.rotate90 = 1;
  bytes[206] = static_cast<uint8_t>(1);
  expected.screensaverEnabled = 0;
  bytes[207] = static_cast<uint8_t>(0);
  expected.screensaverTimeout = 1208;
  bytes[208] = static_cast<uint8_t>(1208);
  bytes[209] = static_cast<uint8_t>(1208 >> 8);
  expected.screensaverBlank = 0;
  bytes[210] = static_cast<uint8_t>(0);
  expected.screensaverPlayingEnabled = 1;
  bytes[211] = static_cast<uint8_t>(1);
  expected.screensaverPlayingTimeout = 1212;
  bytes[212] = static_cast<uint8_t>(1212);
  bytes[213] = static_cast<uint8_t>(1212 >> 8);
  expected.screensaverPlayingBlank = 1;
  bytes[214] = static_cast<uint8_t>(1);
  for (std::size_t i = 0; i < 24; ++i) {
    bytes[215 + i] = static_cast<uint8_t>(33 + (i % 60));
    expected.mdnsname[i] = static_cast<char>(bytes[215 + i]);
  }
  expected.skipPlaylistUpDown = 1;
  bytes[239] = static_cast<uint8_t>(1);
  expected.abuff = 1240;
  bytes[240] = static_cast<uint8_t>(1240);
  bytes[241] = static_cast<uint8_t>(1240 >> 8);
  expected.reservedTelnet = 1;
  bytes[242] = static_cast<uint8_t>(1);
  expected.watchdog = 0;
  bytes[243] = static_cast<uint8_t>(0);
  expected.timeSyncInterval = 1244;
  bytes[244] = static_cast<uint8_t>(1244);
  bytes[245] = static_cast<uint8_t>(1244 >> 8);
  expected.timeSyncIntervalRTC = 1246;
  bytes[246] = static_cast<uint8_t>(1246);
  bytes[247] = static_cast<uint8_t>(1246 >> 8);
  expected.reservedWeatherSyncInterval = 1248;
  bytes[248] = static_cast<uint8_t>(1248);
  bytes[249] = static_cast<uint8_t>(1248 >> 8);
  expected.maximumVolume = 71;
  bytes[250] = static_cast<uint8_t>(71);
  expected.startupMode = 72;
  bytes[251] = static_cast<uint8_t>(72);
  expected.startupFixedVolume = 73;
  bytes[252] = static_cast<uint8_t>(73);
  expected.lastUserVolume = 74;
  bytes[253] = static_cast<uint8_t>(74);
}

void recordTests() {
  using S = ConfigRecordStatus;
  uint8_t raw[254];
  config_v6_t expected{};
  legacyBytes(raw, expected);
  config_v7_t result{};
  assert(loadConfigRecord(raw, sizeof(raw), true, result) == S::MIGRATED_V6);
  expected.config_set = kConfigV7Magic;
  expected.version = 7;
  assertFieldsEqual(expected, result.fields);
  assert(result.btEnabled == 1 && validateConfigV7(result));
  // Independent v7 fixture: historical field bytes minus four known padding
  // slots, with the new header, BT byte and reference CRC.
  uint8_t wireExpected[255]{};
  std::size_t wirePosition = 0;
  for (std::size_t i = 0; i < sizeof(raw); ++i) {
    if (i == 9 || i == 19 || i == 181 || i == 193) continue;
    wireExpected[wirePosition++] = raw[i];
  }
  assert(wirePosition == 250);
  wireExpected[0] = 0xa7; wireExpected[1] = 0xc7; wireExpected[2] = 7;
  wireExpected[250] = 1;
  writeCrc(wireExpected);
  uint8_t wireActual[255]{};
  assert(serializeConfigV7(result, wireActual, sizeof(wireActual)));
  assert(std::memcmp(wireExpected, wireActual, sizeof(wireActual)) == 0);
  assert(loadConfigRecord(wireExpected, sizeof(wireExpected), false, result) == S::LOADED_V7);
  assertFieldsEqual(expected, result.fields);
  assert(result.btEnabled == 1);
  assert(loadConfigRecord(raw, sizeof(raw), false, result) == S::MIGRATED_V6);
  assert(result.btEnabled == 0);

  raw[2] = 5;
  expected.maximumVolume = 100;
  expected.startupMode = 0;
  expected.startupFixedVolume = 20;
  expected.lastUserVolume = volumeRawToUser(expected.volume);
  assert(loadConfigRecord(raw, 250, true, result) == S::MIGRATED_V5);
  assertFieldsEqual(expected, result.fields);
  assert(result.btEnabled == 1 && validateConfigV7(result));
  assert(loadConfigRecord(raw, 250, false, result) == S::MIGRATED_V5);
  assert(result.btEnabled == 0);

  // Weather-era v5: bool at 102, latitude[10], longitude[10], key[58].
  raw[102] = 1;
  std::memcpy(raw + 103, "52.123456", 10);
  std::memcpy(raw + 113, "21.123456", 10);
  std::memset(raw + 123, 'K', 58);
  raw[242] = 1;  // Historical telnet.
  raw[248] = 30; raw[249] = 0;  // Historical weather sync interval.
  assert(loadConfigRecord(raw, 250, true, result) == S::MIGRATED_V5);
  assert(std::memcmp(result.fields.reservedWeather, raw + 102, 79) == 0);
  assert(result.fields.reservedTelnet && result.fields.reservedWeatherSyncInterval == 30);
  // Later v5 reserves the same bytes; no weather bool interpretation.
  raw[102] = 0xfe;
  assert(loadConfigRecord(raw, 250, true, result) == S::MIGRATED_V5);
  assert(result.fields.reservedWeather[0] == 0xfe);
  // Every possible raw volume uses the existing migration conversion.
  for (unsigned volume = 0; volume < 256; ++volume) {
    raw[4] = static_cast<uint8_t>(volume);
    assert(loadConfigRecord(raw, 250, false, result) == S::MIGRATED_V5);
    assert(result.fields.volume == volume);
    assert(result.fields.lastUserVolume == volumeRawToUser(static_cast<uint8_t>(volume)));
  }
  config_v5_t typed{};
  typed.config_set = kLegacyConfigMagic; typed.version = 5; typed.volume = 254;
  assert(migrateConfigV5ToV7(typed, true, result));
  assert(result.fields.lastUserVolume == 100);
  typed.volume = 0;
  assert(migrateConfigV5ToV7(typed, false, result));
  assert(result.fields.lastUserVolume == 0);
  typed.version = 4;
  assert(!migrateConfigV5ToV7(typed, false, result));
  typed.version = 5; typed.config_set = 0;
  assert(!migrateConfigV5ToV7(typed, false, result));

  uint8_t good[255]{};
  assert(serializeConfigV7(result, good, sizeof(good)));
  assert(good[0] == 0xa7 && good[1] == 0xc7);
  // Historical v6 firmware checks magic before attempting old migrations.
  const uint16_t oldMagic = static_cast<uint16_t>(good[0] | (good[1] << 8));
  assert(oldMagic != 4262);  // Old firmware chooses defaults, never migrations.
  assert(loadConfigRecord(good, sizeof(good), true, result) == S::LOADED_V7);
  assert(result.btEnabled == 0);  // Loading does not reapply hardware defaults.
  const config_v7_t before = result;
  auto failure = [&](const uint8_t* data, std::size_t size, S status) {
    assert(loadConfigRecord(data, size, true, result) == status);
    assertFieldsEqual(before.fields, result.fields);
    assert(before.btEnabled == result.btEnabled && before.crc32 == result.crc32);
  };
  for (std::size_t size = 0; size < sizeof(good); ++size)
    failure(good, size, S::INVALID_LENGTH);
  uint8_t oversized[256]{};
  std::memcpy(oversized, good, sizeof(good));
  failure(oversized, sizeof(oversized), S::INVALID_LENGTH);
  failure(nullptr, 255, S::INVALID_ARGUMENT);
  uint8_t bad[255];
  std::memcpy(bad, good, sizeof(good));
  bad[4] ^= 1; failure(bad, 255, S::INVALID_CRC);
  std::memcpy(bad, good, sizeof(good));
  bad[251] ^= 1; failure(bad, 255, S::INVALID_CRC);
  std::memcpy(bad, good, sizeof(good));
  bad[250] = 2; writeCrc(bad); failure(bad, 255, S::INVALID_BOOL);
  std::memcpy(bad, good, sizeof(good));
  bad[14] = 2; writeCrc(bad); failure(bad, 255, S::INVALID_BOOL);
  std::memcpy(bad, good, sizeof(good));
  for (uint8_t version : {uint8_t(0), uint8_t(6), uint8_t(8), uint8_t(255)}) {
    bad[2] = version; writeCrc(bad);
    failure(bad, 255, S::UNSUPPORTED_NEWER);
    failure(bad, 4, S::UNSUPPORTED_NEWER);
  }
  for (uint8_t version : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3),
                          uint8_t(4), uint8_t(7), uint8_t(255)}) {
    raw[2] = version; failure(raw, 250, S::DEFAULTS_REQUIRED);
  }
  for (uint8_t version : {uint8_t(5), uint8_t(6)}) {
    raw[2] = version;
    const std::size_t size = version == 5 ? 250 : 254;
    for (std::size_t truncated = 0; truncated < size; ++truncated)
      failure(raw, truncated, S::INVALID_LENGTH);
    // Check every legacy bool, without ever constructing an invalid C++ bool.
    const unsigned boolOffsets[] = {15,22,24,25,26,27,28,29,186,192,200,201,
      202,203,204,205,206,207,210,211,214,239,242,243};
    for (unsigned offset : boolOffsets) {
      const uint8_t saved = raw[offset];
      raw[offset] = 2; failure(raw, size, S::INVALID_BOOL); raw[offset] = saved;
    }
  }
  raw[0] = 0; failure(raw, 254, S::DEFAULTS_REQUIRED);
  assert(parseConfigV7(raw, 254, result) == S::INVALID_MAGIC);
}

}  // namespace

int main() {
  static_assert(offsetof(config_v5_t, enc2pullup) == 202,
                "v5 legacy enc2pullup offset");
  static_assert(offsetof(config_v5_t, enc2half) == 203,
                "v5 legacy enc2half offset");
  static_assert(sizeof(config_v6_t) == 254, "v6 size");
  static_assert(offsetof(config_v6_t, config_set) == 0, "v6 magic");
  static_assert(offsetof(config_v6_t, version) == 2, "v6 version");
  static_assert(offsetof(config_v6_t, enc2pullup) == 202,
                "v6 legacy enc2pullup offset");
  static_assert(offsetof(config_v6_t, enc2half) == 203,
                "v6 legacy enc2half offset");
  static_assert(offsetof(config_v6_t, reservedSdStation) == 184,
                "v6 reserved SD station offset");
  static_assert(offsetof(config_v6_t, reservedSdFlags) == 186,
                "v6 reserved SD flags offset");
  static_assert(offsetof(config_v6_t, reservedPlayMode) == 190,
                "v6 reserved play mode offset");
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
  expected.config_set = kConfigV7Magic;
  assertFieldsEqual(expected, enabled.fields);

  config_v7_t disabled{};
  assert(migrateConfigV6ToV7(legacy, false, disabled));
  assert(disabled.btEnabled == 0);
  assert(validateConfigV7(disabled));
  assertFieldsEqual(expected, disabled.fields);

  uint8_t bytes[kConfigV7SerializedSize]{};
  assert(serializeConfigV7(enabled, bytes, sizeof(bytes)));
  assert(bytes[0] == 0xa7 && bytes[1] == 0xc7);  // New magic, little endian.
  assert(bytes[2] == 7 && bytes[3] == 0);
  assert(bytes[kConfigV7ReservedSdStationOffset] == 0x5a);
  assert(bytes[kConfigV7ReservedSdStationOffset + 1] == 0x5a);
  assert(bytes[kConfigV7ReservedSdFlagsOffset] == 0);
  assert(bytes[kConfigV7ReservedPlayModeOffset] == 0x5a);
  assert(bytes[198] == 0);  // Legacy enc2pullup compatibility byte.
  assert(bytes[199] == 1);  // Legacy enc2half compatibility byte.
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

  recordTests();
  std::puts("PASS config_format_native");
}
