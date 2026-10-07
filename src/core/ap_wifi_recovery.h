#pragma once

#include <cstddef>
#include <cstring>

inline bool apWifiRecoveryAllowed(bool connected) {
  return !connected;
}

inline bool apWifiCredentialsValid(const char* ssid, const char* password) {
  if (!ssid || !password) return false;
  const size_t ssidLength = std::strlen(ssid);
  const size_t passwordLength = std::strlen(password);
  if (ssidLength == 0 || ssidLength >= 30 || passwordLength >= 40) return false;
  return !std::strpbrk(ssid, "\t\r\n") && !std::strpbrk(password, "\t\r\n");
}

inline bool netServerShouldInitialize(bool started) {
  return !started;
}
