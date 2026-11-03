#include "config_capability_migration.h"

#include <nvs.h>

namespace voxone {
namespace config_capability_migration {
namespace {
constexpr char kNamespace[] = "voxcap";
constexpr char kS0BtKey[] = "s0bt1";
constexpr uint8_t kAppliedValue = 1;
}

MarkerStatus readS0BtIntroductionMarker() {
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return MarkerStatus::Missing;
  if (opened != ESP_OK) return MarkerStatus::Unavailable;
  uint8_t value = 0;
  const esp_err_t read = nvs_get_u8(handle, kS0BtKey, &value);
  nvs_close(handle);
  if (read == ESP_ERR_NVS_NOT_FOUND) return MarkerStatus::Missing;
  if (read != ESP_OK) return MarkerStatus::Unavailable;
  return value == kAppliedValue ? MarkerStatus::Applied
                                : MarkerStatus::Invalid;
}

bool writeS0BtIntroductionMarker() {
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  const bool written = nvs_set_u8(handle, kS0BtKey, kAppliedValue) == ESP_OK &&
                       nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return written;
}

}  // namespace config_capability_migration
}  // namespace voxone
