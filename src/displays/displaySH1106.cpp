#include "../core/options.h"
#if DSP_MODEL==DSP_SH1106
#include "dspcore.h"
#include <Wire.h>
#include "../core/config.h"

#ifndef SCREEN_ADDRESS
  #define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; scan the I2C bus if needed: https://create.arduino.cc/projecthub/abdularbi17/how-to-scan-i2c-address-in-arduino-eaadda
#endif

#ifndef I2CFREQ_HZ
  #define I2CFREQ_HZ   4000000UL
#endif

TwoWire I2CSH1106 = TwoWire(0);
DspCore::DspCore(): Adafruit_SH1106G(128, 64, &I2CSH1106, -1, I2CFREQ_HZ) {

}

void DspCore::initDisplay() {
  I2CSH1106.begin(I2C_SDA, I2C_SCL);
  if (!begin(SCREEN_ADDRESS, true)) {
    Serial.println(F("SH110X allocation failed"));
    for (;;); // Don't proceed, loop forever
  }
#include "tools/oledcolorfix.h"
  cp437(true);
  flip();
  invert();
  setTextWrap(false);
}

void DspCore::clearDsp(bool black){ fillScreen(TFT_BG); }
void DspCore::flip(){
  setRotation(config.store.flipscreen?2:0);
}
void DspCore::invert(){ invertDisplay(config.store.invertdisplay); }
void DspCore::sleep(void){ oled_command(SH110X_DISPLAYOFF); }
void DspCore::wake(void){ oled_command(SH110X_DISPLAYON); }

#endif
