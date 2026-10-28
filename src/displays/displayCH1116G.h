#ifndef VOXONE_DISPLAY_CH1116G_H
#define VOXONE_DISPLAY_CH1116G_H

#include <Adafruit_GFX.h>
#include <Wire.h>

#include "ch1116g_driver.h"

class Ch1116gWireTransport : public voxone::displays::ch1116g::Transport {
 public:
  explicit Ch1116gWireTransport(TwoWire& wire) : wire_(wire) {}

  bool probe(uint8_t address) override;
  bool write(uint8_t address, uint8_t control,
             const uint8_t* data, size_t size) override;
  void delayMilliseconds(uint16_t milliseconds) override;

 private:
  TwoWire& wire_;
};

class DisplayCH1116G : public Adafruit_GFX {
 public:
  explicit DisplayCH1116G(TwoWire& wire);

  bool begin(uint8_t address = voxone::displays::ch1116g::kDefaultAddress);
  bool display();
  bool sleep();
  bool wake();
  void invertDisplay(bool inverted) override;
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillScreen(uint16_t color) override;
  bool ready() const { return driver_.ready(); }

 private:
  Ch1116gWireTransport transport_;
  voxone::displays::ch1116g::Driver driver_;
};

#endif
