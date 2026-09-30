#ifndef VOXONE_EQ_PRESET_LABEL_H
#define VOXONE_EQ_PRESET_LABEL_H

#include <stdint.h>

// Match the preset recognition used by the current VoxOne WWW. The saved tone
// values are the only preset state; there is no separate preset identifier.
inline const char* eqPresetLabel(int8_t bass, int8_t middle, int8_t treble) {
  if (bass == 0 && middle == 0 && treble == 0) return "FLAT";
  if (bass == 6 && middle == -2 && treble == 0) return "BASS";
  if (bass == 5 && middle == -2 && treble == 4) return "ROCK";
  if (bass == 3 && middle == 2 && treble == 3) return "POP";
  if (bass == -4 && middle == 4 && treble == 2) return "MOWA";
  return "USER";
}

#endif
