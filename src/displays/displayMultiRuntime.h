#ifndef VOXONE_DISPLAY_MULTI_RUNTIME_H
#define VOXONE_DISPLAY_MULTI_RUNTIME_H

#include "Arduino.h"
#include <Adafruit_GFX.h>
#include "../Adafruit_ST7796S/Adafruit_ST7796S_kbv.h"
#include "fonts/dsfont70.h"
#define VOXONE_OLED_BACKEND_ONLY 1
#include "displayOLED128x64Runtime.h"
#undef VOXONE_OLED_BACKEND_ONLY
#include "display_backend_selection.h"

// The widget layer keeps its existing RGB565 canvas on color displays. OLED output is
// reduced to BLACK/WHITE only at the selected backend boundary.
typedef GFXcanvas16 Canvas;

class MultiDisplayRuntime : public Adafruit_GFX {
 public:
  MultiDisplayRuntime();
  ~MultiDisplayRuntime();

  bool begin(const voxone::display_backend::Selection& selection);
  bool ready() const { return ready_; }
  voxone::display_profile::ControllerId activeController() const {
    return selection_.controller;
  }
  voxone::display_profile::ProfileId activeProfile() const {
    return selection_.profile;
  }
  void flushSelected(bool force = false);
  // Boot handoff safety path for the immediate-mode ST7796 backend.  This
  // deliberately bypasses the runtime facade and any buffered/dirty flush
  // policy so the physical panel is known to have accepted a full frame.
  bool forceTftFillScreen(uint16_t color);
  void sleepSelected();
  void wakeSelected();

  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void startWrite() override;
  void writePixel(int16_t x, int16_t y, uint16_t color) override;
  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                     uint16_t color) override;
  void writeFastVLine(int16_t x, int16_t y, int16_t h,
                      uint16_t color) override;
  void writeFastHLine(int16_t x, int16_t y, int16_t w,
                      uint16_t color) override;
  void endWrite() override;
  void setRotation(uint8_t rotation) override;
  void invertDisplay(bool inverted) override;
  void drawFastVLine(int16_t x, int16_t y, int16_t h,
                     uint16_t color) override;
  void drawFastHLine(int16_t x, int16_t y, int16_t w,
                     uint16_t color) override;
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t color) override;
  void fillScreen(uint16_t color) override;

  // Used by the existing PSRAM framebuffer path on the ST7796 renderer.
  void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void writePixels(uint16_t* colors, uint32_t len);

 private:
  bool oledActive() const;
  uint16_t backendColor(uint16_t color) const;
  Adafruit_GFX* active();

  Adafruit_ST7796S_kbv* st7796_ = nullptr;
  RuntimeOled128x64Display* oled_ = nullptr;
  voxone::display_backend::Selection selection_{};
  bool ready_ = false;
};

typedef MultiDisplayRuntime yoDisplay;

#include "tools/commongfx.h"

#if __has_include("conf/displayST7796conf_custom.h")
  #include "conf/displayST7796conf_custom.h"
#else
  #include "conf/displayST7796conf.h"
#endif

#endif
