#ifndef VOXONE_SOURCE_VU_STATE_H
#define VOXONE_SOURCE_VU_STATE_H

#include <stdint.h>

#include "bt_vu_state.h"

struct SourceVuResult {
  uint16_t levels = 0;
  bool playing = false;
};

inline SourceVuResult sourceVuSelect(bool bluetoothActive,
                                     bool radioPlaying,
                                     uint16_t radioLegacyLevels,
                                     bool bluetoothPlaying,
                                     uint16_t peakLeft,
                                     uint16_t peakRight,
                                     uint32_t lastDataMs,
                                     uint32_t nowMs,
                                     uint16_t dimension,
                                     uint32_t freshnessMs = 150) {
  SourceVuResult result;
  const uint16_t boundedDimension = dimension > 255 ? 255 : dimension;
  if (bluetoothActive) {
    result.playing = bluetoothPlaying;
    if (!bluetoothPlaying ||
        !btVuDataFresh(nowMs, lastDataMs, freshnessMs))
      return result;
    const uint8_t left = btVuScalePeak(peakLeft, boundedDimension);
    const uint8_t right = btVuScalePeak(peakRight, boundedDimension);
    result.levels = static_cast<uint16_t>((left << 8) | right);
    return result;
  }

  result.playing = radioPlaying;
  const uint8_t legacyLeft = static_cast<uint8_t>(radioLegacyLevels >> 8);
  const uint8_t legacyRight = static_cast<uint8_t>(radioLegacyLevels);
  const uint8_t left = legacyLeft >= boundedDimension
      ? 0 : static_cast<uint8_t>(boundedDimension - legacyLeft);
  const uint8_t right = legacyRight >= boundedDimension
      ? 0 : static_cast<uint8_t>(boundedDimension - legacyRight);
  result.levels = static_cast<uint16_t>((left << 8) | right);
  return result;
}

#endif
