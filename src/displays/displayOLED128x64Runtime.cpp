#include "../core/options.h"

#if VOXONE_MULTI_DISPLAY_RUNTIME

#include "dspcore.h"
#include "../core/config.h"
#include "../hardware/hardware_descriptor.h"
#include "../hardware/i2c_runtime.h"
#include "../hardware/runtime_hardware_config.h"
#include <new>

using voxone::display_profile::ControllerId;

RuntimeOled128x64Display::RuntimeOled128x64Display(TwoWire& sharedWire)
    : Adafruit_GFX(128, 64),
      wire_(sharedWire),
      ssd1306_128x32_(
          128, 32, &wire_,
          voxone::hardware::currentHardware().display.rst,
          voxone::hardware::currentHardware().i2c.frequencyHz),
      ssd1306_128x64_(
          128, 64, &wire_,
          voxone::hardware::currentHardware().display.rst,
          voxone::hardware::currentHardware().i2c.frequencyHz),
      ssd1309_(128, 64, &wire_,
               voxone::hardware::currentHardware().display.rst),
      sh1106_(128, 64, &wire_,
              voxone::hardware::currentHardware().display.rst, 400000UL) {}

RuntimeOled128x64Display::~RuntimeOled128x64Display() { delete ch1116g_; }

Adafruit_GFX* RuntimeOled128x64Display::active() {
  if (activeController_ == ControllerId::Ssd1306) return activeSsd1306();
  if (activeController_ == ControllerId::Ssd1309) return &ssd1309_;
  if (activeController_ == ControllerId::Sh1106) return &sh1106_;
  if (activeController_ == ControllerId::Ch1116g) return ch1116g_;
  return nullptr;
}

const Adafruit_GFX* RuntimeOled128x64Display::active() const {
  if (activeController_ == ControllerId::Ssd1306)
    return activeProfile_ == voxone::display_profile::ProfileId::Oled128x32
               ? static_cast<const Adafruit_GFX*>(&ssd1306_128x32_)
               : static_cast<const Adafruit_GFX*>(&ssd1306_128x64_);
  if (activeController_ == ControllerId::Ssd1309) return &ssd1309_;
  if (activeController_ == ControllerId::Sh1106) return &sh1106_;
  if (activeController_ == ControllerId::Ch1116g) return ch1116g_;
  return nullptr;
}

Adafruit_SSD1306* RuntimeOled128x64Display::activeSsd1306() {
  return activeProfile_ == voxone::display_profile::ProfileId::Oled128x32
             ? &ssd1306_128x32_ : &ssd1306_128x64_;
}

bool RuntimeOled128x64Display::begin(
    ControllerId controller, voxone::display_profile::ProfileId profile) {
  activeController_ = controller;
  activeProfile_ = profile;
  ready_ = false;
  dirty_ = true;
  const auto& i2c = voxone::hardware::currentHardware().i2c;
  if (!voxone::hardware::initHardwareI2c()) return false;
  wire_.setClock(i2c.frequencyHz);
  switch (activeController_) {
    case ControllerId::Ssd1306:
      ready_ = activeSsd1306()->begin(SSD1306_SWITCHCAPVCC, i2c.displayAddress,
                                     true, false);
      break;
    case ControllerId::Ssd1309:
      ready_ = ssd1309_.begin(i2c.displayAddress);
      break;
    case ControllerId::Sh1106:
      ready_ = sh1106_.begin(i2c.displayAddress, true);
      break;
    case ControllerId::Ch1116g:
      wire_.setClock(400000UL);
      ch1116g_ = new (std::nothrow) DisplayCH1116G(wire_);
      ready_ = ch1116g_ && ch1116g_->begin(i2c.displayAddress);
      break;
    default:
      break;
  }
  return ready_;
}

void RuntimeOled128x64Display::display() {
  if (!ready_ || !dirty_) return;
  if (activeController_ == ControllerId::Ssd1306) activeSsd1306()->display();
  else if (activeController_ == ControllerId::Ssd1309) ssd1309_.display();
  else if (activeController_ == ControllerId::Sh1106) sh1106_.display();
  else if (activeController_ == ControllerId::Ch1116g) {
    if (!ch1116g_->display()) {
      ready_ = false;
      return;
    }
  }
  dirty_ = false;
}

void RuntimeOled128x64Display::sleep() {
  if (!ready_) return;
  if (activeController_ == ControllerId::Ssd1306)
    activeSsd1306()->ssd1306_command(SSD1306_DISPLAYOFF);
  else if (activeController_ == ControllerId::Ssd1309)
    ssd1309_.sleep();
  else if (activeController_ == ControllerId::Sh1106)
    sh1106_.oled_command(SH110X_DISPLAYOFF);
  else if (activeController_ == ControllerId::Ch1116g)
    ch1116g_->sleep();
}

void RuntimeOled128x64Display::wake() {
  if (!ready_) return;
  if (activeController_ == ControllerId::Ssd1306)
    activeSsd1306()->ssd1306_command(SSD1306_DISPLAYON);
  else if (activeController_ == ControllerId::Ssd1309)
    ssd1309_.wake();
  else if (activeController_ == ControllerId::Sh1106)
    sh1106_.oled_command(SH110X_DISPLAYON);
  else if (activeController_ == ControllerId::Ch1116g)
    ch1116g_->wake();
}

void RuntimeOled128x64Display::drawPixel(int16_t x, int16_t y,
                                         uint16_t color) {
  if (ready_) {
    dirty_ = true;
    active()->drawPixel(x, y, color);
  }
}

void RuntimeOled128x64Display::startWrite() {
  if (ready_) {
    dirty_ = true;
    active()->startWrite();
  }
}

void RuntimeOled128x64Display::writePixel(int16_t x, int16_t y,
                                          uint16_t color) {
  if (ready_) {
    dirty_ = true;
    active()->writePixel(x, y, color);
  }
}

void RuntimeOled128x64Display::writeFillRect(int16_t x, int16_t y, int16_t w,
                                             int16_t h, uint16_t color) {
  if (ready_) {
    dirty_ = true;
    active()->writeFillRect(x, y, w, h, color);
  }
}

void RuntimeOled128x64Display::writeFastVLine(int16_t x, int16_t y, int16_t h,
                                              uint16_t color) {
  if (ready_) {
    dirty_ = true;
    active()->writeFastVLine(x, y, h, color);
  }
}

void RuntimeOled128x64Display::writeFastHLine(int16_t x, int16_t y, int16_t w,
                                              uint16_t color) {
  if (ready_) {
    dirty_ = true;
    active()->writeFastHLine(x, y, w, color);
  }
}

void RuntimeOled128x64Display::endWrite() {
  if (ready_) active()->endWrite();
}

void RuntimeOled128x64Display::setRotation(uint8_t rotation) {
  Adafruit_GFX::setRotation(rotation);
  if (Adafruit_GFX* backend = active()) backend->setRotation(rotation);
  dirty_ = true;
  const auto geometry = voxone::display_profile::descriptor(activeProfile_);
  if ((rotation & 1u) == 0) {
    _width = static_cast<int16_t>(geometry.width);
    _height = static_cast<int16_t>(geometry.height);
  } else {
    _width = static_cast<int16_t>(geometry.height);
    _height = static_cast<int16_t>(geometry.width);
  }
}

void RuntimeOled128x64Display::invertDisplay(bool inverted) {
  if (ready_) active()->invertDisplay(inverted);
}

void RuntimeOled128x64Display::drawFastVLine(int16_t x, int16_t y, int16_t h,
                                             uint16_t color) {
  if (ready_) {
    dirty_ = true;
    active()->drawFastVLine(x, y, h, color);
  }
}

void RuntimeOled128x64Display::drawFastHLine(int16_t x, int16_t y, int16_t w,
                                             uint16_t color) {
  if (ready_) {
    dirty_ = true;
    active()->drawFastHLine(x, y, w, color);
  }
}

void RuntimeOled128x64Display::fillRect(int16_t x, int16_t y, int16_t w,
                                        int16_t h, uint16_t color) {
  if (ready_) {
    dirty_ = true;
    active()->fillRect(x, y, w, h, color);
  }
}

void RuntimeOled128x64Display::fillScreen(uint16_t color) {
  if (ready_) {
    dirty_ = true;
    active()->fillScreen(color);
  }
}

#if !VOXONE_MULTI_DISPLAY_HAS_ST7796
DspCore::DspCore()
    : RuntimeOled128x64Display(voxone::hardware::hardwareI2cBus()) {}

void DspCore::initDisplay() {
  const auto selection = voxone::display_backend::select(
      voxone::hardware::runtimeHardwareConfig(),
      voxone::hardware::currentHardware(),
      voxone::display_backend::compiledBackends(
          voxone::hardware::currentHardware()));
  if (!begin(selection.controller, selection.profile))
    Serial.println(F("Runtime OLED backend initialization failed; display disabled"));
#include "tools/oledcolorfix.h"
  cp437(true);
  flip();
  invert();
  setTextWrap(false);
}

void DspCore::clearDsp(bool black) { fillScreen(TFT_BG); }
void DspCore::flip() { setRotation(config.runtimeSnapshot().flipScreen ? 2 : 0); }
void DspCore::invert() { invertDisplay(config.runtimeSnapshot().invertDisplay); }
void DspCore::sleep() { RuntimeOled128x64Display::sleep(); }
void DspCore::wake() { RuntimeOled128x64Display::wake(); }
#endif

#endif
