#ifndef VOXONE_TFT_THEME_CONFIG_H
#define VOXONE_TFT_THEME_CONFIG_H

#include "ha_name_config.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum TftTheme : uint8_t {
  THEME_DEFAULT = 0,
  THEME_YAMAHA = 1,
};

namespace voxone {
namespace tft_theme {

// Config v7 has a frozen wire format. Store this optional extension after the
// HA-name extension in the retired weather bytes instead of changing it.
constexpr size_t kOffset = ha_name::kEncodedBytes;
constexpr size_t kEncodedBytes = 5;

inline bool valid(TftTheme theme) {
  return theme == THEME_DEFAULT || theme == THEME_YAMAHA;
}

inline bool encode(TftTheme theme, uint8_t* storage, size_t storageSize) {
  if (!storage || storageSize < kOffset + kEncodedBytes || !valid(theme))
    return false;
  storage[kOffset + 0] = 'V';
  storage[kOffset + 1] = 'T';
  storage[kOffset + 2] = 'F';
  storage[kOffset + 3] = 1;
  storage[kOffset + 4] = static_cast<uint8_t>(theme);
  return true;
}

inline bool decode(const uint8_t* storage, size_t storageSize,
                   TftTheme& theme) {
  theme = THEME_DEFAULT;
  if (!storage || storageSize < kOffset + kEncodedBytes ||
      storage[kOffset + 0] != 'V' || storage[kOffset + 1] != 'T' ||
      storage[kOffset + 2] != 'F' || storage[kOffset + 3] != 1)
    return false;
  const TftTheme decoded = static_cast<TftTheme>(storage[kOffset + 4]);
  if (!valid(decoded)) return false;
  theme = decoded;
  return true;
}

inline const char* apiName(TftTheme theme) {
  return theme == THEME_YAMAHA ? "yamaha" : "default";
}

inline bool parseApiName(const char* value, TftTheme& theme) {
  if (!value) return false;
  if (strcmp(value, "default") == 0) {
    theme = THEME_DEFAULT;
    return true;
  }
  if (strcmp(value, "yamaha") == 0) {
    theme = THEME_YAMAHA;
    return true;
  }
  return false;
}

}  // namespace tft_theme
}  // namespace voxone

#endif
