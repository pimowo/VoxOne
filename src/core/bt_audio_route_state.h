#ifndef VOXONE_BT_AUDIO_ROUTE_STATE_H
#define VOXONE_BT_AUDIO_ROUTE_STATE_H

#include <stdint.h>

struct BtAudioRouteTarget {
  bool radioOutput;
  bool btOutput;
  uint32_t outputRate;
};

inline BtAudioRouteTarget btAudioRouteTarget(bool bluetoothSelected,
                                            bool playbackPlaying,
                                            bool rxRunning, uint32_t btRate,
                                            uint32_t radioRate) {
  const bool btOutput = bluetoothSelected && playbackPlaying && rxRunning &&
                        btRate != 0;
  return {!bluetoothSelected, btOutput,
          bluetoothSelected ? (btOutput ? btRate : 0) : radioRate};
}

#endif
