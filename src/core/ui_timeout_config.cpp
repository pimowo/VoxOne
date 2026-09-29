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

const UiTimeoutSettings& uiTimeoutConfig() {
  if (loaded) return currentSettings;
  loaded = true;
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return currentSettings;
  UiTimeoutRecord record{};
  size_t size = sizeof(record);
  const esp_err_t result = nvs_get_blob(handle, kKey, &record, &size);
  nvs_close(handle);
  if (result == ESP_OK && size == sizeof(record) && record.version == kSchemaVersion &&
      record.settings.stationListSeconds <= 120 && record.settings.btTransportSeconds <= 120)
    currentSettings = record.settings;
  return currentSettings;
}

bool uiTimeoutSave(const UiTimeoutSettings& settings) {
  if (settings.stationListSeconds > 120 || settings.btTransportSeconds > 120) return false;
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  const UiTimeoutRecord record{kSchemaVersion, settings};
  const bool saved = nvs_set_blob(handle, kKey, &record, sizeof(record)) == ESP_OK &&
                     nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  if (saved) {
    currentSettings = settings;
    loaded = true;
  }
  return saved;
}

bool uiTimeoutReset() {
  return uiTimeoutSave(UiTimeoutSettings{});
}
