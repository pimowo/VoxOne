#ifndef VOXONE_DISPLAY_OLED128X64_RUNTIME_H
#define VOXONE_DISPLAY_OLED128X64_RUNTIME_H

#include "Arduino.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#undef BLACK
#undef WHITE
#include <Adafruit_SSD1305.h>
#include <Wire.h>

#include "display_backend_selection.h"
#include "fonts/bootlogo21x32.h"

class RuntimeOled128x64Display : public Adafruit_GFX {
 public:
  RuntimeOled128x64Display();

  bool begin(voxone::display_profile::ControllerId controller);
  void display();
  void sleep();
  void wake();
  bool ready() const { return ready_; }
  voxone::display_profile::ControllerId activeController() const {
    return activeController_;
  }

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

 private:
  Adafruit_GFX* active();
  const Adafruit_GFX* active() const;

  TwoWire wire_;
  Adafruit_SSD1306 ssd1306_;
  // The existing, physically verified SSD1309 path uses Adafruit's
  // SSD1305-compatible controller implementation.
  Adafruit_SSD1305 ssd1309_;
  voxone::display_profile::ControllerId activeController_ =
      voxone::display_profile::ControllerId::None;
  bool ready_ = false;
};

typedef GFXcanvas1 Canvas;
typedef RuntimeOled128x64Display yoDisplay;

#include "tools/commongfx.h"

#if __has_include("conf/displaySSD1306conf_custom.h")
  #include "conf/displaySSD1306conf_custom.h"
#else
  #include "conf/displaySSD1306conf.h"
#endif

#define BOOT_PRG_COLOR WHITE
#define BOOT_TXT_COLOR WHITE
#define PINK WHITE
#define SILVER WHITE
#define TFT_BG BLACK
#define TFT_FG WHITE
#define TFT_LOGO WHITE

#endif
