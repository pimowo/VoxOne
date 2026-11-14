#ifndef VOXONE_LOCAL_FILE_READ_POLICY_H
#define VOXONE_LOCAL_FILE_READ_POLICY_H

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace audio {

// FAT sectors are 512 bytes. Coalescing at least two sectors prevents the
// steady-state decoder cadence from degrading into hundreds of tiny SPI/FAT
// transactions, while a 4 KiB ceiling keeps the main loop responsive.
constexpr uint32_t kLocalFileMinimumReadBytes = 1024;
constexpr uint32_t kLocalFileMaximumReadBytes = 4096;

inline uint32_t localFileReadRequest(uint32_t availableBytes,
                                     uint32_t writeSpace,
                                     uint32_t permittedBytes) {
  uint32_t request = availableBytes < writeSpace ? availableBytes : writeSpace;
  if (request > permittedBytes) request = permittedBytes;
  if (request == 0) return 0;

  const bool finalFragment = permittedBytes <= request;
  if (!finalFragment && request < kLocalFileMinimumReadBytes) return 0;
  return request < kLocalFileMaximumReadBytes
             ? request : kLocalFileMaximumReadBytes;
}

}  // namespace audio
}  // namespace voxone

#endif
