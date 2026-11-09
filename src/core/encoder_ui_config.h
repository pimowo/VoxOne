#ifndef VOXONE_ENCODER_UI_CONFIG_H
#define VOXONE_ENCODER_UI_CONFIG_H

#include "power_mode_config.h"

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace encoder_ui {

// Config v7 has a frozen 255-byte wire format. Keep this optional extension
// in the retired weather area after the other versioned runtime records.
// An absent record is the migration path for every older configuration and
// means NORMAL (rotation controls volume).
constexpr size_t kOffset = power_mode::kOffset + power_mode::kEncodedBytes;
constexpr size_t kEncodedBytes = 5;

inline bool encode(bool reversed, uint8_t* storage, size_t storageSize) {
  if (!storage || storageSize < kOffset + kEncodedBytes) return false;
  storage[kOffset + 0] = 'V';
  storage[kOffset + 1] = 'E';
  storage[kOffset + 2] = 'U';
  storage[kOffset + 3] = 1;
  storage[kOffset + 4] = reversed ? 1 : 0;
  return true;
}

inline bool decode(const uint8_t* storage, size_t storageSize,
                   bool& reversed) {
  reversed = false;
  if (!storage || storageSize < kOffset + kEncodedBytes ||
      storage[kOffset + 0] != 'V' || storage[kOffset + 1] != 'E' ||
      storage[kOffset + 2] != 'U' || storage[kOffset + 3] != 1 ||
      storage[kOffset + 4] > 1)
    return false;
  reversed = storage[kOffset + 4] != 0;
  return true;
}

}  // namespace encoder_ui
}  // namespace voxone

#endif
