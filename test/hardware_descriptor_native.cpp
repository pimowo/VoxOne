#include "../src/hardware/hardware_descriptor.h"
#include "../profiles/profile.h"

#include <cassert>
#include <cstring>
#include <cstdio>

using namespace voxone;
using namespace voxone::hardware;

int main() {
  const HardwareDescriptor& d = currentHardware();
  const HardwareCapabilities& caps = hardwareCapabilities();
  assert(&caps == &d.capabilities);
  assert(std::strcmp(d.board.name, VOXONE_PROFILE_NAME) == 0);
  assert(d.board.mcu == (VOXONE_PROFILE_MCU == Mcu::Esp32 ?
                         McuFamily::Esp32 : McuFamily::Esp32S3));
  assert(caps.hasLocalDisplay() == bool(VOXONE_HAS_DISPLAY));
  assert(caps.supportsEncoder == bool(VOXONE_HAS_ENCODER));
  assert(caps.supportsLocalUi == bool(VOXONE_HAS_LOCAL_UI));
  assert(caps.supportsEncoder ==
         (hasPin(d.encoder.a) && hasPin(d.encoder.b) &&
          hasPin(d.encoder.button)));
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
  assert(d.display.bus ==
         ((d.displayKind == DisplayKind::Ssd1306_128x64 ||
           d.displayKind == DisplayKind::Ssd1309_128x64) ? BusKind::I2c :
          (caps.hasLocalDisplay() ? BusKind::Spi : BusKind::None)));
  assert(d.encoder.a == fromLegacyPin(ENC_BTNL));
  assert(d.encoder.b == fromLegacyPin(ENC_BTNR));
  assert(d.encoder.button == fromLegacyPin(ENC_BTNB));
  assert(d.encoder.internalPullup == bool(ENC_INTERNALPULLUP));
  assert(d.encoder.buttonInternalPullup == bool(ENC_BUTTON_INTERNALPULLUP));
  assert(d.encoder.stepsPerDetent ==
         (ENC_HALFQUARD == 255 ? 1 : (ENC_HALFQUARD ? 2 : 4)));
  assert(d.btAudioIn.bclk == fromLegacyPin(VOXONE_BT_I2S_BCLK_PIN));
  assert(d.btAudioIn.ws == fromLegacyPin(VOXONE_BT_I2S_WS_PIN));
  assert(d.btAudioIn.din == fromLegacyPin(VOXONE_BT_I2S_DATA_PIN));
  assert(d.btAudioIn.dout == kNoPin);
  assert(d.btAudioRxEnabled == bool(VOXONE_BT_I2S_RX_ENABLED));
  assert(!d.btAudioRxEnabled ||
         (hasPin(d.btAudioIn.bclk) && hasPin(d.btAudioIn.ws) &&
          hasPin(d.btAudioIn.din)));
#if defined(VOXONE_PROFILE_C0)
  assert(d.i2c.sda == fromLegacyPin(I2C_SDA));
  assert(d.i2c.scl == fromLegacyPin(I2C_SCL));
#else
  assert(d.i2c.sda == fromLegacyPin(RTC_SDA));
  assert(d.i2c.scl == fromLegacyPin(RTC_SCL));
#endif
#if defined(VOXONE_PROFILE_A0)
  assert(caps.supportsRtc);
#else
  assert(!caps.supportsRtc);
#endif
  assert(fromLegacyPin(255) == kNoPin && fromLegacyPin(-1) == kNoPin);
  assert(!hasPin(kNoPin));
  assert(validateDescriptor(d));
  assert(!hasPinConflicts(d));

#if defined(VOXONE_PROFILE_X0)
  assert(d.board.id == BoardId::BoardX0);
  assert(d.board.family == 'X' && d.board.revision == 0);
  assert(std::strcmp(d.board.name, "X0") == 0);
  assert(d.board.mcu == McuFamily::Esp32);
  assert(d.displayKind == DisplayKind::St7789_284x76);
  assert(caps.hasLocalDisplay());
  assert(caps.supportedDisplays == displaySupport(DisplayKind::St7789_284x76));
  assert(d.display.cs == 5 && d.display.dc == 4);
  assert(d.display.rst == kNoPin && d.display.backlight == kNoPin);
  assert(d.audioOut.dout == 27 && d.audioOut.bclk == 26 && d.audioOut.ws == 25);
  assert(d.encoder.a == 33 && d.encoder.b == 35 && d.encoder.button == 32);
  assert(!d.encoder.internalPullup && d.encoder.stepsPerDetent == 4);
  assert(!d.encoder.buttonInternalPullup);
  // The legacy X0 profile does not specify default SPI bus pins.
  assert(d.spi.sck == kNoPin && d.spi.mosi == kNoPin &&
         d.spi.miso == kNoPin);
  assert(!caps.supportsVoxOneBt);
  assert(!caps.supportsBtFirmwareUpdate);
  assert(!caps.supportsPsram);
  assert(d.btUart.rx == kNoPin && d.btUart.tx == kNoPin);
  assert(d.btAudioIn.bclk == kNoPin && d.btAudioIn.ws == kNoPin &&
         d.btAudioIn.din == kNoPin && !d.btAudioRxEnabled);
  assert(d.dacXsmt == kNoPin);
#elif defined(VOXONE_PROFILE_B0)
  assert(d.board.id == BoardId::BoardB0);
  assert(d.board.family == 'B' && d.board.revision == 0);
  assert(std::strcmp(d.board.name, "B0") == 0);
  assert(d.displayKind == DisplayKind::None);
  assert(!caps.hasLocalDisplay());
  assert(caps.supportedDisplays == 0);
  assert(d.spi.sck == kNoPin && d.spi.mosi == kNoPin &&
         d.spi.miso == kNoPin);
  assert(d.audioOut.bclk == 1 && d.audioOut.dout == 2 && d.audioOut.ws == 3);
  assert(d.btAudioIn.bclk == 4 && d.btAudioIn.ws == 5 && d.btAudioIn.din == 6);
  assert(!d.btAudioRxEnabled);  // Reserved wiring; runtime RX stays disabled.
  assert(d.btUart.rx == 7 && d.btUart.tx == 8);
  assert(d.btUart.rx == fromLegacyPin(VOXONE_BT_UART_RX_PIN));
  assert(d.btUart.tx == fromLegacyPin(VOXONE_BT_UART_TX_PIN));
  assert(caps.supportsVoxOneBt);
  assert(!caps.supportsBtAudioRx);
  assert(caps.supportsBtFirmwareUpdate);
  assert(caps.supportsPsram);
  assert(d.display.cs == kNoPin && d.display.dc == kNoPin &&
         d.display.rst == kNoPin && d.display.backlight == kNoPin);
  assert(!caps.supportsEncoder && !caps.supportsLocalUi);
  assert(d.encoder.a == kNoPin && d.encoder.b == kNoPin &&
         d.encoder.button == kNoPin);
  assert(d.dacXsmt == kNoPin);
#elif defined(VOXONE_PROFILE_A0)
  assert(d.board.id == BoardId::BoardA0);
  assert(d.board.family == 'A' && d.board.revision == 0);
  assert(std::strcmp(d.board.name, "A0") == 0);
  assert(d.displayKind == DisplayKind::St7796_480x320);
  assert(caps.hasLocalDisplay());
  assert(caps.supportedDisplays == displaySupport(DisplayKind::St7796_480x320));
  assert(d.spi.mosi == 11 && d.spi.sck == 12 && d.spi.miso == 13);
  assert(d.display.cs == 10 && d.display.dc == 9 && d.display.backlight == 14);
  assert(d.display.rst == kNoPin);
  assert(d.display.backlight == fromLegacyPin(BRIGHTNESS_PIN));
  assert(d.encoder.a == 41 && d.encoder.b == 40 && d.encoder.button == 39);
  assert(!d.encoder.internalPullup && d.encoder.stepsPerDetent == 4);
  assert(!d.encoder.buttonInternalPullup);
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
#elif defined(VOXONE_PROFILE_C0)
  assert(d.board.id == BoardId::BoardC0);
  assert(d.board.family == 'C' && d.board.revision == 0);
  assert(d.board.flashBytes == 4u * 1024u * 1024u);
  assert(d.board.psramBytes == 2u * 1024u * 1024u);
#if defined(VOXONE_C0_DISPLAY_SSD1309)
  assert(VOXONE_PROFILE_DISPLAY == Display::Ssd1309_128x64);
  assert(d.displayKind == DisplayKind::Ssd1309_128x64);
#else
  assert(VOXONE_PROFILE_DISPLAY == Display::Ssd1306_128x64);
  assert(d.displayKind == DisplayKind::Ssd1306_128x64);
#endif
  assert(d.display.bus == BusKind::I2c);
  assert(caps.supportedDisplays ==
         (displaySupport(DisplayKind::Ssd1306_128x64) |
          displaySupport(DisplayKind::Ssd1309_128x64)));
  assert(caps.supportsDisplay(DisplayKind::Ssd1306_128x64));
  assert(caps.supportsDisplay(DisplayKind::Ssd1309_128x64));
  assert(d.i2c.sda == 7 && d.i2c.scl == 8);
  assert(d.display.cs == kNoPin && d.display.dc == kNoPin);
  assert(d.audioOut.bclk == 1 && d.audioOut.ws == 3 && d.audioOut.dout == 2);
  assert(d.encoder.a == 5 && d.encoder.b == 6 && d.encoder.button == 4);
  assert(!d.encoder.internalPullup);
  assert(d.encoder.buttonInternalPullup);
  assert(!caps.supportsRtc);
  assert(!caps.supportsVoxOneBt);
  assert(!caps.supportsBtFirmwareUpdate);
  assert(caps.supportsEncoder && caps.supportsLocalUi);
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
  bad.capabilities.supportsEncoder = true;
  bad.encoder.button = kNoPin;
  assert(!validateDescriptor(bad));
  bad = d;
  bad.capabilities.supportsEncoder = true;
  bad.encoder.stepsPerDetent = 0;
  assert(!validateDescriptor(bad));
#if defined(VOXONE_C0_DISPLAY_SSD1309)
  std::printf("PASS hardware_descriptor_native (C0 SSD1309)\n");
#else
  std::printf("PASS hardware_descriptor_native (%s)\n", d.board.name);
#endif
}
