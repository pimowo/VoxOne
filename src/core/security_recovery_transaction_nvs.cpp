#include "security_recovery_transaction.h"

#include "security_api.h"
#include "update_progress.h"

#include <nvs.h>

#include <cstring>

namespace voxone {
namespace security_recovery {
namespace {

constexpr char kNamespace[] = "voxsecurity";
constexpr char kSettingsKey[] = "settings";
constexpr char kJournalKey[] = "recovery_jrnl";

void secureZero(void* memory, size_t size) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(memory);
  while (size--) *bytes++ = 0;
}

security::SecurityStorageReadStatus readBlob(const char* key, uint8_t* output,
                                             size_t capacity, size_t& size) {
  size = 0;
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND)
    return security::SecurityStorageReadStatus::Missing;
  if (opened != ESP_OK) return security::SecurityStorageReadStatus::Error;
  size_t storedSize = 0;
  esp_err_t result = nvs_get_blob(handle, key, nullptr, &storedSize);
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return security::SecurityStorageReadStatus::Missing;
  }
  if (result != ESP_OK) {
    nvs_close(handle);
    return security::SecurityStorageReadStatus::Error;
  }
  size = storedSize;
  if (!output || storedSize > capacity) {
    nvs_close(handle);
    return security::SecurityStorageReadStatus::Found;
  }
  result = nvs_get_blob(handle, key, output, &storedSize);
  nvs_close(handle);
  size = storedSize;
  return result == ESP_OK ? security::SecurityStorageReadStatus::Found
                          : security::SecurityStorageReadStatus::Error;
}

bool writeBlob(const char* key, const uint8_t* input, size_t size,
               size_t expectedSize) {
  if (!input || size != expectedSize) return false;
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  bool saved = nvs_set_blob(handle, key, input, size) == ESP_OK &&
               nvs_commit(handle) == ESP_OK;
  uint8_t verified[kRecoveryJournalRecordSize]{};
  size_t verifiedSize = size;
  saved = saved && size <= sizeof(verified) &&
          nvs_get_blob(handle, key, verified, &verifiedSize) == ESP_OK &&
          verifiedSize == size && std::memcmp(input, verified, size) == 0;
  secureZero(verified, sizeof(verified));
  nvs_close(handle);
  return saved;
}

class NvsRecoveryTransactionStorage : public RecoveryTransactionStorage {
 public:
  security::SecurityStorageReadStatus readSettings(
      uint8_t* output, size_t capacity, size_t& size) override {
    return readBlob(kSettingsKey, output, capacity, size);
  }

  bool writeSettings(const uint8_t* input, size_t size) override {
    return writeBlob(kSettingsKey, input, size,
                     security::kSecurityRecordSize);
  }

  security::SecurityStorageReadStatus readJournal(
      uint8_t* output, size_t capacity, size_t& size) override {
    return readBlob(kJournalKey, output, capacity, size);
  }

  bool writeJournal(const uint8_t* input, size_t size) override {
    return writeBlob(kJournalKey, input, size, kRecoveryJournalRecordSize);
  }

  bool eraseJournal() override {
    nvs_handle_t handle;
    const esp_err_t opened = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
    if (opened != ESP_OK) return false;
    const esp_err_t erased = nvs_erase_key(handle, kJournalKey);
    bool cleared = (erased == ESP_OK || erased == ESP_ERR_NVS_NOT_FOUND) &&
                   nvs_commit(handle) == ESP_OK;
    size_t remaining = 0;
    if (cleared) {
      const esp_err_t checked =
          nvs_get_blob(handle, kJournalKey, nullptr, &remaining);
      cleared = checked == ESP_ERR_NVS_NOT_FOUND;
    }
    nvs_close(handle);
    return cleared;
  }
};

NvsRecoveryTransactionStorage storage;
RecoveryTransaction transaction(storage);

}  // namespace

RecoveryJournalResolution resolveDeviceSecurityRecoveryJournal() {
  return resolveRecoveryJournal(storage);
}

RecoveryBaselineStatus inspectDeviceSecurityRecoveryBaseline() {
  if (resolveRecoveryJournal(storage) == RecoveryJournalResolution::Locked)
    return RecoveryBaselineStatus::JournalConflict;
  return inspectRecoveryBaseline(storage);
}

RecoveryTransactionResult commitDeviceSecurityRecovery(
    const PreparedRecoveryCredential& prepared) {
  if (!beginUpdate(UpdateTarget::SecurityRecovery, UpdatePhase::Preparing))
    return RecoveryTransactionResult::Busy;
  setUpdatePhase(UpdateTarget::SecurityRecovery, UpdatePhase::Writing);
  const RecoveryTransactionResult result = transaction.commit(prepared);
  if (result == RecoveryTransactionResult::Applied) {
    setUpdatePhase(UpdateTarget::SecurityRecovery, UpdatePhase::Verifying);
    security::deviceSecuritySessionService().invalidateAll();
    finishUpdate(UpdateTarget::SecurityRecovery, UpdatePhase::Success, 0,
                 "SECURITY recovery committed");
  } else {
    finishUpdate(UpdateTarget::SecurityRecovery, UpdatePhase::Error, 1,
                 recoveryTransactionResultName(result));
  }
  return result;
}

}  // namespace security_recovery
}  // namespace voxone
