#ifndef displayST7789_h
#define displayST7789_h

#include "Arduino.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "display_profile.h"

#if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
  #include "fonts/bootlogo62x40.h"
  #include "fonts/dsfont35.h"
#else
  #include "fonts/bootlogo99x64.h"
  #include "fonts/dsfont52.h"
#endif

typedef GFXcanvas16 Canvas;
typedef Adafruit_ST7789 yoDisplay;

#include "tools/commongfx.h"

#if __has_include("conf/displayST7789conf_custom.h")
  #include "conf/displayST7789conf_custom.h"
#else
  #if VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76
    #include "conf/displayST7789_76conf.h"
  #else
    #include "conf/displayST7789conf.h"
  #endif
#endif

#endif
