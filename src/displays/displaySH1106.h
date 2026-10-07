#ifndef displaySH1106_h
#define displaySH1106_h

#include "Arduino.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "fonts/bootlogo21x32.h"

typedef GFXcanvas1 Canvas;

typedef Adafruit_SH1106G yoDisplay;

#include "tools/commongfx.h"

#if __has_include("conf/displaySH1106conf_custom.h")
  #include "conf/displaySH1106conf_custom.h"
#else
  #include "conf/displaySH1106conf.h"
#endif

/*
 * OLED COLORS
 */
#define BOOT_PRG_COLOR    SH110X_WHITE
#define BOOT_TXT_COLOR    SH110X_WHITE
#define PINK              SH110X_WHITE
#define SILVER            SH110X_WHITE
#define TFT_BG            SH110X_BLACK
#define TFT_FG            SH110X_WHITE
#define TFT_LOGO          SH110X_WHITE

#endif
