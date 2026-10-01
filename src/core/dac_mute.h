#ifndef VOXONE_DAC_MUTE_H
#define VOXONE_DAC_MUTE_H

#include "dac_mute_state.h"
#include <atomic>

class DacMuteController {
 public:
  void begin();
  void update(DacPlaybackState playback, bool updateAudioBlocked);
  bool logicalMuted() const { return !high_.load(std::memory_order_relaxed); }

 private:
  std::atomic<bool> high_{false};
};

extern DacMuteController dacMute;

#endif
