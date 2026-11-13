#ifndef VOXONE_LINE_OUT_CONFIG_H
#define VOXONE_LINE_OUT_CONFIG_H

#include "encoder_ui_config.h"

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace line_out {

constexpr bool kDefaultEnabled = false;
constexpr uint8_t kDefaultVolume = 100;

inline uint8_t maximumOrDefault(uint8_t maximum) {
  return maximum >= 1 && maximum <= 100 ? maximum : 100;
}

inline uint8_t clampToMaximum(uint8_t volume, uint8_t maximum) {
  const uint8_t boundedMaximum = maximumOrDefault(maximum);
  return volume > boundedMaximum ? boundedMaximum : volume;
}

// Config v7 has a frozen 255-byte wire format. Keep LINE OUT in the retired
// weather area after the other optional, versioned runtime records.
constexpr size_t kOffset = encoder_ui::kOffset + encoder_ui::kEncodedBytes;
constexpr size_t kEncodedBytes = 6;

inline bool encode(bool enabled, uint8_t volume, uint8_t* storage,
                   size_t storageSize) {
  if (!storage || storageSize < kOffset + kEncodedBytes || volume > 100)
    return false;
  storage[kOffset + 0] = 'V';
  storage[kOffset + 1] = 'L';
  storage[kOffset + 2] = 'O';
  storage[kOffset + 3] = 1;
  storage[kOffset + 4] = enabled ? 1 : 0;
  storage[kOffset + 5] = volume;
  return true;
}

inline bool decode(const uint8_t* storage, size_t storageSize, bool& enabled,
                   uint8_t& volume) {
  enabled = kDefaultEnabled;
  volume = kDefaultVolume;
  if (!storage || storageSize < kOffset + kEncodedBytes ||
      storage[kOffset + 0] != 'V' || storage[kOffset + 1] != 'L' ||
      storage[kOffset + 2] != 'O' || storage[kOffset + 3] != 1 ||
      storage[kOffset + 4] > 1 || storage[kOffset + 5] > 100)
    return false;
  enabled = storage[kOffset + 4] != 0;
  volume = storage[kOffset + 5];
  return true;
}

}  // namespace line_out
}  // namespace voxone

#endif
