#include "../core/options.h"
#if DSP_MODEL==DSP_ST7789 || DSP_MODEL==DSP_ST7789_76
#include "dspcore.h"
#include "../core/config.h"
#include "../hardware/hardware_descriptor.h"

#if DSP_HSPI
DspCore::DspCore(): Adafruit_ST7789(&SPI2,
  voxone::hardware::currentHardware().display.cs,
  voxone::hardware::currentHardware().display.dc,
  voxone::hardware::currentHardware().display.rst) {}
#else
DspCore::DspCore(): Adafruit_ST7789(
  voxone::hardware::currentHardware().display.cs,
  voxone::hardware::currentHardware().display.dc,
  voxone::hardware::currentHardware().display.rst) {}
#endif

void DspCore::initDisplay() {
  if(VOXONE_DISPLAY_PROFILE == VOXONE_DISPLAY_PROFILE_ST7789_284X76){
    init(76,284);
  }else{
    init(240,320);
  }
  invert();
  cp437(true);
  flip();
  setTextWrap(false);
  setTextSize(1);
  fillScreen(0x0000);
}

void DspCore::clearDsp(bool black){ fillScreen(black?0:config.theme.background); }
void DspCore::flip(){
  setRotation(config.store.flipscreen?3:1);
}
void DspCore::invert(){ invertDisplay(config.store.invertdisplay); }
void DspCore::sleep(void){ enableSleep(true); delay(150); enableDisplay(false); delay(150); }
void DspCore::wake(void){ enableDisplay(true); delay(150); enableSleep(false); delay(150); }

#endif
