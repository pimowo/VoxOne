#ifndef VOXONE_POWER_MODE_CONFIG_H
#define VOXONE_POWER_MODE_CONFIG_H

#include "tft_theme_config.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum PowerMode : uint8_t {
  MODE_SOFT_STANDBY = 0,
  MODE_HARD_POWER = 1,
  MODE_MUTE_ONLY = 2,
};

namespace voxone {
namespace power_mode {

// Config v7 has a frozen 255-byte wire format. Keep this extension in the
// retired weather area after the HA name and TFT theme records.
constexpr size_t kOffset = tft_theme::kOffset + tft_theme::kEncodedBytes;
constexpr size_t kEncodedBytes = 5;

inline bool valid(PowerMode mode) {
  return mode == MODE_SOFT_STANDBY || mode == MODE_HARD_POWER ||
         mode == MODE_MUTE_ONLY;
}

inline bool encode(PowerMode mode, uint8_t* storage, size_t storageSize) {
  if (!storage || storageSize < kOffset + kEncodedBytes || !valid(mode))
    return false;
  storage[kOffset + 0] = 'V';
  storage[kOffset + 1] = 'P';
  storage[kOffset + 2] = 'M';
  storage[kOffset + 3] = 1;
  storage[kOffset + 4] = static_cast<uint8_t>(mode);
  return true;
}

inline bool decode(const uint8_t* storage, size_t storageSize,
                   PowerMode& mode) {
  mode = MODE_SOFT_STANDBY;
  if (!storage || storageSize < kOffset + kEncodedBytes ||
      storage[kOffset + 0] != 'V' || storage[kOffset + 1] != 'P' ||
      storage[kOffset + 2] != 'M' || storage[kOffset + 3] != 1)
    return false;
  const PowerMode decoded = static_cast<PowerMode>(storage[kOffset + 4]);
  if (!valid(decoded)) return false;
  mode = decoded;
  return true;
}

inline const char* apiName(PowerMode mode) {
  switch (mode) {
    case MODE_HARD_POWER: return "hard_power";
    case MODE_MUTE_ONLY: return "mute_only";
    case MODE_SOFT_STANDBY: return "soft_standby";
  }
  return "soft_standby";
}

inline bool parseApiName(const char* value, PowerMode& mode) {
  if (!value) return false;
  if (strcmp(value, "soft_standby") == 0) {
    mode = MODE_SOFT_STANDBY;
    return true;
  }
  if (strcmp(value, "hard_power") == 0) {
    mode = MODE_HARD_POWER;
    return true;
  }
  if (strcmp(value, "mute_only") == 0) {
    mode = MODE_MUTE_ONLY;
    return true;
  }
  return false;
}

}  // namespace power_mode
}  // namespace voxone

#endif
