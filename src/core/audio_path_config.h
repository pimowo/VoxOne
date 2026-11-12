#ifndef VOXONE_AUDIO_PATH_CONFIG_H
#define VOXONE_AUDIO_PATH_CONFIG_H

#include "line_out_config.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum AudioPath : uint8_t {
  AUDIO_PCM5102 = 0,
  AUDIO_DSPMINI = 1,
};

namespace voxone {
namespace audio_path {

constexpr AudioPath kDefault = AUDIO_PCM5102;

// Config v7 has a frozen 255-byte wire format. Store the audio-path extension
// after the other versioned records in the retired weather area.
constexpr size_t kOffset = line_out::kOffset + line_out::kEncodedBytes;
constexpr size_t kEncodedBytes = 5;

inline bool valid(AudioPath path) {
  return path == AUDIO_PCM5102 || path == AUDIO_DSPMINI;
}

inline bool encode(AudioPath path, uint8_t* storage, size_t storageSize) {
  if (!storage || storageSize < kOffset + kEncodedBytes || !valid(path))
    return false;
  storage[kOffset + 0] = 'V';
  storage[kOffset + 1] = 'A';
  storage[kOffset + 2] = 'P';
  storage[kOffset + 3] = 1;
  storage[kOffset + 4] = static_cast<uint8_t>(path);
  return true;
}

inline bool decode(const uint8_t* storage, size_t storageSize,
                   AudioPath& path) {
  path = kDefault;
  if (!storage || storageSize < kOffset + kEncodedBytes ||
      storage[kOffset + 0] != 'V' || storage[kOffset + 1] != 'A' ||
      storage[kOffset + 2] != 'P' || storage[kOffset + 3] != 1)
    return false;
  const AudioPath decoded = static_cast<AudioPath>(storage[kOffset + 4]);
  if (!valid(decoded)) return false;
  path = decoded;
  return true;
}

inline const char* apiName(AudioPath path) {
  return path == AUDIO_DSPMINI ? "dspmini" : "pcm5102";
}

inline bool parseApiName(const char* value, AudioPath& path) {
  if (!value) return false;
  if (strcmp(value, "pcm5102") == 0) {
    path = AUDIO_PCM5102;
    return true;
  }
  if (strcmp(value, "dspmini") == 0) {
    path = AUDIO_DSPMINI;
    return true;
  }
  return false;
}

}  // namespace audio_path
}  // namespace voxone

#endif
