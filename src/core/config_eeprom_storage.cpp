#include "config_eeprom_storage.h"

#include <EEPROM.h>
#include <nvs.h>

#include <cstring>

#include "config_format.h"

namespace voxone {
namespace config_storage {
namespace {
constexpr char kNamespace[] = "eeprom";
constexpr char kKey[] = "eeprom";
}

bool beginEeprom() { return EEPROM.begin(kEepromSize); }
void endEeprom() { EEPROM.end(); }

bool eraseEepromRecord() {
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
  if (opened != ESP_OK) return false;
  const esp_err_t erased = nvs_erase_key(handle, kKey);
  const bool result = (erased == ESP_OK || erased == ESP_ERR_NVS_NOT_FOUND) &&
                      nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return result;
}

bool ConfigEepromStorage::writeRecord(const uint8_t* bytes, std::size_t size) {
  if (!bytes || size != config_format::kConfigV7SerializedSize ||
      EEPROM.length() != kEepromSize) return false;
  for (std::size_t i = 0; i < size; ++i) EEPROM.write(kConfigOffset + i, bytes[i]);
  return true;
}

bool ConfigEepromStorage::commit() { return EEPROM.commit(); }

bool ConfigEepromStorage::readRecord(uint8_t* bytes, std::size_t size) {
  bool found = false;
  return readRecord(bytes, size, found) && found;
}

bool ConfigEepromStorage::readRecord(uint8_t* bytes, std::size_t size,
                                     bool& found) {
  found = false;
  if (!bytes || size != config_format::kConfigV7SerializedSize) return false;
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
  if (opened != ESP_OK) return false;
  uint8_t stored[kEepromSize];
  std::size_t length = sizeof(stored);
  const esp_err_t result = nvs_get_blob(handle, kKey, stored, &length);
  nvs_close(handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return true;
  if (result != ESP_OK || length != sizeof(stored)) return false;
  std::memcpy(bytes, stored + kConfigOffset, size);
  found = true;
  return true;
}

}  // namespace config_storage
}  // namespace voxone
