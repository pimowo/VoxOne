#ifndef VOXONE_CORE_UTILS_H
#define VOXONE_CORE_UTILS_H

#include <stdint.h>

enum displayMode_e {
  PLAYER, VOL, STATIONS, NUMBERS, LOST, UPDATING, INFO, SETTINGS, TIMEZONE,
  WIFI, CLEAR, SLEEPING, SCREENSAVER, SCREENBLANK, BT_TRANSPORT, SHUTTING_DOWN,
  MENU
};

enum pages_e : uint8_t {
  PG_PLAYER = 0, PG_DIALOG = 1, PG_PLAYLIST = 2, PG_SCREENSAVER = 3
};

enum displayRequestType_e {
  BOOTSTRING, NEWMODE, CLOCK, NEWTITLE, NEWSTATION, NEXTSTATION, DRAWPLAYLIST,
  CLOSEPLAYLIST, DRAWVOL, DBITRATE, SHOWVUMETER, DSPRSSI, PSTOP, PSTART,
  DSP_START, NEWIP, NOPE, RESETIDLE
};

struct requestParams_t {
  displayRequestType_e type;
  int payload;
};

enum controlEvt_e {
  EVT_NONE = 255, EVT_BTNLEFT = 0, EVT_BTNCENTER = 1, EVT_BTNRIGHT = 2,
  EVT_ENCBTNB = 3, EVT_BTNUP = 4, EVT_BTNDOWN = 5, EVT_BTNMODE = 7
};

// Tone values are the preset state; there is no separate preset identifier.
inline const char* eqPresetLabel(int8_t bass, int8_t middle, int8_t treble) {
  if (bass == 0 && middle == 0 && treble == 0) return "FLAT";
  if (bass == 6 && middle == -2 && treble == 0) return "BASS";
  if (bass == 5 && middle == -2 && treble == 4) return "ROCK";
  if (bass == 3 && middle == 2 && treble == 3) return "POP";
  if (bass == -4 && middle == 4 && treble == 2) return "MOWA";
  return "USER";
}

namespace voxone {
namespace vu {

constexpr uint32_t kFrameIntervalMs = 50;  // 20 FPS maximum.

inline bool frameDue(uint32_t nowMs, uint32_t lastFrameMs) {
  return static_cast<uint32_t>(nowMs - lastFrameMs) >= kFrameIntervalMs;
}

}  // namespace vu
}  // namespace voxone

#endif
