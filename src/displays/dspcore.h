#ifndef dspcore_h
#define dspcore_h
#pragma once

#if DSP_MODEL==DSP_DUMMY
  #define DUMMYDISPLAY
  #define DSP_NOT_FLIPPED

#elif DSP_MODEL==DSP_ST7735    // https://k210.org/images/content/uploads/yoradio/ST7735.jpg
  #define TIME_SIZE           35
  #define PSFBUFFER
  #include "displayST7735.h"

#elif DSP_MODEL==DSP_SSD1306    // https://k210.org/images/content/uploads/yoradio/SSD1306.jpg
  #define TIME_SIZE           2
  #define DSP_OLED
  #include "displaySSD1306.h"

#elif DSP_MODEL==DSP_SSD1306x32    // https://k210.org/images/content/uploads/yoradio/SSD1306x32.jpg
  #define TIME_SIZE           1
  #define DSP_OLED
  #include "displaySSD1306.h"

#elif DSP_MODEL==DSP_ST7789 || DSP_MODEL==DSP_ST7789_240 || DSP_MODEL==DSP_ST7789_170  // https://k210.org/images/content/uploads/yoradio/ST7789.jpg
  #define TIME_SIZE           52
  #define PSFBUFFER
  #include "displayST7789.h"

#elif DSP_MODEL==DSP_ST7789_76         // https://k210.org/images/content/uploads/yoradio/ST7789_76.mp4
  #define TIME_SIZE           35
  #define PSFBUFFER
  #include "displayST7789.h"

#elif DSP_MODEL==DSP_SH1106 || DSP_MODEL==DSP_SH1107    // https://k210.org/images/content/uploads/yoradio/SH1106.jpg
  #define TIME_SIZE           2
  #define DSP_OLED
  #include "displaySH1106.h"

#elif DSP_MODEL==DSP_SSD1305 || DSP_MODEL==DSP_SSD1305I2C   // https://k210.org/images/content/uploads/yoradio/SSD1305.jpg
  #define TIME_SIZE           2
  #define DSP_OLED
  #include "displaySSD1305.h"

#elif DSP_MODEL==DSP_CUSTOM
  #define TIME_SIZE           0
  #include "displayCustom.h"

#elif DSP_MODEL==DSP_ST7796         // https://k210.org/images/content/uploads/yoradio/ST7796.jpg
  #define TIME_SIZE           70
  #define PSFBUFFER
  #include "displayST7796.h"

#elif DSP_MODEL==DSP_GC9A01A         // https://k210.org/images/content/uploads/yoradio/GC9A01A.jpg
  #define TIME_SIZE           52
  #define PSFBUFFER
  #include "displayGC9A01A.h"

#elif DSP_MODEL==DSP_SSD1322        // https://k210.org/images/content/uploads/yoradio/ssd1322.mp4
  #define TIME_SIZE           35
  #define DSP_OLED
  #include "displaySSD1322.h"

#elif DSP_MODEL==DSP_ST7920         // https://k210.org/images/content/uploads/yoradio/ST7920.jpg
  #define TIME_SIZE           2
  #define DSP_OLED
  #include "displayST7920.h"

#endif

//extern DspCore dsp;

#endif
