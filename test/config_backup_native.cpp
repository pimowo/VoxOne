#include "../src/core/config_backup.h"
#include "../src/hardware/hardware_descriptor.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace voxone::config_backup;
using namespace voxone::config_format;
using namespace voxone::hardware;

namespace {

constexpr std::size_t kConfigOffset = 34;
constexpr std::size_t kHardwareOffset = kConfigOffset + kConfigV7SerializedSize;
constexpr std::size_t kMqttOffset =
    kHardwareOffset + kRuntimeHardwareConfigSerializedSize;
constexpr std::size_t kUiOffset = kMqttOffset + kMqttWireSize;

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
    return beginOk;
  }
  bool stage(const Data& candidate) override {
    ++stages;
    if (!stageOk) return false;
    staged = candidate.config.fields.volume;
    current = staged;
    return true;
  }
  bool commit() override {
    ++commits;
    return commitOk;
  }
  bool rollback() override {
    ++rollbacks;
    if (rollbackOk) current = snapshot;
    return rollbackOk;
  }
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

  Data parsed{};
  assert(parse(bytes.data(), bytes.size(), hardwareCapabilities(), parsed) ==
         Status::Ok);
  assertDataEqual(source, parsed);

  // Canonical serialize -> parse -> serialize loses no persistent semantics.
  std::vector<uint8_t> second(serializedSize(parsed));
  std::size_t written = 0;
  assert(serialize(parsed, hardwareCapabilities(), second.data(), second.size(),
                   written) == Status::Ok);
  assert(bytes == second);

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
  wrongSchema[4] = 2;
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
  assert(storage.begins == 1 && storage.stages == 1 && storage.commits == 1);

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

  // Runtime-only state is not part of Data and cannot affect the artifact.
  int runtimeMute = 0;
  std::vector<uint8_t> beforeRuntimeChange = makeBackup(source);
  runtimeMute = 1;
  std::vector<uint8_t> afterRuntimeChange = makeBackup(source);
  assert(runtimeMute == 1 && beforeRuntimeChange == afterRuntimeChange);
  return 0;
}
