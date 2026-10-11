#include "options.h"
#include "config_restore_adapter.h"

#include <EEPROM.h>
#include <SPIFFS.h>
#include <nvs.h>

#include <cstring>

#include "config.h"
#include "config_eeprom_storage.h"
#include "config_restore_transaction.h"
#include "mqtt_config.h"
#include "playlist_store.h"
#include "ui_timeout_config.h"
#include "../hardware/hardware_descriptor.h"
#include "../hardware/runtime_hardware_config.h"

namespace voxone {
namespace config_restore {
namespace {

constexpr char kNamespace[] = "voxrestore";
constexpr char kMarkerKey[] = "marker";
constexpr char kFlagsKey[] = "flags";
constexpr char kConfigKey[] = "config";
constexpr char kHardwareKey[] = "hardware";
constexpr char kMqttKey[] = "mqtt";
constexpr char kUiKey[] = "ui";
constexpr char kWifiOld[] = "/data/.vxrestore.wifi.old";
constexpr char kStationsOld[] = "/data/.vxrestore.stations.old";
constexpr char kWifiNew[] = "/data/.vxrestore.wifi.new";
constexpr char kStationsNew[] = "/data/.vxrestore.stations.new";
constexpr char kRestoreTemp[] = "/data/.vxrestore.apply.tmp";
constexpr uint32_t kMarkerMagic = 0x52584356u;  // VXCR, little endian.
constexpr uint8_t kMarkerSchema = 1;
constexpr std::size_t kMarkerSize = 12;

enum SnapshotFlag : uint8_t {
  ConfigPresent = 1u << 0,
  HardwarePresent = 1u << 1,
  MqttPresent = 1u << 2,
  UiPresent = 1u << 3,
  WifiPresent = 1u << 4,
  StationsPresent = 1u << 5
};

uint32_t crc32(const uint8_t* data, std::size_t size) {
  uint32_t crc = 0xffffffffu;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return crc ^ 0xffffffffu;
}

void put32(uint8_t* output, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    output[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t get32(const uint8_t* input) {
  return static_cast<uint32_t>(input[0]) |
         (static_cast<uint32_t>(input[1]) << 8) |
         (static_cast<uint32_t>(input[2]) << 16) |
         (static_cast<uint32_t>(input[3]) << 24);
}

void encodeMarker(Phase phase, uint8_t (&bytes)[kMarkerSize]) {
  std::memset(bytes, 0, sizeof(bytes));
  put32(bytes, kMarkerMagic);
  bytes[4] = kMarkerSchema;
  bytes[5] = static_cast<uint8_t>(phase);
  put32(bytes + 8, crc32(bytes, 8));
}

MarkerStatus readMarkerRecord(Phase& phase) {
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return MarkerStatus::None;
  if (opened != ESP_OK) return MarkerStatus::Error;
  uint8_t bytes[kMarkerSize]{};
  std::size_t size = sizeof(bytes);
  const esp_err_t result = nvs_get_blob(handle, kMarkerKey, bytes, &size);
  nvs_close(handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return MarkerStatus::None;
  if (result != ESP_OK || size != sizeof(bytes) ||
      get32(bytes) != kMarkerMagic || bytes[4] != kMarkerSchema ||
      bytes[5] < static_cast<uint8_t>(Phase::Prepared) ||
      bytes[5] > static_cast<uint8_t>(Phase::Verified) ||
      get32(bytes + 8) != crc32(bytes, 8)) return MarkerStatus::Error;
  phase = static_cast<Phase>(bytes[5]);
  return MarkerStatus::Pending;
}

bool removeIfPresent(const char* path) {
  return !SPIFFS.exists(path) || SPIFFS.remove(path);
}

bool writeFile(const char* path, const uint8_t* bytes, std::size_t size) {
  if ((size && !bytes) || !removeIfPresent(path)) return false;
  File file = SPIFFS.open(path, "w");
  if (!file) return false;
  const bool written = !size || file.write(bytes, size) == size;
  file.flush();
  file.close();
  return written;
}

bool fileEquals(const char* path, const uint8_t* bytes, std::size_t size) {
  if ((size && !bytes) || !SPIFFS.exists(path)) return false;
  File file = SPIFFS.open(path, "r");
  if (!file || file.size() != size) { file.close(); return false; }
  uint8_t buffer[256];
  std::size_t position = 0;
  while (position < size) {
    const std::size_t count = size - position < sizeof(buffer)
                                  ? size - position : sizeof(buffer);
    if (file.read(buffer, count) != count ||
        std::memcmp(buffer, bytes + position, count) != 0) {
      file.close();
      return false;
    }
    position += count;
  }
  file.close();
  return true;
}

bool filesEqual(const char* leftPath, const char* rightPath) {
  File left = SPIFFS.open(leftPath, "r");
  File right = SPIFFS.open(rightPath, "r");
  if (!left || !right || left.size() != right.size()) {
    left.close(); right.close(); return false;
  }
  uint8_t leftBytes[256];
  uint8_t rightBytes[256];
  while (left.available()) {
    const std::size_t remaining = left.size() - left.position();
    const std::size_t count = remaining < sizeof(leftBytes) ? remaining : sizeof(leftBytes);
    if (left.read(leftBytes, count) != count || right.read(rightBytes, count) != count ||
        std::memcmp(leftBytes, rightBytes, count) != 0) {
      left.close(); right.close(); return false;
    }
  }
  left.close(); right.close();
  return true;
}

bool copyFile(const char* source, const char* target) {
  File input = SPIFFS.open(source, "r");
  if (!input || !removeIfPresent(target)) { input.close(); return false; }
  File output = SPIFFS.open(target, "w");
  if (!output) { input.close(); return false; }
  uint8_t buffer[256];
  bool ok = true;
  while (input.available()) {
    const std::size_t count = input.read(buffer, sizeof(buffer));
    if (!count || output.write(buffer, count) != count) { ok = false; break; }
  }
  output.flush();
  output.close();
  input.close();
  return ok && filesEqual(source, target);
}

bool installFile(const char* staged, const char* active) {
  if (!SPIFFS.exists(staged) || !removeIfPresent(active)) return false;
  return SPIFFS.rename(staged, active);
}

template <typename T>
bool setBlob(nvs_handle_t handle, const char* key, const T& value) {
  return nvs_set_blob(handle, key, &value, sizeof(value)) == ESP_OK;
}

template <typename T>
bool getBlob(nvs_handle_t handle, const char* key, T& value) {
  std::size_t size = sizeof(value);
  return nvs_get_blob(handle, key, &value, &size) == ESP_OK &&
         size == sizeof(value);
}

class ProductionBackend : public Backend {
 public:
  MarkerStatus readMarker(Phase& phase) override { return readMarkerRecord(phase); }

  bool cleanupStale() override {
    bool files = removeIfPresent(kWifiOld) && removeIfPresent(kStationsOld) &&
                 removeIfPresent(kWifiNew) && removeIfPresent(kStationsNew) &&
                 removeIfPresent(kRestoreTemp);
    nvs_handle_t handle;
    const esp_err_t opened = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return files;
    if (opened != ESP_OK) return false;
    const bool nvs = nvs_erase_all(handle) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return files && nvs;
  }

  bool stageCandidate(const config_backup::Data& candidate) override {
    candidate_ = &candidate;
    return writeFile(kWifiNew, candidate.wifi.data, candidate.wifi.size) &&
           fileEquals(kWifiNew, candidate.wifi.data, candidate.wifi.size) &&
           writeFile(kStationsNew, candidate.stations.data, candidate.stations.size) &&
           fileEquals(kStationsNew, candidate.stations.data, candidate.stations.size);
  }

  bool createRollbackSnapshot() override {
    uint8_t flags = 0;
    uint8_t configBytes[config_format::kConfigV7SerializedSize]{};
    bool found = false;
    config_storage::ConfigEepromStorage configStore;
    if (!configStore.readRecord(configBytes, sizeof(configBytes), found)) return false;
    if (found) {
      config_format::config_v7_t checked{};
      if (config_format::parseConfigV7(configBytes, sizeof(configBytes), checked) !=
          config_format::ConfigRecordStatus::LOADED_V7) return false;
      flags |= ConfigPresent;
    }
    hardware::RuntimeHardwareConfig hardwareConfig{};
    if (!hardware::readRuntimeHardwarePersistent(hardwareConfig, found)) return false;
    if (found) {
      if (hardware::validateRuntimeHardwareConfig(
              hardwareConfig, hardware::hardwareCapabilities()) !=
          hardware::RuntimeHardwareValidation::Valid) return false;
      flags |= HardwarePresent;
    }
    MqttSettings mqtt{};
    if (!mqttReadPersistent(mqtt, found)) return false;
    if (found) flags |= MqttPresent;
    UiTimeoutSettings ui{};
    if (!uiTimeoutReadPersistent(ui, found)) return false;
    if (found) flags |= UiPresent;
    if (SPIFFS.exists(SSIDS_PATH)) {
      if (!copyFile(SSIDS_PATH, kWifiOld)) return false;
      flags |= WifiPresent;
    }
    if (SPIFFS.exists(PLAYLIST_PATH)) {
      if (!copyFile(PLAYLIST_PATH, kStationsOld)) return false;
      flags |= StationsPresent;
    }

    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    bool ok = nvs_erase_all(handle) == ESP_OK &&
              nvs_set_u8(handle, kFlagsKey, flags) == ESP_OK;
    if (ok && (flags & ConfigPresent))
      ok = nvs_set_blob(handle, kConfigKey, configBytes, sizeof(configBytes)) == ESP_OK;
    if (ok && (flags & HardwarePresent)) ok = setBlob(handle, kHardwareKey, hardwareConfig);
    if (ok && (flags & MqttPresent)) ok = setBlob(handle, kMqttKey, mqtt);
    if (ok && (flags & UiPresent)) ok = setBlob(handle, kUiKey, ui);
    ok = ok && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok && verifyStoredSnapshot(flags, configBytes, hardwareConfig, mqtt, ui);
  }

  bool writeMarker(Phase phase) override {
    uint8_t expected[kMarkerSize];
    encodeMarker(phase, expected);
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    const bool written = nvs_set_blob(handle, kMarkerKey, expected, sizeof(expected)) == ESP_OK &&
                         nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    Phase actual = Phase::Prepared;
    return written && readMarker(actual) == MarkerStatus::Pending && actual == phase;
  }

  bool apply(Component component) override {
    if (!candidate_) return false;
    switch (component) {
      case Component::Config: return writeConfig(candidate_->config);
      case Component::Hardware:
        return hardware::writeRuntimeHardwarePersistent(candidate_->hardware);
      case Component::Mqtt: return mqttWritePersistent(candidate_->mqtt);
      case Component::Ui: return uiTimeoutWritePersistent(candidate_->ui);
      case Component::Wifi: return installFile(kWifiNew, SSIDS_PATH);
      case Component::Stations: return installFile(kStationsNew, PLAYLIST_PATH);
    }
    return false;
  }

  bool rebuildStationsIndex() override {
    if (!removeIfPresent(INDEX_PATH) || !playlistStore.begin()) return false;
    return playlistStore.rebuildIndex();
  }

  bool verifyCandidate() override {
    if (!candidate_ || !verifyConfig(candidate_->config)) return false;
    hardware::RuntimeHardwareConfig hardwareConfig{};
    bool found = false;
    if (!hardware::readRuntimeHardwarePersistent(hardwareConfig, found) || !found ||
        hardwareConfig != candidate_->hardware) return false;
    MqttSettings mqtt{};
    if (!mqttReadPersistent(mqtt, found) || !found ||
        std::memcmp(&mqtt, &candidate_->mqtt, sizeof(mqtt)) != 0) return false;
    UiTimeoutSettings ui{};
    if (!uiTimeoutReadPersistent(ui, found) || !found ||
        std::memcmp(&ui, &candidate_->ui, sizeof(ui)) != 0) return false;
    return fileEquals(SSIDS_PATH, candidate_->wifi.data, candidate_->wifi.size) &&
           fileEquals(PLAYLIST_PATH, candidate_->stations.data, candidate_->stations.size);
  }

  bool restoreRollbackSnapshot() override {
    uint8_t flags = 0;
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK ||
        nvs_get_u8(handle, kFlagsKey, &flags) != ESP_OK) return false;
    uint8_t configBytes[config_format::kConfigV7SerializedSize]{};
    hardware::RuntimeHardwareConfig hardwareConfig{};
    MqttSettings mqtt{};
    UiTimeoutSettings ui{};
    bool read = true;
    if (flags & ConfigPresent) {
      std::size_t size = sizeof(configBytes);
      read = nvs_get_blob(handle, kConfigKey, configBytes, &size) == ESP_OK &&
             size == sizeof(configBytes);
    }
    if (read && (flags & HardwarePresent)) read = getBlob(handle, kHardwareKey, hardwareConfig);
    if (read && (flags & MqttPresent)) read = getBlob(handle, kMqttKey, mqtt);
    if (read && (flags & UiPresent)) read = getBlob(handle, kUiKey, ui);
    nvs_close(handle);
    if (!read) return false;

    config_storage::ConfigEepromStorage configStore;
    bool restored = (flags & ConfigPresent)
        ? configStore.writeRecord(configBytes, sizeof(configBytes)) && configStore.commit()
        : config_storage::eraseEepromRecord();
    restored = restored && ((flags & HardwarePresent)
        ? hardware::writeRuntimeHardwarePersistent(hardwareConfig)
        : hardware::eraseRuntimeHardwarePersistent());
    restored = restored && ((flags & MqttPresent)
        ? mqttWritePersistent(mqtt) : mqttErasePersistent());
    restored = restored && ((flags & UiPresent)
        ? uiTimeoutWritePersistent(ui) : uiTimeoutErasePersistent());
    if (!restored || !restoreFile(flags & WifiPresent, kWifiOld, SSIDS_PATH) ||
        !restoreFile(flags & StationsPresent, kStationsOld, PLAYLIST_PATH) ||
        !removeIfPresent(INDEX_PATH)) return false;
    if (flags & StationsPresent) return rebuildStationsIndex();
    return true;
  }

  bool verifyRollbackSnapshot() override {
    uint8_t flags = 0;
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK ||
        nvs_get_u8(handle, kFlagsKey, &flags) != ESP_OK) return false;
    uint8_t configBytes[config_format::kConfigV7SerializedSize]{};
    hardware::RuntimeHardwareConfig hardwareConfig{};
    MqttSettings mqtt{};
    UiTimeoutSettings ui{};
    bool read = true;
    if (flags & ConfigPresent) {
      std::size_t size = sizeof(configBytes);
      read = nvs_get_blob(handle, kConfigKey, configBytes, &size) == ESP_OK &&
             size == sizeof(configBytes);
    }
    if (read && (flags & HardwarePresent)) read = getBlob(handle, kHardwareKey, hardwareConfig);
    if (read && (flags & MqttPresent)) read = getBlob(handle, kMqttKey, mqtt);
    if (read && (flags & UiPresent)) read = getBlob(handle, kUiKey, ui);
    nvs_close(handle);
    if (!read || !verifyConfigPresence(flags & ConfigPresent, configBytes) ||
        !verifyHardwarePresence(flags & HardwarePresent, hardwareConfig) ||
        !verifyMqttPresence(flags & MqttPresent, mqtt) ||
        !verifyUiPresence(flags & UiPresent, ui)) return false;
    const bool wifi = flags & WifiPresent
        ? filesEqual(kWifiOld, SSIDS_PATH) : !SPIFFS.exists(SSIDS_PATH);
    const bool stations = flags & StationsPresent
        ? filesEqual(kStationsOld, PLAYLIST_PATH) : !SPIFFS.exists(PLAYLIST_PATH);
    return wifi && stations;
  }

  bool clearMarker() override {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t erased = nvs_erase_key(handle, kMarkerKey);
    const bool cleared = erased == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    Phase ignored = Phase::Prepared;
    return cleared && readMarker(ignored) == MarkerStatus::None;
  }

 private:
  const config_backup::Data* candidate_ = nullptr;

  bool writeConfig(const config_format::config_v7_t& configValue) {
    uint8_t bytes[config_format::kConfigV7SerializedSize]{};
    if (!config_format::serializeConfigV7(configValue, bytes, sizeof(bytes))) return false;
    config_storage::ConfigEepromStorage storage;
    return storage.writeRecord(bytes, sizeof(bytes)) && storage.commit() &&
           verifyConfig(configValue);
  }

  bool verifyConfig(const config_format::config_v7_t& expected) {
    uint8_t expectedBytes[config_format::kConfigV7SerializedSize]{};
    uint8_t actual[config_format::kConfigV7SerializedSize]{};
    config_storage::ConfigEepromStorage storage;
    return config_format::serializeConfigV7(expected, expectedBytes, sizeof(expectedBytes)) &&
           storage.readRecord(actual, sizeof(actual)) &&
           std::memcmp(expectedBytes, actual, sizeof(actual)) == 0;
  }

  bool verifyStoredSnapshot(uint8_t flags, const uint8_t* configBytes,
                            const hardware::RuntimeHardwareConfig& hardwareConfig,
                            const MqttSettings& mqtt, const UiTimeoutSettings& ui) {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
    uint8_t actualFlags = 0;
    bool ok = nvs_get_u8(handle, kFlagsKey, &actualFlags) == ESP_OK &&
              actualFlags == flags;
    uint8_t actualConfig[config_format::kConfigV7SerializedSize]{};
    if (ok && (flags & ConfigPresent)) {
      std::size_t size = sizeof(actualConfig);
      ok = nvs_get_blob(handle, kConfigKey, actualConfig, &size) == ESP_OK &&
           size == sizeof(actualConfig) &&
           std::memcmp(actualConfig, configBytes, sizeof(actualConfig)) == 0;
    }
    hardware::RuntimeHardwareConfig actualHardware{};
    MqttSettings actualMqtt{};
    UiTimeoutSettings actualUi{};
    if (ok && (flags & HardwarePresent))
      ok = getBlob(handle, kHardwareKey, actualHardware) && actualHardware == hardwareConfig;
    if (ok && (flags & MqttPresent))
      ok = getBlob(handle, kMqttKey, actualMqtt) &&
           std::memcmp(&actualMqtt, &mqtt, sizeof(mqtt)) == 0;
    if (ok && (flags & UiPresent))
      ok = getBlob(handle, kUiKey, actualUi) &&
           std::memcmp(&actualUi, &ui, sizeof(ui)) == 0;
    nvs_close(handle);
    if (!ok) return false;
    return (!(flags & WifiPresent) || filesEqual(SSIDS_PATH, kWifiOld)) &&
           (!(flags & StationsPresent) || filesEqual(PLAYLIST_PATH, kStationsOld));
  }

  bool restoreFile(bool present, const char* snapshot, const char* active) {
    if (!present) return removeIfPresent(active);
    if (!copyFile(snapshot, kRestoreTemp)) return false;
    if (!removeIfPresent(active)) return false;
    return SPIFFS.rename(kRestoreTemp, active);
  }

  bool verifyConfigPresence(bool present, const uint8_t* expected) {
    uint8_t actual[config_format::kConfigV7SerializedSize]{};
    bool found = false;
    config_storage::ConfigEepromStorage storage;
    return storage.readRecord(actual, sizeof(actual), found) && found == present &&
           (!present || std::memcmp(actual, expected, sizeof(actual)) == 0);
  }

  bool verifyHardwarePresence(bool present,
                              const hardware::RuntimeHardwareConfig& expected) {
    hardware::RuntimeHardwareConfig actual{};
    bool found = false;
    return hardware::readRuntimeHardwarePersistent(actual, found) && found == present &&
           (!present || actual == expected);
  }

  bool verifyMqttPresence(bool present, const MqttSettings& expected) {
    MqttSettings actual{};
    bool found = false;
    return mqttReadPersistent(actual, found) && found == present &&
           (!present || std::memcmp(&actual, &expected, sizeof(actual)) == 0);
  }

  bool verifyUiPresence(bool present, const UiTimeoutSettings& expected) {
    UiTimeoutSettings actual{};
    bool found = false;
    return uiTimeoutReadPersistent(actual, found) && found == present &&
           (!present || std::memcmp(&actual, &expected, sizeof(actual)) == 0);
  }
};

}  // namespace

BootRecoveryStatus recoverAtBoot() {
  Phase phase = Phase::Prepared;
  const MarkerStatus marker = readMarkerRecord(phase);
  if (marker == MarkerStatus::None) return BootRecoveryStatus::NotPending;
  if (marker == MarkerStatus::Error || !config_storage::beginEeprom() ||
      !SPIFFS.begin(false)) return BootRecoveryStatus::Failed;
  ProductionBackend backend;
  TransactionStorage transaction(backend);
  const bool recovered = transaction.recover();
  SPIFFS.end();
  config_storage::endEeprom();
  return recovered ? BootRecoveryStatus::Recovered : BootRecoveryStatus::Failed;
}

void cleanupStaleAfterStorageInit() {
  Phase phase = Phase::Prepared;
  if (readMarkerRecord(phase) != MarkerStatus::None) return;
  ProductionBackend backend;
  backend.cleanupStale();
}

config_backup::RestoreResult restoreProductionBackup(const uint8_t* input,
                                                     std::size_t size) {
  ProductionBackend backend;
  TransactionStorage transaction(backend);
  return config_backup::restore(input, size, hardware::hardwareCapabilities(),
                                transaction);
}

}  // namespace config_restore
}  // namespace voxone
