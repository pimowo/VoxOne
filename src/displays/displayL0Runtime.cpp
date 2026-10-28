#include "../core/options.h"

#if defined(VOXONE_RUNTIME_L0_DISPLAY_BACKENDS)

#include "dspcore.h"
#include "../core/config.h"
#include "../core/rtcsupport.h"
#include "../hardware/hardware_descriptor.h"
#include "../hardware/runtime_hardware_config.h"
#include <new>

using voxone::display_profile::ControllerId;
using voxone::display_profile::ProfileId;

RuntimeL0Display::RuntimeL0Display() : Adafruit_GFX(480, 320) {}

RuntimeL0Display::~RuntimeL0Display() {
  delete st7796_;
  delete oled_;
}

bool RuntimeL0Display::oledActive() const {
  return selection_.profile == ProfileId::Oled128x32 ||
         selection_.profile == ProfileId::Oled128x64;
}

uint16_t RuntimeL0Display::backendColor(uint16_t color) const {
  return oledActive() ? (color == 0 ? 0 : 1) : color;
}

Adafruit_GFX* RuntimeL0Display::active() {
  if (st7796_) return st7796_;
  return oled_;
}

bool RuntimeL0Display::begin(
    const voxone::display_backend::Selection& selection) {
  selection_ = selection;
  ready_ = false;
  if (selection_.controller == ControllerId::St7796) {
    const auto& pins = voxone::hardware::currentHardware().display;
    st7796_ = new (std::nothrow) Adafruit_ST7796S_kbv(
        pins.cs, pins.dc, pins.rst);
    if (!st7796_) return false;
    st7796_->begin();
    ready_ = true;
  } else if (oledActive()) {
    // RTC and OLED share the documented L0 I2C bus, including its lock and
    // controller instance.  No second TwoWire(0) owner is created.
    oled_ = new (std::nothrow) RuntimeOled128x64Display(RTCWire);
    if (!oled_) return false;
    ready_ = oled_->begin(selection_.controller, selection_.profile);
  }
  const auto geometry = voxone::display_profile::descriptor(selection_.profile);
  _width = static_cast<int16_t>(geometry.width);
  _height = static_cast<int16_t>(geometry.height);
  return ready_;
}

void RuntimeL0Display::flushSelected() {
  if (ready_ && oled_) oled_->display();
}

void RuntimeL0Display::sleepSelected() {
  if (!ready_) return;
  if (oled_) oled_->sleep();
  else {
    st7796_->sendCommand(ST7796S_SLPIN);
    delay(150);
    st7796_->sendCommand(ST7796S_DISPOFF);
    delay(150);
  }
}

void RuntimeL0Display::wakeSelected() {
  if (!ready_) return;
  if (oled_) oled_->wake();
  else {
    st7796_->sendCommand(ST7796S_DISPON);
    delay(150);
    st7796_->sendCommand(ST7796S_SLPOUT);
    delay(150);
  }
}

void RuntimeL0Display::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (ready_) active()->drawPixel(x, y, backendColor(color));
}
void RuntimeL0Display::startWrite() { if (ready_) active()->startWrite(); }
void RuntimeL0Display::writePixel(int16_t x, int16_t y, uint16_t color) {
  if (ready_) active()->writePixel(x, y, backendColor(color));
}
void RuntimeL0Display::writeFillRect(int16_t x, int16_t y, int16_t w,
                                     int16_t h, uint16_t color) {
  if (ready_) active()->writeFillRect(x, y, w, h, backendColor(color));
}
void RuntimeL0Display::writeFastVLine(int16_t x, int16_t y, int16_t h,
                                      uint16_t color) {
  if (ready_) active()->writeFastVLine(x, y, h, backendColor(color));
}
void RuntimeL0Display::writeFastHLine(int16_t x, int16_t y, int16_t w,
                                      uint16_t color) {
  if (ready_) active()->writeFastHLine(x, y, w, backendColor(color));
}
void RuntimeL0Display::endWrite() { if (ready_) active()->endWrite(); }
void RuntimeL0Display::setRotation(uint8_t rotation) {
  Adafruit_GFX::setRotation(rotation);
  if (ready_) {
    active()->setRotation(rotation);
    _width = active()->width();
    _height = active()->height();
  }
}
void RuntimeL0Display::invertDisplay(bool inverted) {
  if (ready_) active()->invertDisplay(inverted);
}
void RuntimeL0Display::drawFastVLine(int16_t x, int16_t y, int16_t h,
                                     uint16_t color) {
  if (ready_) active()->drawFastVLine(x, y, h, backendColor(color));
}
void RuntimeL0Display::drawFastHLine(int16_t x, int16_t y, int16_t w,
                                     uint16_t color) {
  if (ready_) active()->drawFastHLine(x, y, w, backendColor(color));
}
void RuntimeL0Display::fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                                uint16_t color) {
  if (ready_) active()->fillRect(x, y, w, h, backendColor(color));
}
void RuntimeL0Display::fillScreen(uint16_t color) {
  if (ready_) active()->fillScreen(backendColor(color));
}
void RuntimeL0Display::setAddrWindow(uint16_t x, uint16_t y, uint16_t w,
                                     uint16_t h) {
  if (ready_ && st7796_) st7796_->setAddrWindow(x, y, w, h);
}
void RuntimeL0Display::writePixels(uint16_t* colors, uint32_t len) {
  if (ready_ && st7796_) st7796_->writePixels(colors, len);
}

DspCore::DspCore() : RuntimeL0Display() {}

void DspCore::initDisplay() {
  const auto selection = voxone::display_backend::select(
      voxone::hardware::runtimeHardwareConfig(),
      voxone::hardware::currentHardware(),
      voxone::display_backend::compiledBackends());
  if (!begin(selection)) {
    Serial.println(F("Runtime L0 display backend initialization failed"));
    return;
  }
  if (activeProfile() != ProfileId::St7796_480x320) {
    #define TFT_BG 0
    #define TFT_FG 1
    #include "tools/oledcolorfix.h"
    #undef TFT_BG
    #undef TFT_FG
  }
  cp437(true);
  flip();
  invert();
  setTextWrap(false);
  setTextSize(1);
  fillScreen(0x0000);
}

void DspCore::clearDsp(bool black) {
  fillScreen(black || activeProfile() != ProfileId::St7796_480x320
                 ? 0 : config.theme.background);
}
void DspCore::flip() {
  if (activeProfile() == ProfileId::St7796_480x320)
    setRotation(config.runtimeSnapshot().flipScreen ? 1 : 3);
  else
    setRotation(config.runtimeSnapshot().flipScreen ? 2 : 0);
}
void DspCore::invert() {
  invertDisplay(config.runtimeSnapshot().invertDisplay);
}
void DspCore::sleep() { sleepSelected(); }
void DspCore::wake() { wakeSelected(); }

#endif
