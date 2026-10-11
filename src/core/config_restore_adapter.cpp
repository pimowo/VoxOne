#include "options.h"
#include "config_restore_adapter.h"

#include <EEPROM.h>
#include <SPIFFS.h>
#include <nvs.h>

#include <algorithm>
#include <cstring>
#include <new>

#include "config.h"
#include "config_eeprom_storage.h"
#include "config_backup_web.h"
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
constexpr char kWifiMetaKey[] = "wifimeta";
constexpr char kStationsMetaKey[] = "stameta";
constexpr char kWifiOld[] = "/data/.vxrestore.wifi.old";
constexpr char kStationsOld[] = "/data/.vxrestore.stations.old";
constexpr char kWifiNew[] = "/data/.vxrestore.wifi.new";
constexpr char kStationsNew[] = "/data/.vxrestore.stations.new";
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

struct FileFingerprint {
  uint32_t size;
  uint32_t crc;
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

bool fingerprintFile(const char* path, FileFingerprint& result) {
  File file = SPIFFS.open(path, "r");
  if (!file || file.size() > UINT32_MAX) { file.close(); return false; }
  config_backup::Crc32 crc;
  uint8_t buffer[512];
  while (file.available()) {
    const std::size_t count = file.read(buffer, sizeof(buffer));
    if (!count) { file.close(); return false; }
    crc.update(buffer, count);
  }
  result.size = static_cast<uint32_t>(file.size());
  result.crc = crc.value();
  file.close();
  return true;
}

bool fileMatches(const char* path, const FileFingerprint& expected) {
  FileFingerprint actual{};
  return fingerprintFile(path, actual) && actual.size == expected.size &&
         actual.crc == expected.crc;
}

class FileByteReader : public config_backup::ByteReader {
 public:
  FileByteReader(File& file, std::size_t size) : file_(file), size_(size) {}
  std::size_t size() const override { return size_; }
  bool read(std::size_t offset, uint8_t* output,
            std::size_t length) override {
    if ((length && !output) || offset > size_ || length > size_ - offset)
      return false;
    return !length || (file_ && file_.seek(offset) &&
                       file_.read(output, length) == length);
  }
 private:
  File& file_;
  std::size_t size_;
};

bool installTransactionalFile(const char* staged, const char* active,
                              const char* rollback) {
  if (!SPIFFS.exists(staged) || !removeIfPresent(rollback)) return false;
  if (SPIFFS.exists(active) && !SPIFFS.rename(active, rollback)) return false;
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
  explicit ProductionBackend(bool stagedFiles = false)
      : stagedFiles_(stagedFiles) {}
  MarkerStatus readMarker(Phase& phase) override { return readMarkerRecord(phase); }

  bool cleanupStale() override {
    bool files = removeIfPresent(kWifiOld) && removeIfPresent(kStationsOld) &&
                 ((stagedFiles_ && !candidate_) ||
                  (removeIfPresent(kWifiNew) &&
                   removeIfPresent(kStationsNew)));
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
    if (stagedFiles_) {
      return fingerprintFile(kWifiNew, candidateWifi_) &&
             fingerprintFile(kStationsNew, candidateStations_) &&
             candidateWifi_.size == candidate.wifi.size &&
             candidateStations_.size == candidate.stations.size;
    }
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
    FileFingerprint wifiMeta{}, stationsMeta{};
    if (SPIFFS.exists(SSIDS_PATH)) {
      if (!fingerprintFile(SSIDS_PATH, wifiMeta)) return false;
      flags |= WifiPresent;
    }
    if (SPIFFS.exists(PLAYLIST_PATH)) {
      if (!fingerprintFile(PLAYLIST_PATH, stationsMeta)) return false;
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
    if (ok && (flags & WifiPresent)) ok = setBlob(handle, kWifiMetaKey, wifiMeta);
    if (ok && (flags & StationsPresent))
      ok = setBlob(handle, kStationsMetaKey, stationsMeta);
    ok = ok && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok && verifyStoredSnapshot(flags, configBytes, hardwareConfig, mqtt, ui,
                                      wifiMeta, stationsMeta);
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
      case Component::Wifi:
        return installTransactionalFile(kWifiNew, SSIDS_PATH, kWifiOld);
      case Component::Stations:
        return installTransactionalFile(kStationsNew, PLAYLIST_PATH, kStationsOld);
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
    if (stagedFiles_)
      return fileMatches(SSIDS_PATH, candidateWifi_) &&
             fileMatches(PLAYLIST_PATH, candidateStations_);
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
    FileFingerprint wifiMeta{}, stationsMeta{};
    bool read = true;
    if (flags & ConfigPresent) {
      std::size_t size = sizeof(configBytes);
      read = nvs_get_blob(handle, kConfigKey, configBytes, &size) == ESP_OK &&
             size == sizeof(configBytes);
    }
    if (read && (flags & HardwarePresent)) read = getBlob(handle, kHardwareKey, hardwareConfig);
    if (read && (flags & MqttPresent)) read = getBlob(handle, kMqttKey, mqtt);
    if (read && (flags & UiPresent)) read = getBlob(handle, kUiKey, ui);
    if (read && (flags & WifiPresent))
      read = getBlob(handle, kWifiMetaKey, wifiMeta);
    if (read && (flags & StationsPresent))
      read = getBlob(handle, kStationsMetaKey, stationsMeta);
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
    if (!restored || !restoreTransactionalFile(flags & WifiPresent, kWifiOld,
                                               SSIDS_PATH, wifiMeta) ||
        !restoreTransactionalFile(flags & StationsPresent, kStationsOld,
                                  PLAYLIST_PATH, stationsMeta) ||
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
    FileFingerprint wifiMeta{}, stationsMeta{};
    bool read = true;
    if (flags & ConfigPresent) {
      std::size_t size = sizeof(configBytes);
      read = nvs_get_blob(handle, kConfigKey, configBytes, &size) == ESP_OK &&
             size == sizeof(configBytes);
    }
    if (read && (flags & HardwarePresent)) read = getBlob(handle, kHardwareKey, hardwareConfig);
    if (read && (flags & MqttPresent)) read = getBlob(handle, kMqttKey, mqtt);
    if (read && (flags & UiPresent)) read = getBlob(handle, kUiKey, ui);
    if (read && (flags & WifiPresent))
      read = getBlob(handle, kWifiMetaKey, wifiMeta);
    if (read && (flags & StationsPresent))
      read = getBlob(handle, kStationsMetaKey, stationsMeta);
    nvs_close(handle);
    if (!read || !verifyConfigPresence(flags & ConfigPresent, configBytes) ||
        !verifyHardwarePresence(flags & HardwarePresent, hardwareConfig) ||
        !verifyMqttPresence(flags & MqttPresent, mqtt) ||
        !verifyUiPresence(flags & UiPresent, ui)) return false;
    const bool wifi = flags & WifiPresent
        ? fileMatches(SSIDS_PATH, wifiMeta) : !SPIFFS.exists(SSIDS_PATH);
    const bool stations = flags & StationsPresent
        ? fileMatches(PLAYLIST_PATH, stationsMeta) : !SPIFFS.exists(PLAYLIST_PATH);
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
  bool stagedFiles_ = false;
  FileFingerprint candidateWifi_{};
  FileFingerprint candidateStations_{};

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
                            const MqttSettings& mqtt, const UiTimeoutSettings& ui,
                            const FileFingerprint& wifiMeta,
                            const FileFingerprint& stationsMeta) {
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
    FileFingerprint actualWifi{}, actualStations{};
    if (ok && (flags & WifiPresent))
      ok = getBlob(handle, kWifiMetaKey, actualWifi) &&
           std::memcmp(&actualWifi, &wifiMeta, sizeof(wifiMeta)) == 0;
    if (ok && (flags & StationsPresent))
      ok = getBlob(handle, kStationsMetaKey, actualStations) &&
           std::memcmp(&actualStations, &stationsMeta, sizeof(stationsMeta)) == 0;
    nvs_close(handle);
    if (!ok) return false;
    return (!(flags & WifiPresent) || fileMatches(SSIDS_PATH, wifiMeta)) &&
           (!(flags & StationsPresent) || fileMatches(PLAYLIST_PATH, stationsMeta));
  }

  bool restoreTransactionalFile(bool present, const char* rollback,
                                const char* active,
                                const FileFingerprint& expected) {
    if (!present) return removeIfPresent(active);
    if (SPIFFS.exists(rollback)) {
      // The durable marker makes the rollback file authoritative. Removing a
      // partial NEW file and renaming OLD is retryable after a power cut and
      // does not require a third full-size SPIFFS copy.
      if (!removeIfPresent(active) || !SPIFFS.rename(rollback, active))
        return false;
    }
    return fileMatches(active, expected);
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

struct ProductionBackupStream::Impl {
  config_backup::Data data{};
  File wifi;
  File stations;
  uint8_t prefix[config_backup::kBackupVariableOffset]{};
  uint8_t checksum[4]{};
  std::size_t total = 0;
  bool ready = false;
};

ProductionBackupStream::ProductionBackupStream()
    : impl_(new (std::nothrow) Impl) {
  static_assert(sizeof(Impl) <= 2048,
                "backup download transport must remain a small allocation");
}
ProductionBackupStream::~ProductionBackupStream() {
  if (impl_) {
    impl_->wifi.close();
    impl_->stations.close();
    delete impl_;
  }
}

bool ProductionBackupStream::begin(config_backup::Status& status) {
  status = config_backup::Status::StorageReadFailed;
  if (!impl_) { status = config_backup::Status::AllocationFailed; return false; }
  config_backup::Data& data = impl_->data;
  uint8_t configBytes[config_format::kConfigV7SerializedSize]{};
  bool found = false;
  config_storage::ConfigEepromStorage configStore;
  if (!configStore.readRecord(configBytes, sizeof(configBytes), found) || !found ||
      config_format::parseConfigV7(configBytes, sizeof(configBytes), data.config) !=
          config_format::ConfigRecordStatus::LOADED_V7)
    return false;

  if (!hardware::readRuntimeHardwarePersistent(data.hardware, found))
    return false;
  if (!found) data.hardware = hardware::runtimeHardwareConfig();
  if (!mqttReadPersistent(data.mqtt, found))
    return false;
  if (!found) data.mqtt = mqttConfig();
  if (!uiTimeoutReadPersistent(data.ui, found))
    return false;
  if (!found) data.ui = uiTimeoutConfig();

  if (!SPIFFS.exists(PLAYLIST_PATH))
    return false;
  impl_->wifi = SPIFFS.open(SSIDS_PATH, "r");
  impl_->stations = SPIFFS.open(PLAYLIST_PATH, "r");
  const std::size_t wifiSize = impl_->wifi ? impl_->wifi.size() : 0;
  const std::size_t stationsSize = impl_->stations ? impl_->stations.size() : 0;
  if (!impl_->stations) return false;
  data.wifi = {nullptr, wifiSize};
  data.stations = {nullptr, stationsSize};
  impl_->total = config_backup::serializedSize(data);
  if (!impl_->total) { status = config_backup::Status::InvalidLength; return false; }
  FileByteReader wifiReader(impl_->wifi, wifiSize);
  FileByteReader stationsReader(impl_->stations, stationsSize);
  status = config_backup::validateExternal(
      data, hardware::hardwareCapabilities(), wifiReader, stationsReader);
  if (status != config_backup::Status::Ok) return false;
  status = config_backup::encodePrefix(
      data, hardware::hardwareCapabilities(), impl_->prefix, sizeof(impl_->prefix));
  if (status != config_backup::Status::Ok) return false;
  config_backup::Crc32 crc;
  crc.update(impl_->prefix, sizeof(impl_->prefix));
  uint8_t buffer[1024];
  for (FileByteReader* reader : {&wifiReader, &stationsReader}) {
    for (std::size_t offset = 0; offset < reader->size();) {
      const std::size_t count = std::min(sizeof(buffer), reader->size() - offset);
      if (!reader->read(offset, buffer, count)) {
        status = config_backup::Status::StorageReadFailed;
        return false;
      }
      crc.update(buffer, count);
      offset += count;
    }
  }
  const uint32_t value = crc.value();
  for (unsigned i = 0; i < 4; ++i)
    impl_->checksum[i] = static_cast<uint8_t>(value >> (8 * i));
  impl_->ready = true;
  status = config_backup::Status::Ok;
  return true;
}

std::size_t ProductionBackupStream::size() const {
  return impl_ && impl_->ready ? impl_->total : 0;
}

std::size_t ProductionBackupStream::read(std::size_t offset, uint8_t* output,
                                         std::size_t capacity) {
  if (!impl_ || !impl_->ready || !output || offset >= impl_->total) return 0;
  const std::size_t wanted = std::min(capacity, impl_->total - offset);
  std::size_t copied = 0;
  bool success = true;
  auto copyMemory = [&](const uint8_t* source, std::size_t length) {
    if (!success || copied >= wanted) return;
    if (offset >= length) { offset -= length; return; }
    const std::size_t count = std::min(wanted - copied, length - offset);
    memcpy(output + copied, source + offset, count);
    copied += count;
    offset = 0;
  };
  auto copyFileRange = [&](File& file, std::size_t length) {
    if (!success || copied >= wanted) return;
    if (offset >= length) { offset -= length; return; }
    const std::size_t count = std::min(wanted - copied, length - offset);
    if (!file || !file.seek(offset) || file.read(output + copied, count) != count) {
      success = false;
      return;
    }
    copied += count;
    offset = 0;
  };
  copyMemory(impl_->prefix, sizeof(impl_->prefix));
  copyFileRange(impl_->wifi, impl_->data.wifi.size);
  copyFileRange(impl_->stations, impl_->data.stations.size);
  copyMemory(impl_->checksum, sizeof(impl_->checksum));
  return success ? copied : 0;
}

struct ProductionRestoreUpload::Impl {
  config_backup::Data candidate{};
  config_backup::Crc32 crc;
  File wifi;
  File stations;
  uint8_t prefix[config_backup::kBackupVariableOffset]{};
  uint8_t checksum[4]{};
  std::size_t total = 0;
  std::size_t received = 0;
  config_backup::Status status = config_backup::Status::InvalidArgument;
  bool decoded = false;
  bool finished = false;
};

ProductionRestoreUpload::ProductionRestoreUpload()
    : impl_(new (std::nothrow) Impl) {
  static_assert(sizeof(Impl) <= 2048,
                "restore upload transport must remain a small allocation");
}

ProductionRestoreUpload::~ProductionRestoreUpload() {
  abort();
  delete impl_;
}

config_backup::Status ProductionRestoreUpload::begin(std::size_t total) {
  using config_backup::Status;
  if (!impl_) return Status::AllocationFailed;
  if (total < config_backup::kBackupFixedOverhead || total > UINT32_MAX)
    return impl_->status = total ? Status::Truncated : Status::Empty;
  Phase phase = Phase::Prepared;
  const MarkerStatus marker = readMarkerRecord(phase);
  if (marker != MarkerStatus::None)
    return impl_->status = Status::RecoveryPending;
  if (!removeIfPresent(kWifiNew) || !removeIfPresent(kStationsNew))
    return impl_->status = Status::StorageWriteFailed;
  impl_->total = total;
  impl_->status = Status::Ok;
  return Status::Ok;
}

config_backup::Status ProductionRestoreUpload::append(
    std::size_t index, const uint8_t* data, std::size_t length) {
  using config_backup::Status;
  if (!impl_ || impl_->status != Status::Ok || impl_->finished)
    return impl_ ? impl_->status : Status::AllocationFailed;
  if ((length && !data) || index != impl_->received ||
      length > impl_->total - impl_->received)
    return impl_->status = Status::InvalidLength;

  const std::size_t checksumOffset = impl_->total - sizeof(impl_->checksum);
  if (index < checksumOffset) {
    const std::size_t crcLength =
        std::min(length, checksumOffset - index);
    impl_->crc.update(data, crcLength);
  }

  while (length) {
    const std::size_t position = impl_->received;
    if (position < sizeof(impl_->prefix)) {
      const std::size_t count =
          std::min(length, sizeof(impl_->prefix) - position);
      std::memcpy(impl_->prefix + position, data, count);
      impl_->received += count;
      data += count;
      length -= count;
      if (impl_->received == sizeof(impl_->prefix)) {
        impl_->status = config_backup::decodePrefix(
            impl_->prefix, sizeof(impl_->prefix), impl_->total,
            hardware::hardwareCapabilities(), impl_->candidate);
        if (impl_->status != Status::Ok) return impl_->status;

        // SPIFFS has no cross-file transaction. Both candidate files must fit
        // beside the active generation; one erase block is reserved for FS
        // metadata and rename bookkeeping.
        const std::size_t stagedBytes = impl_->candidate.wifi.size +
                                        impl_->candidate.stations.size;
        const std::size_t freeBytes = SPIFFS.totalBytes() - SPIFFS.usedBytes();
        if (stagedBytes > freeBytes ||
            freeBytes - stagedBytes <
                config_backup_web::kRestoreStagingReserveBytes)
          return impl_->status = Status::StorageWriteFailed;
        impl_->wifi = SPIFFS.open(kWifiNew, "w");
        impl_->stations = SPIFFS.open(kStationsNew, "w");
        if (!impl_->wifi || !impl_->stations)
          return impl_->status = Status::StorageWriteFailed;
        impl_->decoded = true;
      }
      continue;
    }

    const std::size_t wifiEnd = sizeof(impl_->prefix) +
                                impl_->candidate.wifi.size;
    const std::size_t stationsEnd = wifiEnd + impl_->candidate.stations.size;
    if (position < wifiEnd) {
      const std::size_t count = std::min(length, wifiEnd - position);
      if (impl_->wifi.write(data, count) != count)
        return impl_->status = Status::StorageWriteFailed;
      impl_->received += count;
      data += count;
      length -= count;
      continue;
    }
    if (position < stationsEnd) {
      const std::size_t count = std::min(length, stationsEnd - position);
      if (impl_->stations.write(data, count) != count)
        return impl_->status = Status::StorageWriteFailed;
      impl_->received += count;
      data += count;
      length -= count;
      continue;
    }
    if (position < impl_->total) {
      const std::size_t checksumIndex = position - stationsEnd;
      const std::size_t count =
          std::min(length, sizeof(impl_->checksum) - checksumIndex);
      std::memcpy(impl_->checksum + checksumIndex, data, count);
      impl_->received += count;
      data += count;
      length -= count;
      continue;
    }
    return impl_->status = Status::InvalidLength;
  }
  return Status::Ok;
}

config_backup::RestoreResult ProductionRestoreUpload::finish() {
  using config_backup::Status;
  if (!impl_) return {Status::AllocationFailed, false};
  if (impl_->status != Status::Ok) return {impl_->status, false};
  if (!impl_->decoded || impl_->received != impl_->total)
    return {Status::Truncated, false};
  impl_->wifi.flush();
  impl_->stations.flush();
  impl_->wifi.close();
  impl_->stations.close();
  if (get32(impl_->checksum) != impl_->crc.value()) {
    impl_->status = Status::InvalidChecksum;
    abort();
    return {Status::InvalidChecksum, false};
  }

  impl_->wifi = SPIFFS.open(kWifiNew, "r");
  impl_->stations = SPIFFS.open(kStationsNew, "r");
  if (!impl_->wifi || !impl_->stations) {
    impl_->status = Status::StorageReadFailed;
    abort();
    return {impl_->status, false};
  }
  FileByteReader wifiReader(impl_->wifi, impl_->candidate.wifi.size);
  FileByteReader stationsReader(impl_->stations, impl_->candidate.stations.size);
  impl_->status = config_backup::validateExternal(
      impl_->candidate, hardware::hardwareCapabilities(), wifiReader,
      stationsReader);
  impl_->wifi.close();
  impl_->stations.close();
  if (impl_->status != Status::Ok) {
    abort();
    return {impl_->status, false};
  }

  ProductionBackend backend(true);
  TransactionStorage transaction(backend);
  const config_backup::RestoreResult result =
      config_backup::restoreValidated(impl_->candidate, transaction);
  impl_->status = result.status;
  impl_->finished = true;
  return result;
}

void ProductionRestoreUpload::abort() {
  if (!impl_) return;
  impl_->wifi.close();
  impl_->stations.close();
  if (impl_->finished) return;
  Phase phase = Phase::Prepared;
  if (readMarkerRecord(phase) == MarkerStatus::None) {
    removeIfPresent(kWifiNew);
    removeIfPresent(kStationsNew);
  }
  impl_->finished = true;
}

bool ProductionRestoreUpload::complete() const {
  return impl_ && impl_->status == config_backup::Status::Ok &&
         impl_->received == impl_->total;
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
