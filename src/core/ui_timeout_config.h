#ifndef VOXONE_UI_TIMEOUT_CONFIG_H
#define VOXONE_UI_TIMEOUT_CONFIG_H

#include <cstdint>
#include <cstdlib>

struct UiTimeoutSettings {
  uint8_t stationListSeconds = 10;
  uint8_t btTransportSeconds = 20;
};

inline bool parseUiTimeout(const char* text, uint8_t& seconds) {
  if (!text || !*text) return false;
  unsigned value = 0;
  for (const char* cursor = text; *cursor; ++cursor) {
    if (*cursor < '0' || *cursor > '9') return false;
    value = value * 10 + static_cast<unsigned>(*cursor - '0');
    if (value > 120) return false;
  }
  seconds = static_cast<uint8_t>(value);
  return true;
}

const UiTimeoutSettings& uiTimeoutConfig();
bool uiTimeoutSave(const UiTimeoutSettings& settings);
bool uiTimeoutReset();

#endif
