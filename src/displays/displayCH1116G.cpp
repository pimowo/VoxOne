#include "displayCH1116G.h"

#include <algorithm>

bool Ch1116gWireTransport::probe(uint8_t address) {
  wire_.beginTransmission(address);
  return wire_.endTransmission() == 0;
}

bool Ch1116gWireTransport::write(uint8_t address, uint8_t control,
                                 const uint8_t* data, size_t size) {
  if (!data || size == 0) return false;
  wire_.beginTransmission(address);
  if (wire_.write(control) != 1 || wire_.write(data, size) != size) {
    wire_.endTransmission();
    return false;
  }
  return wire_.endTransmission() == 0;
}

void Ch1116gWireTransport::delayMilliseconds(uint16_t milliseconds) {
  delay(milliseconds);
}

DisplayCH1116G::DisplayCH1116G(TwoWire& wire)
    : Adafruit_GFX(voxone::displays::ch1116g::kWidth,
                   voxone::displays::ch1116g::kHeight),
      transport_(wire),
      driver_(transport_) {}

bool DisplayCH1116G::begin(uint8_t address) {
  voxone::displays::ch1116g::Config config;
  config.address = address;
  return driver_.begin(config);
}

bool DisplayCH1116G::display() { return driver_.display(); }
bool DisplayCH1116G::sleep() { return driver_.sleep(); }
bool DisplayCH1116G::wake() { return driver_.wake(); }

void DisplayCH1116G::invertDisplay(bool inverted) {
  driver_.invert(inverted);
}

void DisplayCH1116G::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || x >= width() || y < 0 || y >= height()) return;
  switch (getRotation()) {
    case 1:
      std::swap(x, y);
      x = WIDTH - x - 1;
      break;
    case 2:
      x = WIDTH - x - 1;
      y = HEIGHT - y - 1;
      break;
    case 3:
      std::swap(x, y);
      y = HEIGHT - y - 1;
      break;
    default:
      break;
  }
  using voxone::displays::ch1116g::PixelMode;
  driver_.drawPixel(x, y, color == 0 ? PixelMode::Black
                                     : color == 1 ? PixelMode::White
                                                  : PixelMode::Invert);
}

void DisplayCH1116G::fillScreen(uint16_t color) {
  if (color == 0 || color == 1) driver_.clear(color == 1);
  else {
    uint8_t* buffer = driver_.buffer();
    if (!buffer) return;
    for (size_t index = 0; index < voxone::displays::ch1116g::kBufferSize;
         ++index)
      buffer[index] = static_cast<uint8_t>(~buffer[index]);
  }
}
