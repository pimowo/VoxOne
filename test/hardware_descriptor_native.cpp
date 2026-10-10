#include "../src/hardware/hardware_descriptor.h"
#include "../profiles/profile.h"

#include <cassert>
#include <cstring>
#include <cstdio>

using namespace voxone;
using namespace voxone::hardware;
using namespace voxone::display_profile;

int main() {
  const HardwareDescriptor& d = currentHardware();
  const HardwareCapabilities& caps = hardwareCapabilities();
  assert(&caps == &d.capabilities);
  assert(std::strcmp(d.board.name, VOXONE_PROFILE_NAME) == 0);
  assert(d.board.mcu == (VOXONE_PROFILE_MCU == Mcu::Esp32 ?
                         McuFamily::Esp32 : McuFamily::Esp32S3));
  assert(caps.hasLocalDisplay() == bool(VOXONE_HAS_DISPLAY));
  assert(caps.supportsInput(LocalInputKind::Ec11) == bool(VOXONE_HAS_ENCODER));
  assert(caps.supportsInput(LocalInputKind::Buttons3));
  assert(caps.supportsInput(LocalInputKind::None));
  assert(caps.supportsLocalUi == bool(VOXONE_HAS_LOCAL_UI));
  assert(caps.hasLocalInput() ==
         (hasPin(d.localInput.a) && hasPin(d.localInput.b) &&
          hasPin(d.localInput.key)));
  assert(caps.supportsVoxOneBt == bool(VOXONE_HAS_BT));
  assert(caps.supportsVoxOneBt ==
         (hasPin(d.btUart.rx) && hasPin(d.btUart.tx)));
  assert(caps.supportsAudioOutput(AudioOutputKind::Pcm5102a));
  assert(!caps.supportsAudioOutput(AudioOutputKind::Max98357));
  assert(!caps.supportsAudioOutput(AudioOutputKind::DspMini));
  assert(!caps.supportsDsp && !caps.supportsAux && !caps.supportsSpdif);
  assert(!caps.supportsTda7719);
  assert(caps.supportsBtAudioRx == bool(VOXONE_BT_I2S_RX_ENABLED));
  assert(caps.supportsDacHardwareMute == hasPin(d.dacXsmt));
  assert(d.audioOutputKind == AudioOutputKind::Pcm5102a);
  assert(d.audioOut.bclk == fromLegacyPin(I2S_BCLK));
  assert(d.audioOut.ws == fromLegacyPin(I2S_LRC));
  assert(d.audioOut.dout == fromLegacyPin(I2S_DOUT));
  assert(d.display.cs == fromLegacyPin(TFT_CS));
  assert(d.display.dc == fromLegacyPin(TFT_DC));
  assert(d.display.rst == fromLegacyPin(TFT_RST));
  assert((d.displayKind == DisplayKind::None) == !caps.hasLocalDisplay());
  assert(d.displayKind == DisplayKind::None || caps.supportsDisplay(d.displayKind));
  assert(activeProfile() == profileFor(d.displayKind));
  assert(activeController() == controllerFor(d.displayKind));
  const Descriptor layout = activeDescriptor();
  assert(d.display.bus ==
         ((d.displayKind == DisplayKind::Ssd1306_128x64 ||
           d.displayKind == DisplayKind::Ssd1309_128x64) ? BusKind::I2c :
          (caps.hasLocalDisplay() ? BusKind::Spi : BusKind::None)));
  assert(d.localInput.a == fromLegacyPin(ENC_BTNL));
  assert(d.localInput.b == fromLegacyPin(ENC_BTNR));
  assert(d.localInput.key == fromLegacyPin(ENC_BTNB));
  assert(!bool(ENC_INTERNALPULLUP));
  assert(!bool(ENC_BUTTON_INTERNALPULLUP));
  assert(d.localInput.ec11StepsPerDetent ==
         (ENC_HALFQUARD == 255 ? 1 : (ENC_HALFQUARD ? 2 : 4)));
  assert(d.btAudioIn.bclk == fromLegacyPin(VOXONE_BT_I2S_BCLK_PIN));
  assert(d.btAudioIn.ws == fromLegacyPin(VOXONE_BT_I2S_WS_PIN));
  assert(d.btAudioIn.din == fromLegacyPin(VOXONE_BT_I2S_DATA_PIN));
  assert(d.btAudioIn.dout == kNoPin);
  assert(d.btAudioRxEnabled == bool(VOXONE_BT_I2S_RX_ENABLED));
  assert(!d.btAudioRxEnabled ||
         (hasPin(d.btAudioIn.bclk) && hasPin(d.btAudioIn.ws) &&
          hasPin(d.btAudioIn.din)));
#if defined(VOXONE_RUNTIME_OLED_128X64_BACKENDS)
  assert(d.i2c.sda == fromLegacyPin(I2C_SDA));
  assert(d.i2c.scl == fromLegacyPin(I2C_SCL));
#else
  assert(d.i2c.sda == fromLegacyPin(RTC_SDA));
  assert(d.i2c.scl == fromLegacyPin(RTC_SCL));
#endif
#if defined(VOXONE_PROFILE_L0)
  assert(caps.supportsRtc);
#else
  assert(!caps.supportsRtc);
#endif
  assert(fromLegacyPin(255) == kNoPin && fromLegacyPin(-1) == kNoPin);
  assert(!hasPin(kNoPin));
  assert(validateDescriptor(d));
  assert(!hasPinConflicts(d));

  // A neutral no-display descriptor exercises the hardware contract without
  // introducing a product profile for a board that is no longer supported.
  HardwareDescriptor noDisplay = d;
  noDisplay.displayKind = DisplayKind::None;
  noDisplay.display = {kNoPin, kNoPin, kNoPin, kNoPin, BusKind::None};
  noDisplay.localInput = {kNoPin, kNoPin, kNoPin, 1};
  noDisplay.capabilities.supportedDisplays = 0;
  noDisplay.capabilities.supportedInputs = 0;
  noDisplay.capabilities.supportsLocalUi = false;
  assert(!noDisplay.capabilities.hasLocalDisplay());
  assert(validateDescriptor(noDisplay));
  assert(!hasPinConflicts(noDisplay));
#if defined(VOXONE_PROFILE_L0)
  assert(noDisplay.capabilities.supportsVoxOneBt);
  assert(noDisplay.capabilities.supportsBtFirmwareUpdate);
  assert(noDisplay.btUart.rx == 15 && noDisplay.btUart.tx == 16);
#endif
  noDisplay.capabilities.supportsLocalUi = true;
  assert(!validateDescriptor(noDisplay));

#if defined(VOXONE_PROFILE_W0)
  assert(d.board.id == BoardId::BoardW0);
  assert(d.board.family == 'W' && d.board.revision == 0);
  assert(std::strcmp(d.board.name, "W0") == 0);
  assert(d.board.mcu == McuFamily::Esp32);
  assert(d.displayKind == DisplayKind::St7789_284x76);
  assert(activeProfile() == ProfileId::St7789_284x76);
  assert(activeController() == ControllerId::St7789);
  assert(layout.width == 284 && layout.height == 76 && !layout.monochrome);
  assert(caps.hasLocalDisplay());
  assert(caps.supportedDisplays == displaySupport(DisplayKind::St7789_284x76));
  assert(d.display.cs == 5 && d.display.dc == 4);
  assert(d.display.rst == kNoPin && d.display.backlight == kNoPin);
  assert(d.audioOut.dout == 27 && d.audioOut.bclk == 26 && d.audioOut.ws == 25);
  assert(d.localInput.a == 33 && d.localInput.b == 35 &&
         d.localInput.key == 32);
  assert(d.localInput.ec11StepsPerDetent == 4);
  // The W0 profile does not specify default SPI bus pins.
  assert(d.spi.sck == kNoPin && d.spi.mosi == kNoPin &&
         d.spi.miso == kNoPin);
  assert(!caps.supportsVoxOneBt);
  assert(!caps.supportsBtFirmwareUpdate);
  assert(!caps.supportsPsram);
  assert(d.btUart.rx == kNoPin && d.btUart.tx == kNoPin);
  assert(d.btAudioIn.bclk == kNoPin && d.btAudioIn.ws == kNoPin &&
         d.btAudioIn.din == kNoPin && !d.btAudioRxEnabled);
  assert(d.dacXsmt == kNoPin);
#elif defined(VOXONE_PROFILE_L0)
  assert(d.board.id == BoardId::BoardL0);
  assert(d.board.family == 'L' && d.board.revision == 0);
  assert(std::strcmp(d.board.name, "L0") == 0);
  assert(d.displayKind == DisplayKind::St7796_480x320);
  assert(activeProfile() == ProfileId::St7796_480x320);
  assert(activeController() == ControllerId::St7796);
  assert(layout.width == 480 && layout.height == 320 && !layout.monochrome);
  assert(caps.hasLocalDisplay());
  assert(caps.supportedDisplays == displaySupport(DisplayKind::St7796_480x320));
  assert(d.spi.mosi == 11 && d.spi.sck == 12 && d.spi.miso == 13);
  assert(d.display.cs == 10 && d.display.dc == 9 && d.display.backlight == 14);
  assert(d.display.rst == kNoPin);
  assert(d.display.backlight == fromLegacyPin(BRIGHTNESS_PIN));
  assert(d.localInput.a == 41 && d.localInput.b == 40 &&
         d.localInput.key == 39);
  assert(d.localInput.ec11StepsPerDetent == 4);
  assert(d.audioOut.dout == 4 && d.audioOut.bclk == 5 && d.audioOut.ws == 6);
  assert(d.i2c.sda == 8 && d.i2c.scl == 7);
  assert(d.btUart.rx == 15 && d.btUart.tx == 16);
  assert(d.btUart.rx == fromLegacyPin(VOXONE_BT_UART_RX_PIN));
  assert(d.btUart.tx == fromLegacyPin(VOXONE_BT_UART_TX_PIN));
  assert(caps.supportsVoxOneBt && caps.supportsBtAudioRx && d.btAudioRxEnabled);
  assert(caps.supportsBtFirmwareUpdate);
  assert(caps.supportsRtc && caps.supportsVu && caps.supportsLocalUi);
  assert(caps.supportsPsram);
  assert(d.btAudioIn.bclk == 1 && d.btAudioIn.ws == 2 && d.btAudioIn.din == 17);
  assert(d.dacXsmt == kNoPin);
  assert(d.dacXsmt == fromLegacyPin(VOXONE_DAC_XSMT_PIN));
#elif defined(VOXONE_PROFILE_M0)
  assert(d.board.id == BoardId::BoardM0);
  assert(d.board.family == 'M' && d.board.revision == 0);
  assert(d.board.flashBytes == 4u * 1024u * 1024u);
  assert(d.board.psramBytes == 2u * 1024u * 1024u);
  assert(VOXONE_PROFILE_DISPLAY == Display::Ssd1306_128x64);
  assert(d.displayKind == DisplayKind::Ssd1306_128x64);
  assert(activeController() == ControllerId::Ssd1306);
  assert(activeProfile() == ProfileId::Oled128x64);
  assert(layout.width == 128 && layout.height == 64 && layout.monochrome);
  assert(d.display.bus == BusKind::I2c);
  assert(caps.supportedDisplays ==
         (displaySupport(DisplayKind::Ssd1306_128x64) |
          displaySupport(DisplayKind::Ssd1309_128x64)));
  assert(caps.supportsDisplay(DisplayKind::Ssd1306_128x64));
  assert(caps.supportsDisplay(DisplayKind::Ssd1309_128x64));
  assert(d.i2c.sda == 7 && d.i2c.scl == 8);
  assert(d.display.cs == kNoPin && d.display.dc == kNoPin);
  assert(d.audioOut.bclk == 1 && d.audioOut.ws == 3 && d.audioOut.dout == 2);
  assert(d.localInput.a == 5 && d.localInput.b == 6 && d.localInput.key == 4);
  assert(!bool(ENC_INTERNALPULLUP));
  assert(!bool(ENC_BUTTON_INTERNALPULLUP));
  assert(!caps.supportsRtc);
  assert(!caps.supportsVoxOneBt);
  assert(!caps.supportsBtFirmwareUpdate);
  assert(caps.supportsInput(LocalInputKind::Ec11) &&
         caps.supportsInput(LocalInputKind::Buttons3) && caps.supportsLocalUi);
  assert(caps.supportsPsram);
  assert(d.btUart.rx == kNoPin && d.btUart.tx == kNoPin);
  assert(d.btAudioIn.bclk == kNoPin && d.btAudioIn.ws == kNoPin &&
         d.btAudioIn.din == kNoPin && !d.btAudioRxEnabled);
#elif defined(VOXONE_PROFILE_S0)
  assert(d.board.id == BoardId::BoardS0);
  assert(d.board.family == 'S' && d.board.revision == 0);
  assert(d.board.flashBytes == 4u * 1024u * 1024u);
  assert(d.board.psramBytes == 2u * 1024u * 1024u);
  assert(VOXONE_PROFILE_DISPLAY == Display::Ssd1306_128x64);
  assert(d.displayKind == DisplayKind::Ssd1306_128x64);
  assert(activeController() == ControllerId::Ssd1306);
  assert(activeProfile() == ProfileId::Oled128x64);
  assert(layout.width == 128 && layout.height == 64 && layout.monochrome);
  assert(d.display.bus == BusKind::I2c);
  assert(caps.supportedDisplays ==
         (displaySupport(DisplayKind::Ssd1306_128x64) |
          displaySupport(DisplayKind::Ssd1309_128x64)));
  assert(caps.supportsDisplay(DisplayKind::Ssd1306_128x64));
  assert(caps.supportsDisplay(DisplayKind::Ssd1309_128x64));
  assert(d.i2c.sda == 7 && d.i2c.scl == 8);
  assert(d.display.cs == kNoPin && d.display.dc == kNoPin);
  assert(d.audioOut.bclk == 1 && d.audioOut.ws == 3 && d.audioOut.dout == 2);
  assert(d.localInput.a == 5 && d.localInput.b == 6 && d.localInput.key == 4);
  assert(!bool(ENC_INTERNALPULLUP));
  assert(!bool(ENC_BUTTON_INTERNALPULLUP));
  assert(!caps.supportsRtc);
  assert(!caps.supportsVoxOneBt);
  assert(!caps.supportsBtFirmwareUpdate);
  assert(caps.supportsInput(LocalInputKind::Ec11) &&
         caps.supportsInput(LocalInputKind::Buttons3) && caps.supportsLocalUi);
  assert(caps.supportsPsram);
  assert(d.btUart.rx == kNoPin && d.btUart.tx == kNoPin);
  assert(d.btAudioIn.bclk == kNoPin && d.btAudioIn.ws == kNoPin &&
         d.btAudioIn.din == kNoPin && !d.btAudioRxEnabled);
#elif defined(VOXONE_PROFILE_DIN0)
  assert(d.board.id == BoardId::BoardDIN0);
  assert(d.board.family == 'D' && d.board.revision == 0);
  assert(d.board.flashBytes == 4u * 1024u * 1024u);
  assert(d.board.psramBytes == 2u * 1024u * 1024u);
  assert(VOXONE_PROFILE_DISPLAY == Display::Ssd1306_128x64);
  assert(d.displayKind == DisplayKind::Ssd1306_128x64);
  assert(activeController() == ControllerId::Ssd1306);
  assert(activeProfile() == ProfileId::Oled128x64);
  assert(layout.width == 128 && layout.height == 64 && layout.monochrome);
  assert(d.display.bus == BusKind::I2c);
  assert(caps.supportedDisplays ==
         (displaySupport(DisplayKind::Ssd1306_128x64) |
          displaySupport(DisplayKind::Ssd1309_128x64)));
  assert(caps.supportsDisplay(DisplayKind::Ssd1306_128x64));
  assert(caps.supportsDisplay(DisplayKind::Ssd1309_128x64));
  assert(d.i2c.sda == 7 && d.i2c.scl == 8);
  assert(d.display.cs == kNoPin && d.display.dc == kNoPin);
  assert(d.audioOut.bclk == 1 && d.audioOut.ws == 3 && d.audioOut.dout == 2);
  assert(d.localInput.a == 5 && d.localInput.b == 6 && d.localInput.key == 4);
  assert(!bool(ENC_INTERNALPULLUP));
  assert(!bool(ENC_BUTTON_INTERNALPULLUP));
  assert(!caps.supportsRtc);
  assert(!caps.supportsVoxOneBt);
  assert(!caps.supportsBtFirmwareUpdate);
  assert(caps.supportsInput(LocalInputKind::Ec11) &&
         caps.supportsInput(LocalInputKind::Buttons3) && caps.supportsLocalUi);
  assert(caps.supportsPsram);
  assert(d.btUart.rx == kNoPin && d.btUart.tx == kNoPin);
  assert(d.btAudioIn.bclk == kNoPin && d.btAudioIn.ws == kNoPin &&
         d.btAudioIn.din == kNoPin && !d.btAudioRxEnabled);
#endif

  HardwareDescriptor bad = d;
  bad.audioOut.bclk = bad.audioOut.ws;
  assert(hasPinConflicts(bad));
  assert(!validateDescriptor(bad));
  bad = d;
  bad.audioOut.bclk = kNoPin;
  assert(!validateDescriptor(bad));
  bad = d;
  bad.audioOut.bclk = 99;
  assert(!validateDescriptor(bad));
  bad = d;
  bad.capabilities.supportsDsp = true;
  // Future optional capabilities are not inferred from current GPIO.
  assert(validateDescriptor(bad));
  bad = d;
  bad.capabilities.supportedDisplays = d.capabilities.hasLocalDisplay()
      ? 0 : displaySupport(DisplayKind::St7789_284x76);
  assert(!validateDescriptor(bad));
  bad = d;
  bad.display.bus = d.capabilities.hasLocalDisplay() ? BusKind::None : BusKind::Spi;
  assert(!validateDescriptor(bad));
  bad = d;
  bad.displayKind = d.capabilities.hasLocalDisplay() ? DisplayKind::None :
                    DisplayKind::St7789_284x76;
  assert(!validateDescriptor(bad));
  bad = d;
  bad.capabilities.supportedDisplays = displaySupport(DisplayKind::St7789_284x76);
  bad.displayKind = DisplayKind::St7789_284x76;
  bad.display.bus = BusKind::Spi;
  bad.display.cs = kNoPin;
  assert(!validateDescriptor(bad));
  bad = d;
  bad.i2c.sda = kNoPin;
  if (d.displayKind == DisplayKind::Ssd1306_128x64 ||
      d.displayKind == DisplayKind::Ssd1309_128x64 ||
      d.capabilities.supportsRtc)
    assert(!validateDescriptor(bad));
  bad = d;
  bad.capabilities.supportedInputs = inputSupport(LocalInputKind::Ec11);
  bad.localInput.key = kNoPin;
  assert(!validateDescriptor(bad));
  bad = d;
  bad.capabilities.supportedInputs = inputSupport(LocalInputKind::Ec11);
  bad.localInput.ec11StepsPerDetent = 0;
  assert(!validateDescriptor(bad));
  std::printf("PASS hardware_descriptor_native (%s)\n", d.board.name);
}
