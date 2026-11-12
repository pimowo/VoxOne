#ifndef VOXONE_BT_AUDIO_ROUTE_STATE_H
#define VOXONE_BT_AUDIO_ROUTE_STATE_H

#include <stdint.h>

struct BtAudioRouteTarget {
  bool radioOutput;
  bool btOutput;
  uint32_t outputRate;
};

// I2S1 data is the physical proof that the A2DP stream has started. Keep a
// short grace period so a single scheduler/DMA gap does not flap the DAC,
// while still closing the route promptly after A2DP suspend/stop.
constexpr uint32_t kBtPcmActivityTimeoutMs = 120;

inline bool btPcmStreamStarted(bool pcmSeen, uint32_t lastPcmMs,
                               uint32_t nowMs) {
  return pcmSeen && nowMs - lastPcmMs <= kBtPcmActivityTimeoutMs;
}

inline BtAudioRouteTarget btAudioRouteTarget(bool bluetoothSelected,
                                            bool physicalStreamStarted,
                                            bool rxRunning, uint32_t btRate,
                                            uint32_t radioRate) {
  const bool btOutput = bluetoothSelected && physicalStreamStarted &&
                        rxRunning && btRate != 0;
  const bool radioOutput = !bluetoothSelected && radioRate != 0;
  return {radioOutput, btOutput,
          bluetoothSelected ? (btOutput ? btRate : 0)
                            : (radioOutput ? radioRate : 0)};
}

#endif
