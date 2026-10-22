#include "security_settings.h"
#include "security_runtime.h"
#include "security_api.h"

#include <esp_system.h>
#include <nvs.h>

#include <cstring>

namespace voxone {
namespace security {
namespace {

constexpr char kNamespace[] = "voxsecurity";
constexpr char kRecordKey[] = "settings";

void secureZero(void* memory, size_t size) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(memory);
  while (size--) *bytes++ = 0;
}

class NvsSecurityStorage : public SecurityStorage {
 public:
  SecurityStorageReadStatus read(uint8_t* output, size_t capacity,
                                 size_t& size) override {
    size = 0;
    nvs_handle_t handle;
    const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND)
      return SecurityStorageReadStatus::Missing;
    if (opened != ESP_OK) return SecurityStorageReadStatus::Error;

    size_t storedSize = 0;
    esp_err_t result = nvs_get_blob(handle, kRecordKey, nullptr, &storedSize);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
      nvs_close(handle);
      return SecurityStorageReadStatus::Missing;
    }
    if (result != ESP_OK) {
      nvs_close(handle);
      return SecurityStorageReadStatus::Error;
    }
    size = storedSize;
    if (storedSize > capacity) {
      nvs_close(handle);
      return SecurityStorageReadStatus::Found;
    }
    result = nvs_get_blob(handle, kRecordKey, output, &storedSize);
    nvs_close(handle);
    size = storedSize;
    return result == ESP_OK ? SecurityStorageReadStatus::Found
                            : SecurityStorageReadStatus::Error;
  }

  bool write(const uint8_t* input, size_t size) override {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    bool saved = nvs_set_blob(handle, kRecordKey, input, size) == ESP_OK &&
                 nvs_commit(handle) == ESP_OK;
    uint8_t verified[kSecurityRecordSize];
    size_t verifiedSize = sizeof(verified);
    saved = saved && size == sizeof(verified) &&
            nvs_get_blob(handle, kRecordKey, verified, &verifiedSize) == ESP_OK &&
            verifiedSize == size && std::memcmp(input, verified, size) == 0;
    secureZero(verified, sizeof(verified));
    nvs_close(handle);
    return saved;
  }

  bool erase() override {
    nvs_handle_t handle;
    const esp_err_t opened = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
    if (opened != ESP_OK) return false;
    const esp_err_t erased = nvs_erase_key(handle, kRecordKey);
    const bool result = (erased == ESP_OK || erased == ESP_ERR_NVS_NOT_FOUND) &&
                        nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return result;
  }
};

class EspSecurityRandomSource : public SecurityRandomSource {
 public:
  bool fill(uint8_t* output, size_t size) override {
    if (!output || !size) return false;
    esp_fill_random(output, size);
    return true;
  }
};

EspSecurityRandomSource& deviceSecurityRandomSource() {
  static EspSecurityRandomSource random;
  return random;
}

}  // namespace

SecurityService& deviceSecurityService() {
  static NvsSecurityStorage storage;
  static SecurityService service(storage, deviceSecurityRandomSource());
  return service;
}

SecurityOperationStatus recoverDeviceSecurityPassword(const char* newPassword) {
  SecurityService& service = deviceSecurityService();
  const SecurityLoadStatus loaded = service.begin(true);
  if (loaded != SecurityLoadStatus::Loaded)
    return SecurityOperationStatus::InvalidState;
  const SecurityOperationStatus result = service.recoverProtected(newPassword);
  if (result == SecurityOperationStatus::Applied)
    deviceSecuritySessionService().invalidateAll();
  return result;
}

SecuritySessionService& deviceSecuritySessionService() {
  static SecuritySessionService sessions(deviceSecurityService(),
                                          deviceSecurityRandomSource());
  return sessions;
}

SecurityRuntimeDiagnostics initializeDeviceSecurity(bool wifiConfigured) {
  return initializeSecurityRuntime(deviceSecurityService(), wifiConfigured);
}

}  // namespace security
}  // namespace voxone
