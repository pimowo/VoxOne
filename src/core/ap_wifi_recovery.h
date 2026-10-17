#pragma once

#include <cstddef>
#include <cstring>

#include "wifi_config_parser.h"

inline bool apWifiCredentialsValid(const char* ssid, const char* password) {
  if (!ssid || !password) return false;
  const size_t ssidLength = std::strlen(ssid);
  const size_t passwordLength = std::strlen(password);
  if (ssidLength == 0 || ssidLength >= voxone::wifi_config::kSsidCapacity ||
      passwordLength >= voxone::wifi_config::kPasswordCapacity)
    return false;
  return voxone::wifi_config::validFieldBytes(
             reinterpret_cast<const uint8_t*>(ssid), ssidLength) &&
         voxone::wifi_config::validFieldBytes(
             reinterpret_cast<const uint8_t*>(password), passwordLength);
}

inline bool netServerShouldInitialize(bool started) {
  return !started;
}
