#include "../core/options.h"

#if defined(VOXONE_RUNTIME_OLED_128X64_BACKENDS)

#include "dspcore.h"
#include "../core/config.h"
#include "../hardware/runtime_hardware_config.h"

#ifndef SCREEN_ADDRESS
  #define SCREEN_ADDRESS 0x3C
#endif

#ifndef I2CFREQ_HZ
  #define I2CFREQ_HZ 4000000UL
#endif

using voxone::display_profile::ControllerId;

RuntimeOled128x64Display::RuntimeOled128x64Display()
    : Adafruit_GFX(128, 64),
      wire_(0),
      ssd1306_(128, 64, &wire_, I2C_RST, I2CFREQ_HZ),
      ssd1309_(128, 64, &wire_, I2C_RST) {}

Adafruit_GFX* RuntimeOled128x64Display::active() {
  if (activeController_ == ControllerId::Ssd1306) return &ssd1306_;
  if (activeController_ == ControllerId::Ssd1309) return &ssd1309_;
  return nullptr;
}

const Adafruit_GFX* RuntimeOled128x64Display::active() const {
  if (activeController_ == ControllerId::Ssd1306) return &ssd1306_;
  if (activeController_ == ControllerId::Ssd1309) return &ssd1309_;
  return nullptr;
}

bool RuntimeOled128x64Display::begin(ControllerId controller) {
  activeController_ = controller;
  ready_ = false;
  wire_.begin(I2C_SDA, I2C_SCL);
  switch (activeController_) {
    case ControllerId::Ssd1306:
      ready_ = ssd1306_.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS, true,
                              false);
      break;
    case ControllerId::Ssd1309:
      ready_ = ssd1309_.begin(SCREEN_ADDRESS);
      break;
    default:
      break;
  }
  return ready_;
}

void RuntimeOled128x64Display::display() {
  if (!ready_) return;
  if (activeController_ == ControllerId::Ssd1306) ssd1306_.display();
  else if (activeController_ == ControllerId::Ssd1309) ssd1309_.display();
}

void RuntimeOled128x64Display::sleep() {
  if (!ready_) return;
  if (activeController_ == ControllerId::Ssd1306)
    ssd1306_.ssd1306_command(SSD1306_DISPLAYOFF);
  else if (activeController_ == ControllerId::Ssd1309)
    ssd1309_.sleep();
}

void RuntimeOled128x64Display::wake() {
  if (!ready_) return;
  if (activeController_ == ControllerId::Ssd1306)
    ssd1306_.ssd1306_command(SSD1306_DISPLAYON);
  else if (activeController_ == ControllerId::Ssd1309)
    ssd1309_.wake();
}

void RuntimeOled128x64Display::drawPixel(int16_t x, int16_t y,
                                         uint16_t color) {
  if (ready_) active()->drawPixel(x, y, color);
}

void RuntimeOled128x64Display::startWrite() {
  if (ready_) active()->startWrite();
}

void RuntimeOled128x64Display::writePixel(int16_t x, int16_t y,
                                          uint16_t color) {
  if (ready_) active()->writePixel(x, y, color);
}

void RuntimeOled128x64Display::writeFillRect(int16_t x, int16_t y, int16_t w,
                                             int16_t h, uint16_t color) {
  if (ready_) active()->writeFillRect(x, y, w, h, color);
}

void RuntimeOled128x64Display::writeFastVLine(int16_t x, int16_t y, int16_t h,
                                              uint16_t color) {
  if (ready_) active()->writeFastVLine(x, y, h, color);
}

void RuntimeOled128x64Display::writeFastHLine(int16_t x, int16_t y, int16_t w,
                                              uint16_t color) {
  if (ready_) active()->writeFastHLine(x, y, w, color);
}

void RuntimeOled128x64Display::endWrite() {
  if (ready_) active()->endWrite();
}

void RuntimeOled128x64Display::setRotation(uint8_t rotation) {
  Adafruit_GFX::setRotation(rotation);
  if (Adafruit_GFX* backend = active()) backend->setRotation(rotation);
}

void RuntimeOled128x64Display::invertDisplay(bool inverted) {
  if (ready_) active()->invertDisplay(inverted);
}

void RuntimeOled128x64Display::drawFastVLine(int16_t x, int16_t y, int16_t h,
                                             uint16_t color) {
  if (ready_) active()->drawFastVLine(x, y, h, color);
}

void RuntimeOled128x64Display::drawFastHLine(int16_t x, int16_t y, int16_t w,
                                             uint16_t color) {
  if (ready_) active()->drawFastHLine(x, y, w, color);
}

void RuntimeOled128x64Display::fillRect(int16_t x, int16_t y, int16_t w,
                                        int16_t h, uint16_t color) {
  if (ready_) active()->fillRect(x, y, w, h, color);
}

void RuntimeOled128x64Display::fillScreen(uint16_t color) {
  if (ready_) active()->fillScreen(color);
}

DspCore::DspCore() : RuntimeOled128x64Display() {}

void DspCore::initDisplay() {
  const auto selection = voxone::display_backend::select(
      voxone::hardware::runtimeHardwareConfig(),
      voxone::hardware::currentHardware(),
      voxone::display_backend::compiledBackends());
  if (!begin(selection.controller))
    Serial.println(F("Runtime OLED backend initialization failed; display disabled"));
#include "tools/oledcolorfix.h"
  cp437(true);
  flip();
  invert();
  setTextWrap(false);
}

void DspCore::clearDsp(bool black) { fillScreen(TFT_BG); }
void DspCore::flip() { setRotation(config.store.flipscreen ? 2 : 0); }
void DspCore::invert() { invertDisplay(config.store.invertdisplay); }
void DspCore::sleep() { RuntimeOled128x64Display::sleep(); }
void DspCore::wake() { RuntimeOled128x64Display::wake(); }

#endif
