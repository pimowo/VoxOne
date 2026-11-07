#ifndef VOXONE_HA_NAME_CONFIG_H
#define VOXONE_HA_NAME_CONFIG_H

#include "wifi_config_parser.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace voxone {
namespace ha_name {

constexpr size_t kMaximumBytes = 32;
constexpr size_t kRuntimeCapacity = kMaximumBytes + 1;
constexpr size_t kStorageBytes = 79;
constexpr size_t kHeaderBytes = 5;
constexpr size_t kEncodedBytes = kHeaderBytes + kMaximumBytes;

inline bool valid(const char* value, size_t length) {
  if (!value || length > kMaximumBytes ||
      !wifi_config::validUtf8(
          reinterpret_cast<const uint8_t*>(value), length))
    return false;
  for (size_t index = 0; index < length; ++index) {
    const uint8_t byte = static_cast<uint8_t>(value[index]);
    if (byte == 0 || byte < 0x20 || byte == 0x7f) return false;
  }
  return true;
}

inline bool encode(const char* value, size_t length, uint8_t* storage,
                   size_t storageSize) {
  if (!storage || storageSize < kEncodedBytes || !valid(value, length))
    return false;
  storage[0] = 'V';
  storage[1] = 'H';
  storage[2] = 'A';
  storage[3] = 1;
  storage[4] = static_cast<uint8_t>(length);
  memset(storage + kHeaderBytes, 0, kMaximumBytes);
  if (length) memcpy(storage + kHeaderBytes, value, length);
  return true;
}

inline bool decode(const uint8_t* storage, size_t storageSize, char* output,
                   size_t outputSize) {
  if (!output || !outputSize) return false;
  output[0] = '\0';
  if (!storage || storageSize < kEncodedBytes || storage[0] != 'V' ||
      storage[1] != 'H' || storage[2] != 'A' || storage[3] != 1)
    return false;
  const size_t length = storage[4];
  if (length > kMaximumBytes || length >= outputSize ||
      !valid(reinterpret_cast<const char*>(storage + kHeaderBytes), length))
    return false;
  if (length) memcpy(output, storage + kHeaderBytes, length);
  output[length] = '\0';
  return true;
}

}  // namespace ha_name
}  // namespace voxone

#endif
