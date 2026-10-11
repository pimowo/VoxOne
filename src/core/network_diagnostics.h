#ifndef VOXONE_NETWORK_DIAGNOSTICS_H
#define VOXONE_NETWORK_DIAGNOSTICS_H

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace voxone::network_diagnostics {

inline void formatIpv4(char* output, std::size_t outputSize,
                       uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  if (!output || outputSize == 0) return;
  std::snprintf(output, outputSize, "%u.%u.%u.%u", a, b, c, d);
}

inline void formatMac(char* output, std::size_t outputSize,
                      const uint8_t* address) {
  if (!output || outputSize == 0) return;
  if (!address) {
    std::snprintf(output, outputSize, "--");
    return;
  }
  std::snprintf(output, outputSize, "%02X:%02X:%02X:%02X:%02X:%02X",
                address[0], address[1], address[2], address[3], address[4],
                address[5]);
}

}  // namespace voxone::network_diagnostics

#endif
