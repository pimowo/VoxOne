#ifndef VOXONE_WEB_FILE_BOUNDS_H
#define VOXONE_WEB_FILE_BOUNDS_H

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace audio {

inline bool resolveWavDataSize(uint32_t contentLength, uint32_t dataOffset,
                               uint32_t declaredDataSize,
                               size_t& resolvedDataSize) {
  if (dataOffset > contentLength) return false;
  const uint32_t bodyRemainder = contentLength - dataOffset;
  if (declaredDataSize && declaredDataSize > bodyRemainder) return false;
  resolvedDataSize = declaredDataSize ? declaredDataSize : bodyRemainder;
  return true;
}

inline uint32_t webFileReadLimit(uint32_t contentLength, uint32_t bodyBytesRead,
                                 uint32_t audioDataOffset,
                                 size_t audioDataSize) {
  if (bodyBytesRead >= contentLength) return 0;
  uint32_t remaining = contentLength - bodyBytesRead;
  if (!audioDataSize || bodyBytesRead < audioDataOffset) return remaining;

  const uint64_t audioBytesRead =
      static_cast<uint64_t>(bodyBytesRead - audioDataOffset);
  if (audioBytesRead >= audioDataSize) return 0;
  const uint64_t audioRemaining = audioDataSize - audioBytesRead;
  if (audioRemaining < remaining)
    remaining = static_cast<uint32_t>(audioRemaining);
  return remaining;
}

inline bool webFileDataComplete(uint32_t contentLength, uint32_t bodyBytesRead,
                                uint32_t audioDataOffset,
                                size_t audioDataSize) {
  if (bodyBytesRead >= contentLength) return true;
  if (!audioDataSize || bodyBytesRead < audioDataOffset) return false;
  return static_cast<uint64_t>(bodyBytesRead - audioDataOffset) >=
         audioDataSize;
}

}  // namespace audio
}  // namespace voxone

#endif
