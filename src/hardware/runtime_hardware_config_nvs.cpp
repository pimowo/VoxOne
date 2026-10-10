#include "runtime_hardware_config.h"

#include <nvs.h>

namespace voxone {
namespace hardware {
namespace {
constexpr char kNamespace[] = "voxhw";
constexpr char kRecordKey[] = "selection";

class NvsRuntimeHardwareStorage : public RuntimeHardwareStorage {
 public:
  RuntimeHardwareStorageReadStatus read(uint8_t* output, size_t capacity,
                                        size_t& size) override {
    size = 0;
    nvs_handle_t handle;
    const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND)
      return RuntimeHardwareStorageReadStatus::Missing;
    if (opened != ESP_OK) return RuntimeHardwareStorageReadStatus::Error;

    size_t storedSize = 0;
    esp_err_t result = nvs_get_blob(handle, kRecordKey, nullptr, &storedSize);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
      nvs_close(handle);
      return RuntimeHardwareStorageReadStatus::Missing;
    }
    if (result != ESP_OK) {
      nvs_close(handle);
      return RuntimeHardwareStorageReadStatus::Error;
    }
    size = storedSize;
    if (storedSize > capacity) {
      nvs_close(handle);
      return RuntimeHardwareStorageReadStatus::Found;
    }
    result = nvs_get_blob(handle, kRecordKey, output, &storedSize);
    nvs_close(handle);
    size = storedSize;
    return result == ESP_OK ? RuntimeHardwareStorageReadStatus::Found
                            : RuntimeHardwareStorageReadStatus::Error;
  }

  bool write(const uint8_t* input, size_t size) override {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    const bool saved =
        nvs_set_blob(handle, kRecordKey, input, size) == ESP_OK &&
        nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return saved;
  }
};

RuntimeHardwareConfig currentConfig = defaultRuntimeHardwareConfig();
RuntimeHardwareDiagnostics diagnostics{
    RuntimeHardwareLoadStatus::DefaultMissing,
    RuntimeHardwareValidation::Valid};
bool loaded = false;
}  // namespace

const RuntimeHardwareConfig& runtimeHardwareConfig() { return currentConfig; }

RuntimeHardwareLoadStatus loadRuntimeHardwareConfig() {
  NvsRuntimeHardwareStorage storage;
  const RuntimeHardwareLoadResult result =
      loadRuntimeHardwareConfig(storage, currentHardware());
  currentConfig = result.config;
  diagnostics = {result.status, result.validation};
  loaded = true;
  return result.status;
}

RuntimeHardwareSaveStatus saveRuntimeHardwareConfig(
    const RuntimeHardwareConfig& requested) {
  if (!loaded) loadRuntimeHardwareConfig();
  NvsRuntimeHardwareStorage storage;
  const RuntimeHardwareSaveStatus status = saveRuntimeHardwareConfig(
      storage, currentConfig, requested, hardwareCapabilities());
  if (status == RuntimeHardwareSaveStatus::Saved) {
    currentConfig = requested;
    diagnostics = {RuntimeHardwareLoadStatus::Loaded,
                   RuntimeHardwareValidation::Valid};
  }
  return status;
}

RuntimeHardwareDiagnostics runtimeHardwareDiagnostics() {
  return diagnostics;
}

const char* runtimeHardwareLoadStatusName(RuntimeHardwareLoadStatus status) {
  switch (status) {
    case RuntimeHardwareLoadStatus::Loaded: return "loaded";
    case RuntimeHardwareLoadStatus::DefaultMissing: return "default-missing";
    case RuntimeHardwareLoadStatus::DefaultInvalidVersion:
      return "default-invalid-version";
    case RuntimeHardwareLoadStatus::DefaultCorrupt: return "default-corrupt";
    case RuntimeHardwareLoadStatus::DefaultInvalidSelection:
      return "default-invalid-selection";
    case RuntimeHardwareLoadStatus::DefaultUnsupported:
      return "default-unsupported";
    case RuntimeHardwareLoadStatus::DefaultStorageError:
      return "default-storage-error";
  }
  return "default-unknown";
}

}  // namespace hardware
}  // namespace voxone
