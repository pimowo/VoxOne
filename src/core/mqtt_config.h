#ifndef VOXONE_MQTT_CONFIG_H
#define VOXONE_MQTT_CONFIG_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>

struct MqttSettings {
  uint8_t enabled;
  char host[128];
  uint16_t port;
  char username[64];
  char password[128];
  char rootTopic[64]; // Empty selects the MAC-based root.
};

inline MqttSettings mqttDefaultSettings() {
  MqttSettings settings{};
  settings.port = 1883;
  return settings;
}

inline bool mqttAsciiWhitespace(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
}

inline bool mqttNormalizeRoot(const char* input, char* output, size_t capacity) {
  if (!input || !output || capacity == 0) return false;
  const char* first = input;
  while (mqttAsciiWhitespace(*first)) ++first;
  const char* last = first + std::strlen(first);
  while (last > first && mqttAsciiWhitespace(last[-1])) --last;
  while (first < last && *first == '/') ++first;
  while (last > first && last[-1] == '/') --last;
  size_t used = 0;
  for (const char* cursor = first; cursor < last; ++cursor) {
    const char ch = *cursor;
    if (ch == '/') {
      if (used && output[used - 1] != '/') {
        if (used + 1 >= capacity) return false;
        output[used++] = '/';
      }
      continue;
    }
    const bool allowed = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                         (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.';
    if (!allowed || used + 1 >= capacity) return false;
    output[used++] = ch;
  }
  output[used] = '\0';
  return true;
}

inline bool mqttNormalizeHost(const char* input, char* output, size_t capacity) {
  if (!input || !output || capacity == 0) return false;
  const char* first = input;
  while (mqttAsciiWhitespace(*first)) ++first;
  const char* last = first + std::strlen(first);
  while (last > first && mqttAsciiWhitespace(last[-1])) --last;
  const size_t length = static_cast<size_t>(last - first);
  if (length >= capacity) return false;
  for (size_t i = 0; i < length; ++i) {
    const char ch = first[i];
    const bool allowed = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                         (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_';
    if (!allowed) return false;
  }
  std::memcpy(output, first, length);
  output[length] = '\0';
  return true;
}

inline bool mqttValidSettings(const MqttSettings& settings) {
  if (settings.enabled > 1 || settings.port == 0) return false;
  if (!std::memchr(settings.host, 0, sizeof(settings.host)) ||
      !std::memchr(settings.username, 0, sizeof(settings.username)) ||
      !std::memchr(settings.password, 0, sizeof(settings.password)) ||
      !std::memchr(settings.rootTopic, 0, sizeof(settings.rootTopic))) return false;
  if (settings.enabled && !settings.host[0]) return false;
  if (!settings.username[0] && settings.password[0]) return false;
  char normalizedHost[sizeof(settings.host)];
  char normalizedRoot[sizeof(settings.rootTopic)];
  if (!mqttNormalizeHost(settings.host, normalizedHost, sizeof(normalizedHost)) ||
      std::strcmp(settings.host, normalizedHost) != 0 ||
      !mqttNormalizeRoot(settings.rootTopic, normalizedRoot, sizeof(normalizedRoot)) ||
      std::strcmp(settings.rootTopic, normalizedRoot) != 0) return false;
  return true;
}

inline bool mqttApplyPassword(MqttSettings& settings, const char* replacement, bool clear) {
  if (!replacement) replacement = "";
  const size_t length = std::strlen(replacement);
  if (length >= sizeof(settings.password) || (clear && length != 0)) return false;
  if (clear || length) {
    std::memset(settings.password, 0, sizeof(settings.password));
    if (length) std::memcpy(settings.password, replacement, length);
  }
  return true;
}

inline bool mqttParseWireVolume(const char* command, int& wire) {
  if (!command || std::strncmp(command, "vol", 3) != 0 ||
      !mqttAsciiWhitespace(command[3])) return false;
  const char* number = command + 3;
  while (mqttAsciiWhitespace(*number)) ++number;
  char* end = nullptr;
  const long parsed = std::strtol(number, &end, 10);
  if (end == number) return false;
  wire = parsed < 0 ? 0 : parsed > 254 ? 254 : static_cast<int>(parsed);
  return true;
}

inline uint8_t mqttUserToWire(uint8_t user) {
  if (user > 100) user = 100;
  return static_cast<uint8_t>((static_cast<uint16_t>(user) * 254 + 50) / 100);
}

inline uint8_t mqttWireToUser(int wire) {
  if (wire < 0) wire = 0;
  if (wire > 254) wire = 254;
  return static_cast<uint8_t>((wire * 100 + 127) / 254);
}

inline void mqttAutoRootFromMac(const uint8_t mac[6], char output[14]) {
  static const char hex[] = "0123456789ABCDEF";
  std::memcpy(output, "voxone-", 7);
  for (size_t i = 0; i < 3; ++i) {
    output[7 + i * 2] = hex[mac[3 + i] >> 4];
    output[8 + i * 2] = hex[mac[3 + i] & 15];
  }
  output[13] = '\0';
}

const MqttSettings& mqttConfig();
bool mqttSaveConfig(const MqttSettings& settings);
bool mqttClearConfig();
bool mqttAutoRoot(char output[14]);
bool mqttEffectiveRoot(char* output, size_t capacity);

#endif
