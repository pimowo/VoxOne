#include "mqtt_config.h"
#include <esp_mac.h>
#include <nvs.h>

namespace {
constexpr char kNamespace[] = "voxmqtt";
constexpr char kRecordKey[] = "config";
constexpr uint8_t kSchemaVersion = 1;
struct MqttRecord {
  uint8_t version;
  MqttSettings settings;
};

MqttSettings currentSettings = mqttDefaultSettings();
bool loaded = false;
}

const MqttSettings& mqttConfig() {
  if (loaded) return currentSettings;
  loaded = true;
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return currentSettings;
  MqttRecord record{};
  size_t size = sizeof(record);
  const esp_err_t result = nvs_get_blob(handle, kRecordKey, &record, &size);
  nvs_close(handle);
  if (result == ESP_OK && size == sizeof(record) && record.version == kSchemaVersion &&
      mqttValidSettings(record.settings)) currentSettings = record.settings;
  return currentSettings;
}

bool mqttSaveConfig(const MqttSettings& settings) {
  if (!mqttValidSettings(settings)) return false;
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  MqttRecord record{};
  record.version = kSchemaVersion;
  record.settings = settings;
  const bool success = nvs_set_blob(handle, kRecordKey, &record, sizeof(record)) == ESP_OK &&
                       nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return success;
}

bool mqttClearConfig() {
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (opened != ESP_OK) return false;
  const bool success = nvs_erase_all(handle) == ESP_OK && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  if (success) currentSettings = mqttDefaultSettings();
  return success;
}

bool mqttAutoRoot(char output[14]) {
  uint8_t mac[6];
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return false;
  mqttAutoRootFromMac(mac, output);
  return true;
}

bool mqttEffectiveRoot(char* output, size_t capacity) {
  const MqttSettings& settings = mqttConfig();
  if (settings.rootTopic[0]) {
    const size_t length = std::strlen(settings.rootTopic);
    if (length >= capacity) return false;
    std::memcpy(output, settings.rootTopic, length + 1);
    return true;
  }
  if (capacity < 14) return false;
  return mqttAutoRoot(output);
}
