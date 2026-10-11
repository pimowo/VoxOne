#include "ui_timeout_config.h"
#include <nvs.h>

namespace {
constexpr char kNamespace[] = "voxui";
constexpr char kKey[] = "timeouts";
constexpr uint8_t kSchemaVersion = 1;

struct UiTimeoutRecord {
  uint8_t version;
  UiTimeoutSettings settings;
};

UiTimeoutSettings currentSettings{};
bool loaded = false;
}

bool uiTimeoutReadPersistent(UiTimeoutSettings& settings, bool& found) {
  found = false;
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
  if (opened != ESP_OK) return false;
  UiTimeoutRecord record{};
  size_t size = sizeof(record);
  const esp_err_t result = nvs_get_blob(handle, kKey, &record, &size);
  nvs_close(handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return true;
  if (result != ESP_OK || size != sizeof(record) || record.version != kSchemaVersion ||
      record.settings.stationListSeconds > 120 ||
      record.settings.btTransportSeconds > 120) return false;
  settings = record.settings;
  found = true;
  return true;
}

bool uiTimeoutWritePersistent(const UiTimeoutSettings& settings) {
  if (settings.stationListSeconds > 120 || settings.btTransportSeconds > 120) return false;
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  const UiTimeoutRecord record{kSchemaVersion, settings};
  const bool saved = nvs_set_blob(handle, kKey, &record, sizeof(record)) == ESP_OK &&
                     nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return saved;
}

bool uiTimeoutErasePersistent() {
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
  if (opened != ESP_OK) return false;
  const esp_err_t erased = nvs_erase_key(handle, kKey);
  const bool saved = (erased == ESP_OK || erased == ESP_ERR_NVS_NOT_FOUND) &&
                     nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return saved;
}

const UiTimeoutSettings& uiTimeoutConfig() {
  if (loaded) return currentSettings;
  loaded = true;
  UiTimeoutSettings persistent{};
  bool found = false;
  if (uiTimeoutReadPersistent(persistent, found) && found) currentSettings = persistent;
  return currentSettings;
}

bool uiTimeoutSave(const UiTimeoutSettings& settings) {
  const bool saved = uiTimeoutWritePersistent(settings);
  if (saved) {
    currentSettings = settings;
    loaded = true;
  }
  return saved;
}

bool uiTimeoutReset() {
  return uiTimeoutSave(UiTimeoutSettings{});
}
