#ifndef dspcore_h
#define dspcore_h
#pragma once

#if VOXONE_MULTI_DISPLAY_HAS_ST7796
  #define TIME_SIZE           70
  #define PSFBUFFER
  #include "displayMultiRuntime.h"

#elif VOXONE_MULTI_DISPLAY_RUNTIME
  #define TIME_SIZE           2
  #define DSP_OLED
  #include "displayOLED128x64Runtime.h"

#elif DSP_MODEL==DSP_DUMMY
  #define DUMMYDISPLAY
  #define DSP_NOT_FLIPPED

#elif DSP_MODEL==DSP_SSD1306
  #define TIME_SIZE           2
  #define DSP_OLED
  #include "displaySSD1306.h"

#elif DSP_MODEL==DSP_ST7789
  #define TIME_SIZE           52
  #define PSFBUFFER
  #include "displayST7789.h"

#elif DSP_MODEL==DSP_ST7789_76
  #define TIME_SIZE           35
  #define PSFBUFFER
  #include "displayST7789.h"

#elif DSP_MODEL==DSP_SH1106
  #define TIME_SIZE           2
  #define DSP_OLED
  #include "displaySH1106.h"

#elif DSP_MODEL==DSP_SSD1305 || DSP_MODEL==DSP_SSD1305I2C
  #define TIME_SIZE           2
  #define DSP_OLED
  #include "displaySSD1305.h"

#elif DSP_MODEL==DSP_CUSTOM
  #define TIME_SIZE           0
  #include "displayCustom.h"

#elif DSP_MODEL==DSP_ST7796
  #define TIME_SIZE           70
  #define PSFBUFFER
  #include "displayST7796.h"

#elif DSP_MODEL==DSP_GC9A01A
  #define TIME_SIZE           52
  #define PSFBUFFER
  #include "displayGC9A01A.h"

#elif DSP_MODEL==DSP_SSD1322
  #define TIME_SIZE           35
  #define DSP_OLED
  #include "displaySSD1322.h"

#elif DSP_MODEL==DSP_ST7920
  #define TIME_SIZE           2
  #define DSP_OLED
  #include "displayST7920.h"

#endif

//extern DspCore dsp;

#endif
