#include "options.h"
#include "first_boot_transaction_runtime.h"

#include "ap_wifi_recovery.h"
#include "config.h"
#include "security_recovery_transaction.h"
#include "security_runtime.h"
#include "wifi_config_atomic.h"

#include <SPIFFS.h>
#include <nvs.h>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace voxone {
namespace first_boot_transaction {
namespace {

constexpr char kJournalNamespace[] = "voxfirstboot";
constexpr char kJournalKey[] = "transaction";
constexpr char kSecurityNamespace[] = "voxsecurity";
constexpr char kSecurityKey[] = "settings";

std::atomic<bool> transactionBlocked{false};

RecordStatus readNvsBlob(const char* nameSpace, const char* key,
                         uint8_t* output, size_t capacity, size_t& size) {
  size = 0;
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(nameSpace, NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return RecordStatus::Missing;
  if (opened != ESP_OK) return RecordStatus::Error;
  size_t storedSize = 0;
  esp_err_t result = nvs_get_blob(handle, key, nullptr, &storedSize);
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return RecordStatus::Missing;
  }
  if (result != ESP_OK) {
    nvs_close(handle);
    return RecordStatus::Error;
  }
  size = storedSize;
  if (!output || storedSize > capacity) {
    nvs_close(handle);
    return RecordStatus::Found;
  }
  result = nvs_get_blob(handle, key, output, &storedSize);
  nvs_close(handle);
  size = storedSize;
  return result == ESP_OK ? RecordStatus::Found : RecordStatus::Error;
}

bool writeNvsBlob(const char* nameSpace, const char* key,
                  const uint8_t* input, size_t size) {
  if (!input || !size) return false;
  nvs_handle_t handle;
  if (nvs_open(nameSpace, NVS_READWRITE, &handle) != ESP_OK) return false;
  bool saved = nvs_set_blob(handle, key, input, size) == ESP_OK &&
               nvs_commit(handle) == ESP_OK;
  uint8_t verified[kJournalRecordSize]{};
  size_t verifiedSize = size;
  saved = saved && size <= sizeof(verified) &&
          nvs_get_blob(handle, key, verified, &verifiedSize) == ESP_OK &&
          verifiedSize == size && std::memcmp(input, verified, size) == 0;
  nvs_close(handle);
  return saved;
}

bool eraseNvsBlob(const char* nameSpace, const char* key) {
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(nameSpace, NVS_READWRITE, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
  if (opened != ESP_OK) return false;
  const esp_err_t erased = nvs_erase_key(handle, key);
  bool cleared = (erased == ESP_OK || erased == ESP_ERR_NVS_NOT_FOUND) &&
                 nvs_commit(handle) == ESP_OK;
  size_t remaining = 0;
  if (cleared)
    cleared = nvs_get_blob(handle, key, nullptr, &remaining) ==
              ESP_ERR_NVS_NOT_FOUND;
  nvs_close(handle);
  return cleared;
}

class WifiSpiffsStorage {
 public:
  bool exists(const char* path) const { return SPIFFS.exists(path); }
  bool remove(const char* path) { return SPIFFS.remove(path); }
  bool rename(const char* from, const char* to) {
    return SPIFFS.rename(from, to);
  }
  bool write(const char* path, const uint8_t* bytes, size_t size) {
    File file = SPIFFS.open(path, "w");
    if (!file) return false;
    const bool written = !size || file.write(bytes, size) == size;
    file.flush();
    file.close();
    return written;
  }
  bool matches(const char* path, const uint8_t* bytes, size_t size) const {
    File file = SPIFFS.open(path, "r");
    if (!file || file.isDirectory() || file.size() != size) {
      file.close();
      return false;
    }
    uint8_t buffer[64]{};
    size_t offset = 0;
    while (offset < size) {
      const size_t count = std::min(sizeof(buffer), size - offset);
      if (file.read(buffer, count) != count ||
          std::memcmp(buffer, bytes + offset, count) != 0) {
        file.close();
        return false;
      }
      offset += count;
    }
    file.close();
    return true;
  }
  bool validBytes(const uint8_t* bytes, size_t size) const {
    return wifi_config::validSerializedConfig(bytes, size);
  }
  bool valid(const char* path) const {
    File file = SPIFFS.open(path, "r");
    if (!file || file.isDirectory() ||
        file.size() > wifi_config::kMaxFileBytes) {
      file.close();
      return false;
    }
    uint8_t bytes[wifi_config::kMaxFileBytes]{};
    const size_t size = file.size();
    const bool read = !size || file.read(bytes, size) == size;
    file.close();
    return read && validBytes(bytes, size);
  }
  bool matchesFingerprint(const char* path, size_t expectedSize,
                          uint32_t expectedCrc) const {
    File file = SPIFFS.open(path, "r");
    if (!file || file.isDirectory() || file.size() != expectedSize ||
        expectedSize > wifi_config::kMaxFileBytes) {
      file.close();
      return false;
    }
    uint8_t bytes[wifi_config::kMaxFileBytes]{};
    const bool read = !expectedSize ||
                      file.read(bytes, expectedSize) == expectedSize;
    file.close();
    return read && wifi_config::validSerializedConfig(bytes, expectedSize) &&
           wifi_config_atomic::recordCrc32(bytes, expectedSize) == expectedCrc;
  }
};

bool removeIfPresent(const char* path) {
  return !SPIFFS.exists(path) || SPIFFS.remove(path);
}

class DeviceBackend : public Backend {
 public:
  RecordStatus readJournal(uint8_t* output, size_t capacity,
                           size_t& size) override {
    return readNvsBlob(kJournalNamespace, kJournalKey, output, capacity, size);
  }
  bool writeJournal(const uint8_t* input, size_t size) override {
    return size == kJournalRecordSize &&
           writeNvsBlob(kJournalNamespace, kJournalKey, input, size);
  }
  bool eraseJournal() override {
    return eraseNvsBlob(kJournalNamespace, kJournalKey);
  }

  RecordStatus readHardware(uint8_t* output, size_t capacity,
                            size_t& size) override {
    size = 0;
    hardware::RuntimeHardwareConfig config{};
    bool found = false;
    if (!hardware::readRuntimeHardwarePersistent(config, found))
      return RecordStatus::Error;
    if (!found) return RecordStatus::Missing;
    size = hardware::kRuntimeHardwareConfigSerializedSize;
    if (!output || capacity < size) return RecordStatus::Found;
    return hardware::serializeRuntimeHardwareConfig(config, output, capacity)
               ? RecordStatus::Found
               : RecordStatus::Error;
  }
  bool writeHardware(const uint8_t* input, size_t size) override {
    hardware::RuntimeHardwareConfig config{};
    return hardware::deserializeRuntimeHardwareConfig(input, size, config) ==
               hardware::RuntimeHardwareDecodeStatus::Valid &&
           hardware::writeRuntimeHardwarePersistent(config);
  }
  bool eraseHardware() override {
    return hardware::eraseRuntimeHardwarePersistent();
  }

  RecordStatus readWifi(uint8_t* output, size_t capacity,
                        size_t& size) override {
    size = 0;
    if (!SPIFFS.exists(wifi_config_atomic::kActivePath))
      return RecordStatus::Missing;
    File file = SPIFFS.open(wifi_config_atomic::kActivePath, "r");
    if (!file || file.isDirectory()) {
      file.close();
      return RecordStatus::Error;
    }
    size = file.size();
    if (size > wifi_config::kMaxFileBytes || !output || size > capacity) {
      file.close();
      return size > wifi_config::kMaxFileBytes ? RecordStatus::Error
                                                : RecordStatus::Found;
    }
    const bool read = !size || file.read(output, size) == size;
    file.close();
    return read && wifi_config::validSerializedConfig(output, size)
               ? RecordStatus::Found
               : RecordStatus::Error;
  }
  bool writeWifi(const uint8_t* input, size_t size) override {
    WifiSpiffsStorage storage;
    return wifi_config_atomic::replace(storage, input, size);
  }
  bool eraseWifi() override {
    return removeIfPresent(wifi_config_atomic::kTemporaryPath) &&
           removeIfPresent(wifi_config_atomic::kRollbackPath) &&
           removeIfPresent(wifi_config_atomic::kActivePath);
  }
  bool rollbackWifi(bool priorEmpty, size_t candidateSize,
                    uint32_t candidateCrc) override {
    WifiSpiffsStorage storage;
    return wifi_config_atomic::rollbackFirstBoot(
        storage, priorEmpty, candidateSize, candidateCrc);
  }

  RecordStatus readSecurity(uint8_t* output, size_t capacity,
                            size_t& size) override {
    if (security_recovery::resolveDeviceSecurityRecoveryJournal() ==
        security_recovery::RecoveryJournalResolution::Locked) {
      size = 0;
      return RecordStatus::Error;
    }
    return readNvsBlob(kSecurityNamespace, kSecurityKey, output, capacity,
                       size);
  }
  bool completeSecurityOpen(const uint8_t* expected, size_t size) override {
    if (!expected || size != security::kSecurityRecordSize) return false;
    return security::deviceSecurityService().completeWifiProvisioningOpen() ==
           security::SecurityOperationStatus::Applied;
  }
  bool eraseSecurity() override {
    return eraseNvsBlob(kSecurityNamespace, kSecurityKey);
  }
};

bool buildWifiRecord(const char* ssid, const char* password,
                     uint8_t output[wifi_config::kMaxRecordBytes],
                     size_t& size) {
  size = 0;
  if (!ssid || !password || !apWifiCredentialsValid(ssid, password))
    return false;
  const size_t ssidLength = std::strlen(ssid);
  const size_t passwordLength = std::strlen(password);
  size = ssidLength + 1U + passwordLength + 1U;
  if (size > wifi_config::kMaxRecordBytes) return false;
  std::memcpy(output, ssid, ssidLength);
  output[ssidLength] = '\t';
  std::memcpy(output + ssidLength + 1U, password, passwordLength);
  output[size - 1U] = '\n';
  return wifi_config::validSerializedConfig(output, size);
}

bool buildOpenSecurityRecord(uint8_t output[security::kSecurityRecordSize]) {
  security::SecuritySettings settings{};
  settings.state = security::SecurityState::Open;
  settings.generation = 1U;
  return security::serializeSecuritySettings(settings, output,
                                             security::kSecurityRecordSize);
}

bool synchronizeRuntime(bool completed) {
  const hardware::RuntimeHardwareLoadStatus hardwareStatus =
      hardware::loadRuntimeHardwareConfig();
  if (hardwareStatus == hardware::RuntimeHardwareLoadStatus::DefaultStorageError)
    return false;
  if (!config.initNetwork()) return false;
  if (completed != (config.ssidsCount > 0U)) return false;
  const security::SecurityLoadStatus securityStatus =
      security::deviceSecurityService().begin(completed);
  return completed
             ? securityStatus == security::SecurityLoadStatus::Loaded &&
                   security::deviceSecurityService().state() ==
                       security::SecurityState::Open
             : securityStatus == security::SecurityLoadStatus::Unconfigured &&
                   security::deviceSecurityService().state() ==
                       security::SecurityState::Unconfigured;
}

}  // namespace

Result commitDeviceFirstBootTransaction(
    const hardware::RuntimeHardwareConfig& requestedHardware,
    const char* ssid, const char* password, bool securityLockHeld) {
  if (!securityLockHeld) return Result::SecurityUnavailable;
  if (security::deviceSecurityService().state() !=
          security::SecurityState::Unconfigured ||
      security::deviceSecurityService().generation() != 0U)
    return Result::BaselineConflict;

  Candidate candidate{};
  candidate.writeHardware =
      requestedHardware != hardware::runtimeHardwareConfig();
  candidate.hardwareSize = hardware::kRuntimeHardwareConfigSerializedSize;
  if (!hardware::serializeRuntimeHardwareConfig(
          requestedHardware, candidate.hardware, candidate.hardwareSize) ||
      !buildWifiRecord(ssid, password, candidate.wifi, candidate.wifiSize) ||
      !buildOpenSecurityRecord(candidate.security))
    return Result::InvalidCandidate;
  candidate.securitySize = security::kSecurityRecordSize;

  DeviceBackend backend;
  Transaction transaction(backend);
  const Result result = transaction.commit(candidate, true);
  if (result == Result::RollbackFailed) {
    transactionBlocked.store(true);
    Serial.printf("##[ERROR]# FIRST BOOT rollback failed cause=%s\n",
                  resultName(transaction.failureCause()));
    return result;
  }
  const bool completed = result == Result::Applied;
  if (!synchronizeRuntime(completed)) {
    transactionBlocked.store(true);
    Serial.printf("##[ERROR]# FIRST BOOT runtime synchronization failed "
                  "completed=%u\n", completed ? 1U : 0U);
    return Result::RollbackFailed;
  }
  return result;
}

RecoveryStatus recoverDeviceFirstBootTransactionAtBoot() {
  DeviceBackend backend;
  uint8_t journal[kJournalRecordSize]{};
  size_t journalSize = 0;
  const RecordStatus status =
      backend.readJournal(journal, sizeof(journal), journalSize);
  if (status == RecordStatus::Missing) return RecoveryStatus::Clean;
  if (status == RecordStatus::Error || !SPIFFS.begin(false)) {
    transactionBlocked.store(true);
    return RecoveryStatus::Failed;
  }

  Transaction transaction(backend);
  const RecoveryStatus recovered = transaction.recover();
  if (recovered == RecoveryStatus::Failed) {
    transactionBlocked.store(true);
    return recovered;
  }
  // Startup calls this before runtime hardware, Config and any AP/network
  // decision. Those subsystems therefore initialize once from the restored
  // persistent baseline; no pre-crash RAM state is carried across the boot.
  transactionBlocked.store(false);
  return recovered;
}

bool deviceFirstBootTransactionBlocked() {
  return transactionBlocked.load();
}

}  // namespace first_boot_transaction
}  // namespace voxone
