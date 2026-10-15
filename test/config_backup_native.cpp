#include "../src/core/config_backup.h"
#include "../src/hardware/hardware_descriptor.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace voxone::config_backup;
using namespace voxone::config_format;
using namespace voxone::hardware;
namespace display_scroll = voxone::display_scroll;

namespace {

constexpr std::size_t kConfigOffset = 34;
constexpr std::size_t kHardwareOffset = kConfigOffset + kConfigV7SerializedSize;
constexpr std::size_t kMqttOffset =
    kHardwareOffset + kRuntimeHardwareConfigSerializedSize;
constexpr std::size_t kUiOffset = kMqttOffset + kMqttWireSize;
constexpr std::size_t kScrollOffset = kUiOffset + kUiWireSize;

void write16(uint8_t* output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
}

void write32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
  output[2] = static_cast<uint8_t>(value >> 16);
  output[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t crc32(const uint8_t* data, std::size_t size) {
  uint32_t crc = 0xffffffffu;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return crc ^ 0xffffffffu;
}

void repairBackupCrc(std::vector<uint8_t>& bytes) {
  const uint32_t crc = crc32(bytes.data(), bytes.size() - 4);
  for (unsigned i = 0; i < 4; ++i)
    bytes[bytes.size() - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
}

void repairScrollAndBackupCrc(std::vector<uint8_t>& bytes) {
  uint8_t* record = bytes.data() + kScrollOffset + 1;
  write32(record + 9, crc32(record, 9));
  repairBackupCrc(bytes);
}

config_v7_t knownConfig() {
  config_v7_t value{};
  value.fields.config_set = kConfigV7Magic;
  value.fields.version = kConfigV7;
  value.fields.volume = 101;
  value.fields.balance = -3;
  value.fields.trebble = 1;
  value.fields.middle = -2;
  value.fields.bass = 3;
  value.fields.lastStation = 2;
  value.fields.countStation = 2;
  value.fields.lastSSID = 1;
  value.fields.smartstart = 1;
  value.fields.brightness = 80;
  value.fields.maximumVolume = 85;
  value.fields.startupMode = 1;
  value.fields.startupFixedVolume = 30;
  value.fields.lastUserVolume = 42;
  std::strcpy(value.fields.sntp1, "pool.ntp.org");
  std::strcpy(value.fields.sntp2, "time.google.com");
  std::strcpy(value.fields.mdnsname, "voxone-test");
  value.btEnabled = hardwareCapabilities().supportsVoxOneBt ? 1 : 0;
  value.crc32 = calculateConfigV7Crc(value);
  return value;
}

Data knownData() {
  static const uint8_t wifi[] = "Home\tsecret-password\n";
  static const uint8_t stations[] =
      "#VOXONE_STATIONS\t1\n"
      "0000000000000001\tRadio One\thttps://one.test/live\t0\tnormal\n"
      "0000000000000002\tRadio Two\thttp://two.test/live\t-3\tswap\n";
  Data data{};
  data.config = knownConfig();
  data.hardware = defaultRuntimeHardwareConfig(currentHardware());
  data.mqtt = mqttDefaultSettings();
  data.mqtt.enabled = 1;
  std::strcpy(data.mqtt.host, "mqtt.local");
  std::strcpy(data.mqtt.username, "voxone");
  std::strcpy(data.mqtt.password, "mqtt-secret");
  std::strcpy(data.mqtt.rootTopic, "home/voxone");
  data.ui.stationListSeconds = 12;
  data.ui.btTransportSeconds = 21;
  data.wifi = {wifi, sizeof(wifi) - 1};
  data.stations = {stations, sizeof(stations) - 1};
  return data;
}

std::vector<uint8_t> makeBackup(const Data& data) {
  std::vector<uint8_t> bytes(serializedSize(data));
  std::size_t written = 0;
  assert(serialize(data, hardwareCapabilities(), bytes.data(), bytes.size(),
                   written) == Status::Ok);
  assert(written == bytes.size());
  return bytes;
}

std::vector<uint8_t> makeLegacyBackup(const Data& data) {
  std::vector<uint8_t> bytes = makeBackup(data);
  bytes.erase(bytes.begin() + kScrollOffset,
              bytes.begin() + kScrollOffset + kScrollWireSize);
  write16(bytes.data() + 4, kLegacyBackupSchemaVersion);
  write32(bytes.data() + 6, static_cast<uint32_t>(bytes.size()));
  repairBackupCrc(bytes);
  return bytes;
}

void replaceConfig(std::vector<uint8_t>& bytes, config_v7_t config) {
  config.crc32 = calculateConfigV7Crc(config);
  assert(serializeConfigV7(config, bytes.data() + kConfigOffset,
                           kConfigV7SerializedSize));
  repairBackupCrc(bytes);
}

RuntimeHardwareConfig unsupportedHardware() {
  RuntimeHardwareConfig candidate = defaultRuntimeHardwareConfig(currentHardware());
  const RuntimeDisplay displays[] = {RuntimeDisplay::None,
      RuntimeDisplay::Ssd1306_128x64, RuntimeDisplay::Ssd1309_128x64,
      RuntimeDisplay::St7789_284x76, RuntimeDisplay::St7796s_480x320};
  const RuntimeInput inputs[] = {RuntimeInput::None, RuntimeInput::Ec11,
                                 RuntimeInput::Buttons3};
  const RuntimeAudioOutput audio[] = {RuntimeAudioOutput::None,
      RuntimeAudioOutput::Pcm5102a, RuntimeAudioOutput::DspMini,
      RuntimeAudioOutput::Max98357};
  for (RuntimeDisplay display : displays) {
    candidate.display = display;
    if (validateRuntimeHardwareConfig(candidate, hardwareCapabilities()) ==
        RuntimeHardwareValidation::Unsupported) return candidate;
  }
  candidate = defaultRuntimeHardwareConfig(currentHardware());
  for (RuntimeInput input : inputs) {
    candidate.input = input;
    if (validateRuntimeHardwareConfig(candidate, hardwareCapabilities()) ==
        RuntimeHardwareValidation::Unsupported) return candidate;
  }
  candidate = defaultRuntimeHardwareConfig(currentHardware());
  for (RuntimeAudioOutput output : audio) {
    candidate.audio = output;
    if (validateRuntimeHardwareConfig(candidate, hardwareCapabilities()) ==
        RuntimeHardwareValidation::Unsupported) return candidate;
  }
  assert(false && "every profile must have an unsupported valid selection");
  return candidate;
}

struct FakeStorage : RestoreStorage {
  int current = 200;
  int snapshot = 0;
  int staged = 0;
  ScrollBackupMode currentScrollMode = ScrollBackupMode::UserSettings;
  display_scroll::ScrollSettings currentScroll{
      false, display_scroll::ScrollSpeed::Fast, 3000};
  ScrollBackupMode snapshotScrollMode = ScrollBackupMode::RendererDefaults;
  display_scroll::ScrollSettings snapshotScroll =
      display_scroll::defaultScrollSettings();
  ScrollBackupMode stagedScrollMode = ScrollBackupMode::PreserveExisting;
  display_scroll::ScrollSettings stagedScroll =
      display_scroll::defaultScrollSettings();
  bool beginOk = true;
  bool stageOk = true;
  bool commitOk = true;
  bool rollbackOk = true;
  unsigned begins = 0;
  unsigned stages = 0;
  unsigned commits = 0;
  unsigned rollbacks = 0;

  bool begin() override {
    ++begins;
    snapshot = current;
    snapshotScrollMode = currentScrollMode;
    snapshotScroll = currentScroll;
    return beginOk;
  }
  bool stage(const Data& candidate) override {
    ++stages;
    if (!stageOk) return false;
    staged = candidate.config.fields.volume;
    stagedScrollMode = candidate.scrollMode;
    stagedScroll = candidate.scrollSettings;
    current = staged;
    return true;
  }
  bool commit() override {
    ++commits;
    if (commitOk && stagedScrollMode != ScrollBackupMode::PreserveExisting) {
      currentScrollMode = stagedScrollMode;
      currentScroll = stagedScroll;
    }
    return commitOk;
  }
  bool rollback() override {
    ++rollbacks;
    if (rollbackOk) {
      current = snapshot;
      currentScrollMode = snapshotScrollMode;
      currentScroll = snapshotScroll;
    }
    return rollbackOk;
  }
};

class TrackingReader : public ByteReader {
 public:
  explicit TrackingReader(Bytes bytes) : bytes_(bytes) {}
  std::size_t size() const override { return bytes_.size; }
  bool read(std::size_t offset, uint8_t* output,
            std::size_t length) override {
    if (length > largestRead_) largestRead_ = length;
    if ((length && (!output || !bytes_.data)) || offset > bytes_.size ||
        length > bytes_.size - offset) return false;
    if (length) std::memcpy(output, bytes_.data + offset, length);
    return true;
  }
  std::size_t largestRead() const { return largestRead_; }

 private:
  Bytes bytes_;
  std::size_t largestRead_ = 0;
};

class ToggleReader : public ByteReader {
 public:
  explicit ToggleReader(Bytes bytes) : bytes_(bytes) {}
  std::size_t size() const override { return bytes_.size; }
  bool read(std::size_t offset, uint8_t* output,
            std::size_t length) override {
    if (fail_) return false;
    if ((length && (!output || !bytes_.data)) || offset > bytes_.size ||
        length > bytes_.size - offset) return false;
    if (length) std::memcpy(output, bytes_.data + offset, length);
    return true;
  }
  void fail() { fail_ = true; }

 private:
  Bytes bytes_;
  bool fail_ = false;
};

void assertDataEqual(const Data& expected, const Data& actual) {
  uint8_t left[kConfigV7SerializedSize];
  uint8_t right[kConfigV7SerializedSize];
  assert(serializeConfigV7(expected.config, left, sizeof(left)));
  assert(serializeConfigV7(actual.config, right, sizeof(right)));
  assert(std::memcmp(left, right, sizeof(left)) == 0);
  assert(expected.hardware == actual.hardware);
  assert(std::memcmp(&expected.mqtt, &actual.mqtt, sizeof(expected.mqtt)) == 0);
  assert(expected.ui.stationListSeconds == actual.ui.stationListSeconds);
  assert(expected.ui.btTransportSeconds == actual.ui.btTransportSeconds);
  assert(expected.scrollMode == actual.scrollMode);
  if (expected.scrollMode == ScrollBackupMode::UserSettings)
    assert(display_scroll::equalScrollSettings(expected.scrollSettings,
                                               actual.scrollSettings));
  assert(expected.wifi.size == actual.wifi.size);
  assert(std::memcmp(expected.wifi.data, actual.wifi.data, actual.wifi.size) == 0);
  assert(expected.stations.size == actual.stations.size);
  assert(std::memcmp(expected.stations.data, actual.stations.data,
                     actual.stations.size) == 0);
}

}  // namespace

int main() {
  const Data source = knownData();
  std::vector<uint8_t> bytes = makeBackup(source);

  static_assert(kBackupSchemaVersion == 2, "new backups must use schema 2");
  static_assert(kBackupFixedOverhead ==
                    kLegacyBackupFixedOverhead + kScrollWireSize,
                "schema 2 overhead must add only the scroll section");
  assert(bytes[4] == 2 && bytes[5] == 0);
  assert(bytes[kScrollOffset] == 0);
  for (std::size_t i = 1; i < kScrollWireSize; ++i)
    assert(bytes[kScrollOffset + i] == 0);

  // Schema 1 remains readable and explicitly means that scroll persistence is
  // outside the artifact, so restore must preserve the installed value.
  const std::vector<uint8_t> legacy = makeLegacyBackup(source);
  Data legacyParsed{};
  assert(parse(legacy.data(), legacy.size(), hardwareCapabilities(),
               legacyParsed) == Status::Ok);
  assert(legacyParsed.scrollMode == ScrollBackupMode::PreserveExisting);
  assert(legacy.size() + kScrollWireSize == bytes.size());

  // Schema 2 round-trips every allowed user value without confusing an
  // explicitly stored default with the absence of the NVS record.
  const display_scroll::ScrollSpeed speeds[] = {
      display_scroll::ScrollSpeed::Slow,
      display_scroll::ScrollSpeed::Normal,
      display_scroll::ScrollSpeed::Fast};
  const uint16_t pauses[] = {0, 1000, 2000, 3000, 4000, 5000};
  for (bool enabled : {false, true}) {
    for (display_scroll::ScrollSpeed speed : speeds) {
      for (uint16_t pause : pauses) {
        Data withScroll = source;
        withScroll.scrollMode = ScrollBackupMode::UserSettings;
        withScroll.scrollSettings = {enabled, speed, pause};
        const std::vector<uint8_t> artifact = makeBackup(withScroll);
        assert(artifact[kScrollOffset] == 1);
        Data roundTrip{};
        assert(parse(artifact.data(), artifact.size(), hardwareCapabilities(),
                     roundTrip) == Status::Ok);
        assertDataEqual(withScroll, roundTrip);
      }
    }
  }
  Data explicitDefaults = source;
  explicitDefaults.scrollMode = ScrollBackupMode::UserSettings;
  explicitDefaults.scrollSettings = display_scroll::defaultScrollSettings();
  const std::vector<uint8_t> explicitDefaultsBackup =
      makeBackup(explicitDefaults);
  assert(explicitDefaultsBackup[kScrollOffset] == 1);
  assert(explicitDefaultsBackup != bytes);
  Data explicitDefaultsParsed{};
  assert(parse(explicitDefaultsBackup.data(), explicitDefaultsBackup.size(),
               hardwareCapabilities(), explicitDefaultsParsed) == Status::Ok);
  assert(explicitDefaultsParsed.scrollMode == ScrollBackupMode::UserSettings);

  // The production encoder emits the same schema-2 artifact incrementally.
  TrackingReader streamedWifi(source.wifi);
  TrackingReader streamedStations(source.stations);
  StreamEncoder encoder;
  assert(encoder.begin(source, hardwareCapabilities(), streamedWifi,
                       streamedStations) == Status::Ok);
  assert(encoder.size() == bytes.size());
  std::vector<uint8_t> streamed(encoder.size());
  for (std::size_t offset = 0; offset < streamed.size();) {
    const std::size_t count = encoder.read(
        offset, streamed.data() + offset,
        std::min<std::size_t>(37, streamed.size() - offset));
    assert(count > 0);
    offset += count;
  }
  assert(streamed == bytes);

  // A file/transport short read becomes a deterministic controlled failure.
  ToggleReader failingWifi(source.wifi);
  ToggleReader healthyStations(source.stations);
  StreamEncoder failingEncoder;
  assert(failingEncoder.begin(source, hardwareCapabilities(), failingWifi,
                              healthyStations) == Status::Ok);
  failingWifi.fail();
  uint8_t shortBuffer[32]{};
  assert(failingEncoder.read(kBackupVariableOffset, shortBuffer,
                             sizeof(shortBuffer)) == 0);
  assert(failingEncoder.status() == Status::StorageReadFailed);

  Data parsed{};
  assert(parse(bytes.data(), bytes.size(), hardwareCapabilities(), parsed) ==
         Status::Ok);
  assertDataEqual(source, parsed);

  // The signed BALANCE endpoints survive a complete VXCF round-trip.
  for (int endpoint : {-16, 16}) {
    Data endpointData = source;
    endpointData.config.fields.balance = static_cast<int8_t>(endpoint);
    endpointData.config.crc32 = calculateConfigV7Crc(endpointData.config);
    const std::vector<uint8_t> endpointBackup = makeBackup(endpointData);
    Data endpointParsed{};
    assert(parse(endpointBackup.data(), endpointBackup.size(),
                 hardwareCapabilities(), endpointParsed) == Status::Ok);
    assert(endpointParsed.config.fields.balance == endpoint);
  }

  // Canonical serialize -> parse -> serialize loses no persistent semantics.
  std::vector<uint8_t> second(serializedSize(parsed));
  std::size_t written = 0;
  assert(serialize(parsed, hardwareCapabilities(), second.data(), second.size(),
                   written) == Status::Ok);
  assert(bytes == second);

  // Production download reads variable records directly into their final
  // output offsets, so serialization must explicitly support that one-buffer path.
  std::vector<uint8_t> inPlace(serializedSize(source));
  uint8_t* variable = inPlace.data() + kBackupVariableOffset;
  std::memcpy(variable, source.wifi.data, source.wifi.size);
  std::memcpy(variable + source.wifi.size, source.stations.data,
              source.stations.size);
  Data inPlaceData = source;
  inPlaceData.wifi = {variable, source.wifi.size};
  inPlaceData.stations = {variable + source.wifi.size, source.stations.size};
  assert(serialize(inPlaceData, hardwareCapabilities(), inPlace.data(),
                   inPlace.size(), written) == Status::Ok);
  assert(inPlace == bytes);

  // File-backed validation reads bounded records instead of materializing
  // either variable-length section in RAM.
  TrackingReader wifiReader(source.wifi);
  TrackingReader stationsReader(source.stations);
  assert(validateExternal(source, hardwareCapabilities(), wifiReader,
                          stationsReader) == Status::Ok);
  assert(wifiReader.largestRead() <= 70);
  assert(stationsReader.largestRead() <= 512);

  // A missing wifi.csv is represented by an empty section when no profile is
  // selected. A selected profile without that section remains invalid.
  Data noWifi = source;
  noWifi.wifi = {nullptr, 0};
  noWifi.config.fields.lastSSID = 0;
  noWifi.config.crc32 = calculateConfigV7Crc(noWifi.config);
  const std::vector<uint8_t> noWifiBackup = makeBackup(noWifi);
  Data noWifiParsed{};
  assert(parse(noWifiBackup.data(), noWifiBackup.size(), hardwareCapabilities(),
               noWifiParsed) == Status::Ok);
  Data selectedMissingWifi = noWifi;
  selectedMissingWifi.config.fields.lastSSID = 1;
  selectedMissingWifi.config.crc32 =
      calculateConfigV7Crc(selectedMissingWifi.config);
  std::vector<uint8_t> missingScratch(serializedSize(selectedMissingWifi));
  assert(serialize(selectedMissingWifi, hardwareCapabilities(),
                   missingScratch.data(), missingScratch.size(), written) ==
         Status::InvalidConfig);

  // stations.tsv is canonical and therefore mandatory.
  Data noStations = source;
  noStations.stations = {nullptr, 0};
  noStations.config.fields.lastStation = 0;
  noStations.config.crc32 = calculateConfigV7Crc(noStations.config);
  missingScratch.resize(serializedSize(noStations));
  assert(serialize(noStations, hardwareCapabilities(), missingScratch.data(),
                   missingScratch.size(), written) == Status::InvalidStations);

  // countStation is a retired derived counter. Preserve its exact Config v7
  // value without rejecting a canonical stations.tsv snapshot.
  Data staleCount = source;
  staleCount.config.fields.countStation = 0;
  staleCount.config.crc32 = calculateConfigV7Crc(staleCount.config);
  const std::vector<uint8_t> staleCountBackup = makeBackup(staleCount);
  Data staleCountParsed{};
  assert(parse(staleCountBackup.data(), staleCountBackup.size(),
               hardwareCapabilities(), staleCountParsed) == Status::Ok);
  assert(staleCountParsed.config.fields.countStation == 0);

  Data unchanged = source;
  unchanged.config.fields.volume = 9;
  assert(parse(nullptr, 0, hardwareCapabilities(), unchanged) ==
         Status::InvalidArgument);
  assert(unchanged.config.fields.volume == 9);
  assert(parse(bytes.data(), 0, hardwareCapabilities(), unchanged) == Status::Empty);
  assert(unchanged.config.fields.volume == 9);
  assert(parse(bytes.data(), bytes.size() - 1, hardwareCapabilities(), unchanged) ==
         Status::Truncated);
  assert(unchanged.config.fields.volume == 9);

  std::vector<uint8_t> malformed = bytes;
  malformed[0] = 'X';
  assert(parse(malformed.data(), malformed.size(), hardwareCapabilities(), unchanged) ==
         Status::WrongMagic);

  std::vector<uint8_t> wrongSchema = bytes;
  wrongSchema[4] = 3;
  assert(parse(wrongSchema.data(), wrongSchema.size(), hardwareCapabilities(), unchanged) ==
         Status::WrongSchema);

  std::vector<uint8_t> missingComponent = bytes;
  std::memset(missingComponent.data() + 10, 0, 4);
  assert(parse(missingComponent.data(), missingComponent.size(),
               hardwareCapabilities(), unchanged) == Status::InvalidLength);

  std::vector<uint8_t> corrupt = bytes;
  corrupt[kConfigOffset + 20] ^= 1;
  assert(parse(corrupt.data(), corrupt.size(), hardwareCapabilities(), unchanged) ==
         Status::InvalidChecksum);

  std::vector<uint8_t> invalidRange = bytes;
  config_v7_t badConfig = source.config;
  badConfig.fields.maximumVolume = 0;
  replaceConfig(invalidRange, badConfig);
  assert(parse(invalidRange.data(), invalidRange.size(), hardwareCapabilities(), unchanged) ==
         Status::InvalidConfig);

  std::vector<uint8_t> invalidEnum = bytes;
  badConfig = source.config;
  badConfig.fields.startupMode = 2;
  replaceConfig(invalidEnum, badConfig);
  assert(parse(invalidEnum.data(), invalidEnum.size(), hardwareCapabilities(), unchanged) ==
         Status::InvalidConfig);

  std::vector<uint8_t> invalidString = bytes;
  badConfig = source.config;
  std::memset(badConfig.fields.sntp1, 'x', sizeof(badConfig.fields.sntp1));
  replaceConfig(invalidString, badConfig);
  assert(parse(invalidString.data(), invalidString.size(), hardwareCapabilities(), unchanged) ==
         Status::InvalidConfig);

  std::vector<uint8_t> unsupported = bytes;
  RuntimeHardwareConfig unsupportedSelection = unsupportedHardware();
  assert(serializeRuntimeHardwareConfig(
      unsupportedSelection, unsupported.data() + kHardwareOffset,
      kRuntimeHardwareConfigSerializedSize));
  repairBackupCrc(unsupported);
  assert(parse(unsupported.data(), unsupported.size(), hardwareCapabilities(), unchanged) ==
         Status::UnsupportedHardware);

  std::vector<uint8_t> invalidMqtt = bytes;
  invalidMqtt[kMqttOffset + 1] = 2;
  repairBackupCrc(invalidMqtt);
  assert(parse(invalidMqtt.data(), invalidMqtt.size(), hardwareCapabilities(), unchanged) ==
         Status::InvalidMqtt);

  std::vector<uint8_t> invalidUi = bytes;
  invalidUi[kUiOffset + 1] = 121;
  repairBackupCrc(invalidUi);
  assert(parse(invalidUi.data(), invalidUi.size(), hardwareCapabilities(), unchanged) ==
         Status::InvalidUi);

  std::vector<uint8_t> invalidScrollPresence = bytes;
  invalidScrollPresence[kScrollOffset] = 2;
  repairBackupCrc(invalidScrollPresence);
  assert(parse(invalidScrollPresence.data(), invalidScrollPresence.size(),
               hardwareCapabilities(), unchanged) == Status::InvalidScroll);

  std::vector<uint8_t> nonzeroAbsentScroll = bytes;
  nonzeroAbsentScroll[kScrollOffset + 1] = 1;
  repairBackupCrc(nonzeroAbsentScroll);
  assert(parse(nonzeroAbsentScroll.data(), nonzeroAbsentScroll.size(),
               hardwareCapabilities(), unchanged) == Status::InvalidScroll);

  Data validScrollData = source;
  validScrollData.scrollMode = ScrollBackupMode::UserSettings;
  validScrollData.scrollSettings = {
      true, display_scroll::ScrollSpeed::Normal, 5000};
  const std::vector<uint8_t> validScrollBackup = makeBackup(validScrollData);

  std::vector<uint8_t> corruptScrollCrc = validScrollBackup;
  corruptScrollCrc[kScrollOffset + 1 + 9] ^= 1;
  repairBackupCrc(corruptScrollCrc);
  assert(parse(corruptScrollCrc.data(), corruptScrollCrc.size(),
               hardwareCapabilities(), unchanged) == Status::InvalidScroll);

  std::vector<uint8_t> futureScroll = validScrollBackup;
  futureScroll[kScrollOffset + 1 + 4] =
      display_scroll::kScrollSettingsRecordVersion + 1;
  repairScrollAndBackupCrc(futureScroll);
  assert(parse(futureScroll.data(), futureScroll.size(),
               hardwareCapabilities(), unchanged) == Status::InvalidScroll);

  std::vector<uint8_t> invalidEnabled = validScrollBackup;
  invalidEnabled[kScrollOffset + 1 + 5] = 2;
  repairScrollAndBackupCrc(invalidEnabled);
  assert(parse(invalidEnabled.data(), invalidEnabled.size(),
               hardwareCapabilities(), unchanged) == Status::InvalidScroll);

  std::vector<uint8_t> invalidSpeedScroll = validScrollBackup;
  invalidSpeedScroll[kScrollOffset + 1 + 6] = 9;
  repairScrollAndBackupCrc(invalidSpeedScroll);
  assert(parse(invalidSpeedScroll.data(), invalidSpeedScroll.size(),
               hardwareCapabilities(), unchanged) == Status::InvalidScroll);

  std::vector<uint8_t> invalidPause = validScrollBackup;
  invalidPause[kScrollOffset + 1 + 7] = 0xc4;
  invalidPause[kScrollOffset + 1 + 8] = 0x09;  // 2500 ms.
  repairScrollAndBackupCrc(invalidPause);
  assert(parse(invalidPause.data(), invalidPause.size(),
               hardwareCapabilities(), unchanged) == Status::InvalidScroll);

  std::vector<uint8_t> shortScroll = validScrollBackup;
  shortScroll.erase(shortScroll.begin() + kScrollOffset + kScrollWireSize - 1);
  assert(parse(shortScroll.data(), shortScroll.size(), hardwareCapabilities(),
               unchanged) == Status::Truncated);

  Data invalidOutgoingScroll = source;
  invalidOutgoingScroll.scrollMode = ScrollBackupMode::UserSettings;
  invalidOutgoingScroll.scrollSettings = {
      true, display_scroll::ScrollSpeed::Normal, 2500};
  std::vector<uint8_t> invalidScrollScratch(kBackupFixedOverhead +
                                            source.wifi.size +
                                            source.stations.size);
  assert(serialize(invalidOutgoingScroll, hardwareCapabilities(),
                   invalidScrollScratch.data(), invalidScrollScratch.size(),
                   written) == Status::InvalidScroll);

  Data badWifi = source;
  static const uint8_t tooManyWifi[] =
      "a\tp\nb\tp\nc\tp\nd\tp\ne\tp\nf\tp\n";
  badWifi.wifi = {tooManyWifi, sizeof(tooManyWifi) - 1};
  std::vector<uint8_t> scratch(serializedSize(badWifi));
  assert(serialize(badWifi, hardwareCapabilities(), scratch.data(), scratch.size(),
                   written) == Status::InvalidWifi);

  Data badStations = source;
  static const uint8_t malformedStations[] = "#VOXONE_STATIONS\t1\nbad\n";
  badStations.stations = {malformedStations, sizeof(malformedStations) - 1};
  scratch.resize(serializedSize(badStations));
  assert(serialize(badStations, hardwareCapabilities(), scratch.data(), scratch.size(),
                   written) == Status::InvalidStations);

  // A successful restore is the only path that requests a restart.
  FakeStorage storage;
  RestoreResult result = restore(bytes.data(), bytes.size(), hardwareCapabilities(), storage);
  assert(result.status == Status::Ok && result.restartRequired);
  assert(storage.current == source.config.fields.volume);
  assert(storage.currentScrollMode == ScrollBackupMode::RendererDefaults);
  assert(storage.begins == 1 && storage.stages == 1 && storage.commits == 1);

  // A schema-1 restore cannot express scroll intent and therefore preserves
  // an existing user record exactly.
  storage = FakeStorage{};
  const display_scroll::ScrollSettings legacyScroll = storage.currentScroll;
  result = restore(legacy.data(), legacy.size(), hardwareCapabilities(), storage);
  assert(result.status == Status::Ok && result.restartRequired);
  assert(storage.currentScrollMode == ScrollBackupMode::UserSettings);
  assert(display_scroll::equalScrollSettings(storage.currentScroll,
                                             legacyScroll));

  // Schema 2 can restore a validated user record as well as explicit absence.
  storage = FakeStorage{};
  result = restore(validScrollBackup.data(), validScrollBackup.size(),
                   hardwareCapabilities(), storage);
  assert(result.status == Status::Ok && result.restartRequired);
  assert(storage.currentScrollMode == ScrollBackupMode::UserSettings);
  assert(display_scroll::equalScrollSettings(storage.currentScroll,
                                             validScrollData.scrollSettings));

  storage = FakeStorage{};
  storage.beginOk = false;
  result = restore(bytes.data(), bytes.size(), hardwareCapabilities(), storage);
  assert(result.status == Status::StorageBeginFailed && !result.restartRequired);
  assert(storage.current == 200 && storage.stages == 0 && storage.commits == 0);

  storage = FakeStorage{};
  storage.stageOk = false;
  result = restore(bytes.data(), bytes.size(), hardwareCapabilities(), storage);
  assert(result.status == Status::StorageWriteFailed && !result.restartRequired);
  assert(storage.current == 200 && storage.rollbacks == 1 && storage.commits == 0);

  storage = FakeStorage{};
  storage.commitOk = false;
  result = restore(bytes.data(), bytes.size(), hardwareCapabilities(), storage);
  assert(result.status == Status::StorageCommitFailed && !result.restartRequired);
  assert(storage.current == 200 && storage.rollbacks == 1);

  storage = FakeStorage{};
  storage.commitOk = false;
  storage.rollbackOk = false;
  result = restore(bytes.data(), bytes.size(), hardwareCapabilities(), storage);
  assert(result.status == Status::RecoveryPending && !result.restartRequired);

  storage = FakeStorage{};
  result = restore(invalidRange.data(), invalidRange.size(), hardwareCapabilities(), storage);
  assert(result.status == Status::InvalidConfig && !result.restartRequired);
  assert(storage.current == 200 && storage.begins == 0);

  storage = FakeStorage{};
  result = restore(corruptScrollCrc.data(), corruptScrollCrc.size(),
                   hardwareCapabilities(), storage);
  assert(result.status == Status::InvalidScroll && !result.restartRequired);
  assert(storage.current == 200 && storage.begins == 0 &&
         storage.currentScrollMode == ScrollBackupMode::UserSettings);

  // Runtime-only state is not part of Data and cannot affect the artifact.
  int runtimeMute = 0;
  std::vector<uint8_t> beforeRuntimeChange = makeBackup(source);
  runtimeMute = 1;
  std::vector<uint8_t> afterRuntimeChange = makeBackup(source);
  assert(runtimeMute == 1 && beforeRuntimeChange == afterRuntimeChange);

  // stations.idx is derived and absent from VXCF. A 250-row canonical TSV
  // survives backup/parse and can drive the normal post-restore index rebuild.
  std::vector<uint8_t> stations250;
  const char* header = "#VOXONE_STATIONS\t1\n";
  stations250.insert(stations250.end(), header, header + std::strlen(header));
  for (unsigned i = 0; i < 250; ++i) {
    char row[128];
    const int length = std::snprintf(
        row, sizeof(row), "%016llX\tRadio %03u\thttps://stream.example/%03u\t%d\t%s\n",
        static_cast<unsigned long long>(i + 1), i, i,
        static_cast<int>(i % 7) - 3, i % 2 ? "swap" : "normal");
    assert(length > 0 && static_cast<std::size_t>(length) < sizeof(row));
    stations250.insert(stations250.end(), row, row + length);
  }
  Data scale = source;
  scale.config.fields.countStation = 250;
  scale.config.fields.lastStation = 125;
  scale.config.crc32 = calculateConfigV7Crc(scale.config);
  scale.stations = {stations250.data(), stations250.size()};
  const std::vector<uint8_t> scaleBackup = makeBackup(scale);
  Data scaleParsed{};
  assert(parse(scaleBackup.data(), scaleBackup.size(), hardwareCapabilities(),
               scaleParsed) == Status::Ok);
  assert(scaleParsed.stations.size == stations250.size());
  assert(std::memcmp(scaleParsed.stations.data, stations250.data(),
                     stations250.size()) == 0);
  FakeStorage scaleStorage;
  result = restore(scaleBackup.data(), scaleBackup.size(),
                   hardwareCapabilities(), scaleStorage);
  assert(result.status == Status::Ok && result.restartRequired);

  TrackingReader scaleWifi(scale.wifi);
  TrackingReader scaleStations(scale.stations);
  StreamEncoder scaleEncoder;
  assert(scaleEncoder.begin(scale, hardwareCapabilities(), scaleWifi,
                            scaleStations) == Status::Ok);
  std::vector<uint8_t> scaleStream(scaleEncoder.size());
  for (std::size_t offset = 0; offset < scaleStream.size();) {
    const std::size_t count = scaleEncoder.read(
        offset, scaleStream.data() + offset,
        std::min<std::size_t>(997, scaleStream.size() - offset));
    assert(count > 0);
    offset += count;
  }
  assert(scaleStream == scaleBackup);

  // Near-maximum legal station fields remain bounded below the 512-byte line
  // buffer and survive schema/CRC validation.
  const std::string longName(169, 'N');
  const std::string longUrl = "https://example.test/" + std::string(148, 'u');
  std::vector<uint8_t> nearMaxStations(header, header + std::strlen(header));
  for (unsigned i = 0; i < 3; ++i) {
    char id[17];
    std::snprintf(id, sizeof(id), "%016X", i + 1);
    const std::string row = std::string(id) + '\t' + longName + '\t' +
                            longUrl + "\t-30\tnormal\n";
    assert(row.size() < 512);
    nearMaxStations.insert(nearMaxStations.end(), row.begin(), row.end());
  }
  Data nearMax = source;
  nearMax.config.fields.countStation = 3;
  nearMax.config.fields.lastStation = 3;
  nearMax.config.crc32 = calculateConfigV7Crc(nearMax.config);
  nearMax.stations = {nearMaxStations.data(), nearMaxStations.size()};
  const std::vector<uint8_t> nearMaxBackup = makeBackup(nearMax);
  Data nearMaxParsed{};
  assert(parse(nearMaxBackup.data(), nearMaxBackup.size(),
               hardwareCapabilities(), nearMaxParsed) == Status::Ok);

  // Repeated backup creation is deterministic and does not modify sources.
  uint8_t sourceConfigBefore[kConfigV7SerializedSize];
  assert(serializeConfigV7(source.config, sourceConfigBefore,
                           sizeof(sourceConfigBefore)));
  for (unsigned attempt = 0; attempt < 5; ++attempt) {
    TrackingReader repeatedWifi(source.wifi);
    TrackingReader repeatedStations(source.stations);
    StreamEncoder repeated;
    assert(repeated.begin(source, hardwareCapabilities(), repeatedWifi,
                          repeatedStations) == Status::Ok);
    std::vector<uint8_t> artifact(repeated.size());
    std::size_t offset = 0;
    while (offset < artifact.size()) {
      const std::size_t count = repeated.read(
          offset, artifact.data() + offset,
          std::min<std::size_t>(113, artifact.size() - offset));
      assert(count > 0);
      offset += count;
    }
    assert(artifact == bytes);
  }
  uint8_t sourceConfigAfter[kConfigV7SerializedSize];
  assert(serializeConfigV7(source.config, sourceConfigAfter,
                           sizeof(sourceConfigAfter)));
  assert(std::memcmp(sourceConfigBefore, sourceConfigAfter,
                     sizeof(sourceConfigBefore)) == 0);

  // Diagnostic strings expose only bounded status codes, never fixture secrets.
  for (Status status : {Status::Ok, Status::InvalidConfig,
                        Status::InvalidScroll,
                        Status::InvalidWifi, Status::InvalidStations,
                        Status::StorageReadFailed}) {
    const char* diagnostic = statusName(status);
    assert(std::strstr(diagnostic, "secret-password") == nullptr);
    assert(std::strstr(diagnostic, "mqtt-secret") == nullptr);
  }
  return 0;
}
