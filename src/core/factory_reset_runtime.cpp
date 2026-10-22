#include "factory_reset.h"

#include <SPIFFS.h>
#include <nvs.h>

namespace voxone { namespace factory_reset { namespace {

constexpr char kMarkerNamespace[] = "voxfactory";
constexpr char kMarkerKey[] = "state";
constexpr char kUserNamespaces[][16] = {
  "eeprom", "nvs.net80211", "voxhw", "voxmqtt", "voxui", "voxscroll",
  "voxsecurity", "voxrestore", "voxfirstboot", "voxupdate",
};
constexpr char kUserFiles[][40] = {
  "/data/wifi.csv", "/data/wifi.csv.tmp", "/data/wifi.csv.old",
  "/data/stations.tsv", "/data/stations.idx", "/data/playlist.csv",
  "/data/index.dat", "/data/stations.tmp", "/data/stations.bak",
  "/data/stations.idx.tmp", "/data/stations.idx.bak", "/data/stations.upload",
  "/data/.vxrestore.wifi.old", "/data/.vxrestore.stations.old",
  "/data/.vxrestore.wifi.new", "/data/.vxrestore.stations.new",
};

bool eraseNamespace(const char* name) {
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(name, NVS_READWRITE, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
  if (opened != ESP_OK) return false;
  const bool ok = nvs_erase_all(handle) == ESP_OK && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

bool namespaceEmpty(const char* name) {
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(name, NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
  if (opened != ESP_OK) return false;
  nvs_iterator_t iterator =
      nvs_entry_find(NVS_DEFAULT_PART_NAME, name, NVS_TYPE_ANY);
  const bool empty = iterator == nullptr;
  if (iterator) nvs_release_iterator(iterator);
  nvs_close(handle);
  return empty;
}

class DeviceBackend : public Backend {
 public:
  bool writeMarker(Marker marker) override {
    nvs_handle_t handle;
    if (nvs_open(kMarkerNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    const bool ok = nvs_set_u8(handle, kMarkerKey, static_cast<uint8_t>(marker)) == ESP_OK &&
                    nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
  }
  Marker readMarker() override {
    nvs_handle_t handle;
    const esp_err_t opened = nvs_open(kMarkerNamespace, NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return Marker::Missing;
    if (opened != ESP_OK) return Marker::Invalid;
    uint8_t value = 0;
    const esp_err_t read = nvs_get_u8(handle, kMarkerKey, &value);
    nvs_close(handle);
    if (read == ESP_ERR_NVS_NOT_FOUND) return Marker::Missing;
    if (read != ESP_OK || value < static_cast<uint8_t>(Marker::Erasing) ||
        value > static_cast<uint8_t>(Marker::FirstBootPending)) return Marker::Invalid;
    return static_cast<Marker>(value);
  }
  bool clearMarker() override {
    nvs_handle_t handle;
    const esp_err_t opened = nvs_open(kMarkerNamespace, NVS_READWRITE, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
    if (opened != ESP_OK) return false;
    const esp_err_t erased = nvs_erase_key(handle, kMarkerKey);
    const bool ok = (erased == ESP_OK || erased == ESP_ERR_NVS_NOT_FOUND) &&
                    nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
  }
  size_t nvsNamespaceCount() const override { return sizeof(kUserNamespaces) / sizeof(kUserNamespaces[0]); }
  bool eraseNvsNamespace(size_t index) override { return index < nvsNamespaceCount() && eraseNamespace(kUserNamespaces[index]); }
  size_t userFileCount() const override { return sizeof(kUserFiles) / sizeof(kUserFiles[0]); }
  bool eraseUserFile(size_t index) override {
    return index < userFileCount() && (!SPIFFS.exists(kUserFiles[index]) || SPIFFS.remove(kUserFiles[index]));
  }
  bool userDataCleared() override {
    for (const auto& name : kUserNamespaces) if (!namespaceEmpty(name)) return false;
    for (const auto& path : kUserFiles) if (SPIFFS.exists(path)) return false;
    return SPIFFS.exists("/www/assets.vxwm");
  }
};

DeviceBackend backend;
}  // namespace

Result performDeviceFactoryReset() {
  if (!SPIFFS.begin(false)) return Result::FileEraseFailed;
  return perform(backend);
}

bool consumeDeviceFactoryFirstBootBypass() { return consumeFirstBootBypass(backend); }

} }
