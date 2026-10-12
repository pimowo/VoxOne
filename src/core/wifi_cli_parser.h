#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ap_wifi_recovery.h"

namespace voxone {
namespace wifi_cli {

enum class ParseStatus : uint8_t { NotQuotedCommand, Valid, Invalid };

inline ParseStatus parseQuotedCredentials(const char* input, char* ssid,
                                          std::size_t ssidCapacity,
                                          char* password,
                                          std::size_t passwordCapacity) {
  constexpr char kPrefix[] = "wifi.con(\"";
  constexpr std::size_t kPrefixLength = sizeof(kPrefix) - 1;
  if (!input || std::strncmp(input, kPrefix, kPrefixLength) != 0)
    return ParseStatus::NotQuotedCommand;
  if (!ssid || !password || !ssidCapacity || !passwordCapacity)
    return ParseStatus::Invalid;
  ssid[0] = '\0';
  password[0] = '\0';

  const char* ssidStart = input + kPrefixLength;
  const char* ssidEnd = std::strchr(ssidStart, '"');
  if (!ssidEnd || ssidEnd[1] != ',' || ssidEnd[2] != '"')
    return ParseStatus::Invalid;
  const char* passwordStart = ssidEnd + 3;
  const char* passwordEnd = std::strchr(passwordStart, '"');
  if (!passwordEnd || passwordEnd[1] != ')' || passwordEnd[2] != '\0')
    return ParseStatus::Invalid;

  const std::size_t ssidLength = static_cast<std::size_t>(ssidEnd - ssidStart);
  const std::size_t passwordLength =
      static_cast<std::size_t>(passwordEnd - passwordStart);
  if (ssidLength >= ssidCapacity || passwordLength >= passwordCapacity)
    return ParseStatus::Invalid;
  std::memcpy(ssid, ssidStart, ssidLength);
  ssid[ssidLength] = '\0';
  std::memcpy(password, passwordStart, passwordLength);
  password[passwordLength] = '\0';
  return apWifiCredentialsValid(ssid, password) ? ParseStatus::Valid
                                                 : ParseStatus::Invalid;
}

}  // namespace wifi_cli
}  // namespace voxone
