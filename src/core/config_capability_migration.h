#ifndef VOXONE_CONFIG_CAPABILITY_MIGRATION_H
#define VOXONE_CONFIG_CAPABILITY_MIGRATION_H

#include <stdint.h>

#include "config_format.h"

namespace voxone {
namespace config_capability_migration {

// Stored outside Config v7 so the frozen 255-byte wire format remains intact.
// VXCF restore deliberately does not roll this device capability marker back.
enum class MarkerStatus : uint8_t {
  Missing,
  Applied,
  Unavailable,
  Invalid
};

struct Plan {
  bool enableForThisBoot;
  bool persistEnabled;
  bool markAppliedAfterPersistence;
};

inline bool writableStartupStatus(config_format::ConfigRecordStatus status) {
  return status != config_format::ConfigRecordStatus::UNSUPPORTED_NEWER &&
         status != config_format::ConfigRecordStatus::INVALID_ARGUMENT;
}

// A pre-capability S0 v7 false is indistinguishable from a hypothetical user
// choice. Apply one documented introduction migration, then let the marker
// preserve every later explicit false across restart, OTA and VXCF restore.
inline Plan planS0BtIntroduction(
    bool exactS0Profile, bool supportsBt,
    config_format::ConfigRecordStatus startupStatus,
    bool storedBtEnabled, MarkerStatus marker) {
  if (!exactS0Profile || !supportsBt || marker != MarkerStatus::Missing ||
      !writableStartupStatus(startupStatus))
    return {false, false, false};
  return {true, !storedBtEnabled, true};
}

MarkerStatus readS0BtIntroductionMarker();
bool writeS0BtIntroductionMarker();

}  // namespace config_capability_migration
}  // namespace voxone

#endif
