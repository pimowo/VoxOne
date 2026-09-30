#ifndef VOXONE_BT_VU_STATE_H
#define VOXONE_BT_VU_STATE_H

#include <stddef.h>
#include <stdint.h>
#include <math.h>

inline uint8_t btVuScalePeak(uint16_t peak, uint16_t dimension) {
  if (dimension > 255) dimension = 255;
  if (peak == 0 || dimension == 0) return 0;
  if (peak >= 32768) return static_cast<uint8_t>(dimension);

  // Measure only the display level. The PCM delivered to I2S0 is untouched.
  // Piecewise dBFS mapping leaves room for strong music peaks while making
  // normally attenuated A2DP samples visible on the meter.
  const float db = 20.0f * log10f(static_cast<float>(peak) / 32768.0f);
  float fraction = 0.0f;
  if (db > -48.0f && db < -24.0f)
    fraction = (db + 48.0f) * (0.35f / 24.0f);
  else if (db >= -24.0f && db < -12.0f)
    fraction = 0.35f + (db + 24.0f) * (0.30f / 12.0f);
  else if (db >= -12.0f && db < -6.0f)
    fraction = 0.65f + (db + 12.0f) * (0.20f / 6.0f);
  else if (db >= -6.0f)
    fraction = 0.85f + (db + 6.0f) * (0.15f / 6.0f);

  return static_cast<uint8_t>(fraction * dimension + 0.5f);
}

inline bool btVuDataFresh(uint32_t nowMs, uint32_t lastDataMs,
                          uint32_t timeoutMs = 150) {
  return static_cast<uint32_t>(nowMs - lastDataMs) <= timeoutMs;
}

#endif
