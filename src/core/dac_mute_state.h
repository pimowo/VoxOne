#ifndef VOXONE_DAC_MUTE_STATE_H
#define VOXONE_DAC_MUTE_STATE_H

#include <stdint.h>
#include "bt_link_protocol.h"

enum class DacPlaybackState : uint8_t { Stopped, Paused, Playing };

inline DacPlaybackState dacPlaybackForSource(bool bluetoothSelected,
                                             bool radioPlaying,
                                             bool bluetoothReady,
                                             BtPlayback bluetoothPlayback) {
  if (!bluetoothSelected)
    return radioPlaying ? DacPlaybackState::Playing : DacPlaybackState::Stopped;
  if (!bluetoothReady) return DacPlaybackState::Stopped;
  if (bluetoothPlayback == BtPlayback::Playing) return DacPlaybackState::Playing;
  if (bluetoothPlayback == BtPlayback::Paused) return DacPlaybackState::Paused;
  return DacPlaybackState::Stopped;
}

// XSMT is active low. Transitional states and Web Update remain silent.
inline bool dacXsmtHigh(DacPlaybackState playback, bool updateAudioBlocked) {
  return playback == DacPlaybackState::Playing && !updateAudioBlocked;
}

#endif
