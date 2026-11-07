#ifndef VOXONE_BUILD_CAPABILITIES_H
#define VOXONE_BUILD_CAPABILITIES_H

#ifndef VOXONE_BUILD_IS_PRODUCTION_FIRMWARE
// Fail closed: every ordinary build enforces production security unless a
// dedicated test environment explicitly opts out.
#define VOXONE_BUILD_IS_PRODUCTION_FIRMWARE 1
#endif

#if VOXONE_BUILD_IS_PRODUCTION_FIRMWARE != 0 && \
    VOXONE_BUILD_IS_PRODUCTION_FIRMWARE != 1
#error "VOXONE_BUILD_IS_PRODUCTION_FIRMWARE must be 0 or 1"
#endif

namespace voxone {
namespace build {

struct BuildCapabilities {
  bool isProductionFirmware;
};

constexpr BuildCapabilities buildCapabilities() {
  return {VOXONE_BUILD_IS_PRODUCTION_FIRMWARE != 0};
}

}  // namespace build
}  // namespace voxone

#endif
