#include "display_scroll_persistence.h"

#include <nvs.h>

#include <string.h>

namespace voxone {
namespace display_scroll {
namespace {

constexpr char kNamespace[] = "voxscroll";
constexpr char kRecordKey[] = "settings";

class NvsScrollSettingsStorage : public ScrollSettingsStorage {
 public:
  ScrollSettingsStorageReadStatus read(uint8_t* output, size_t capacity,
                                       size_t& size) override {
    size = 0;
    nvs_handle_t handle;
    const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND)
      return ScrollSettingsStorageReadStatus::Missing;
    if (opened != ESP_OK) return ScrollSettingsStorageReadStatus::OpenError;

    size_t storedSize = 0;
    esp_err_t result = nvs_get_blob(handle, kRecordKey, nullptr, &storedSize);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
      nvs_close(handle);
      return ScrollSettingsStorageReadStatus::Missing;
    }
    if (result != ESP_OK) {
      nvs_close(handle);
      return ScrollSettingsStorageReadStatus::ReadError;
    }
    size = storedSize;
    if (storedSize > capacity) {
      nvs_close(handle);
      return ScrollSettingsStorageReadStatus::Found;
    }
    if (storedSize && !output) {
      nvs_close(handle);
      return ScrollSettingsStorageReadStatus::ReadError;
    }
    result = nvs_get_blob(handle, kRecordKey, output, &storedSize);
    nvs_close(handle);
    size = storedSize;
    return result == ESP_OK ? ScrollSettingsStorageReadStatus::Found
                            : ScrollSettingsStorageReadStatus::ReadError;
  }

  ScrollSettingsStorageWriteStatus write(const uint8_t* input,
                                          size_t size) override {
    if (!input || size != kScrollSettingsRecordSize)
      return ScrollSettingsStorageWriteStatus::WriteError;
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK)
      return ScrollSettingsStorageWriteStatus::OpenError;
    if (nvs_set_blob(handle, kRecordKey, input, size) != ESP_OK) {
      nvs_close(handle);
      return ScrollSettingsStorageWriteStatus::WriteError;
    }
    if (nvs_commit(handle) != ESP_OK) {
      nvs_close(handle);
      return ScrollSettingsStorageWriteStatus::CommitError;
    }

    uint8_t verified[kScrollSettingsRecordSize]{};
    size_t verifiedSize = sizeof(verified);
    const bool verifiedOk =
        nvs_get_blob(handle, kRecordKey, verified, &verifiedSize) == ESP_OK &&
        verifiedSize == size && memcmp(input, verified, size) == 0;
    nvs_close(handle);
    return verifiedOk ? ScrollSettingsStorageWriteStatus::Stored
                      : ScrollSettingsStorageWriteStatus::VerifyError;
  }
};

}  // namespace

ScrollSettingsLoadResult readScrollSettingsPersistent() {
  NvsScrollSettingsStorage storage;
  return loadScrollSettings(storage);
}

ScrollSettingsSaveStatus writeScrollSettingsPersistent(
    const ScrollSettingsLoadResult& current,
    const ScrollSettings& requested) {
  NvsScrollSettingsStorage storage;
  return saveScrollSettings(storage, current, requested);
}

}  // namespace display_scroll
}  // namespace voxone
