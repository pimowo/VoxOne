#ifndef VOXONE_BT_AUDIO_INPUT_STATE_H
#define VOXONE_BT_AUDIO_INPUT_STATE_H

#include "bt_link_protocol.h"

// Zero means that I2S RX must be stopped. Only the formats confirmed for
// VoxOneBT's 16-bit stereo PCM link are enabled in this diagnostic stage.
inline uint32_t btAudioDesiredRate(const BtLinkState& link) {
  if (!link.runtimeAvailable || !link.connected) return 0;
  return link.sampleRate == 44100 || link.sampleRate == 48000
             ? link.sampleRate
             : 0;
}

#endif
