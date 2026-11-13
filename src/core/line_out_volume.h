#ifndef VOXONE_LINE_OUT_VOLUME_H
#define VOXONE_LINE_OUT_VOLUME_H

#include "line_out_config.h"
#include "config_runtime_snapshot.h"
#include "volume_map.h"

namespace voxone {
namespace line_out {

inline uint8_t effectiveRawVolume(
    const config_runtime::Snapshot& runtime) {
  return runtime.lineOutEnabled
             ? volumeUserToRaw(clampToMaximum(runtime.lineOutVolume,
                                              runtime.maximumVolume),
                               runtime.maximumVolume)
             : runtime.volume;
}

inline uint8_t effectiveUserVolume(
    const config_runtime::Snapshot& runtime) {
  return runtime.lineOutEnabled
             ? clampToMaximum(runtime.lineOutVolume, runtime.maximumVolume)
             : runtime.userVolume;
}

}  // namespace line_out
}  // namespace voxone

#endif
