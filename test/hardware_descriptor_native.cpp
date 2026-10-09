#include "../src/hardware/hardware_descriptor.h"
#include "../profiles/profile.h"

#include <cassert>
#include <cstring>
#include <cstdio>

using namespace voxone;
using namespace voxone::hardware;

int main() {
  const HardwareDescriptor& d = currentHardware();
  assert(std::strcmp(d.board.name, VOXONE_PROFILE_NAME) == 0);
  assert(d.board.mcu == (activeProfile.mcu == Mcu::Esp32 ?
                         McuFamily::Esp32 : McuFamily::Esp32S3));
  assert(d.capabilities.supportsDisplay == bool(VOXONE_HAS_DISPLAY));
  assert(d.capabilities.supportsEncoder == bool(VOXONE_HAS_ENCODER));
  assert(d.capabilities.supportsEncoder ==
         (hasPin(d.encoder.a) && hasPin(d.encoder.b) &&
          hasPin(d.encoder.button)));
  assert(d.capabilities.supportsVoxOneBt == bool(VOXONE_HAS_BT));
  assert(d.capabilities.supportsVoxOneBt ==
         (hasPin(d.btUart.rx) && hasPin(d.btUart.tx)));
  assert(d.capabilities.supportsPcm5102);
  assert(!d.capabilities.supportsDsp);
  assert(!d.capabilities.supportsMax98357);
  assert(d.audioOut.bclk == fromLegacyPin(I2S_BCLK));
  assert(d.audioOut.ws == fromLegacyPin(I2S_LRC));
  assert(d.audioOut.dout == fromLegacyPin(I2S_DOUT));
  assert(d.display.cs == fromLegacyPin(TFT_CS));
  assert(d.display.dc == fromLegacyPin(TFT_DC));
  assert(d.display.rst == fromLegacyPin(TFT_RST));
  assert(d.capabilities.supportsDisplay == (d.displayKind != DisplayKind::None));
  assert(d.display.bus == (d.capabilities.supportsDisplay ?
                           BusKind::Spi : BusKind::None));
  assert(d.encoder.a == fromLegacyPin(ENC_BTNL));
  assert(d.encoder.b == fromLegacyPin(ENC_BTNR));
  assert(d.encoder.button == fromLegacyPin(ENC_BTNB));
  assert(d.encoder.internalPullup == bool(ENC_INTERNALPULLUP));
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
  assert(d.i2c.sda == fromLegacyPin(RTC_SDA));
  assert(d.i2c.scl == fromLegacyPin(RTC_SCL));
  assert(d.capabilities.supportsRtc ==
         (RTC_SDA != 255 && RTC_SCL != 255));
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
  assert(d.capabilities.supportsDisplay);
  assert(d.display.cs == 5 && d.display.dc == 4);
  assert(d.display.rst == kNoPin && d.display.backlight == kNoPin);
  assert(d.audioOut.dout == 27 && d.audioOut.bclk == 26 && d.audioOut.ws == 25);
  assert(d.encoder.a == 33 && d.encoder.b == 35 && d.encoder.button == 32);
  assert(!d.encoder.internalPullup && d.encoder.stepsPerDetent == 4);
  // The legacy X0 profile does not specify default SPI bus pins.
  assert(d.spi.sck == kNoPin && d.spi.mosi == kNoPin &&
         d.spi.miso == kNoPin);
  assert(!d.capabilities.supportsVoxOneBt);
  assert(!d.capabilities.supportsBtFirmwareUpdate);
  assert(d.btUart.rx == kNoPin && d.btUart.tx == kNoPin);
  assert(d.btAudioIn.bclk == kNoPin && d.btAudioIn.ws == kNoPin &&
         d.btAudioIn.din == kNoPin && !d.btAudioRxEnabled);
  assert(d.dacXsmt == kNoPin);
#elif defined(VOXONE_PROFILE_B0)
  assert(d.board.id == BoardId::BoardB0);
  assert(d.board.family == 'B' && d.board.revision == 0);
  assert(std::strcmp(d.board.name, "B0") == 0);
  assert(d.displayKind == DisplayKind::None);
  assert(!d.capabilities.supportsDisplay);
  assert(d.spi.sck == kNoPin && d.spi.mosi == kNoPin &&
         d.spi.miso == kNoPin);
  assert(d.audioOut.bclk == 1 && d.audioOut.dout == 2 && d.audioOut.ws == 3);
  assert(d.btAudioIn.bclk == 4 && d.btAudioIn.ws == 5 && d.btAudioIn.din == 6);
  assert(!d.btAudioRxEnabled);  // Reserved wiring; runtime RX stays disabled.
  assert(d.btUart.rx == 7 && d.btUart.tx == 8);
  assert(d.btUart.rx == fromLegacyPin(VOXONE_BT_UART_RX_PIN));
  assert(d.btUart.tx == fromLegacyPin(VOXONE_BT_UART_TX_PIN));
  assert(d.capabilities.supportsVoxOneBt);
  assert(d.capabilities.supportsBtFirmwareUpdate);
  assert(d.display.cs == kNoPin && d.display.dc == kNoPin &&
         d.display.rst == kNoPin && d.display.backlight == kNoPin);
  assert(!d.capabilities.supportsEncoder);
  assert(d.encoder.a == kNoPin && d.encoder.b == kNoPin &&
         d.encoder.button == kNoPin);
  assert(d.dacXsmt == kNoPin);
#elif defined(VOXONE_PROFILE_A0)
  assert(d.board.id == BoardId::BoardA0);
  assert(d.board.family == 'A' && d.board.revision == 0);
  assert(std::strcmp(d.board.name, "A0") == 0);
  assert(d.displayKind == DisplayKind::St7796_480x320);
  assert(d.capabilities.supportsDisplay);
  assert(d.spi.mosi == 11 && d.spi.sck == 12 && d.spi.miso == 13);
  assert(d.display.cs == 10 && d.display.dc == 9 && d.display.backlight == 14);
  assert(d.display.rst == kNoPin);
  assert(d.display.backlight == fromLegacyPin(BRIGHTNESS_PIN));
  assert(d.encoder.a == 41 && d.encoder.b == 40 && d.encoder.button == 39);
  assert(!d.encoder.internalPullup && d.encoder.stepsPerDetent == 4);
  assert(d.audioOut.dout == 4 && d.audioOut.bclk == 5 && d.audioOut.ws == 6);
  assert(d.i2c.sda == 8 && d.i2c.scl == 7);
  assert(d.btUart.rx == 15 && d.btUart.tx == 16);
  assert(d.btUart.rx == fromLegacyPin(VOXONE_BT_UART_RX_PIN));
  assert(d.btUart.tx == fromLegacyPin(VOXONE_BT_UART_TX_PIN));
  assert(d.capabilities.supportsVoxOneBt && d.btAudioRxEnabled);
  assert(d.capabilities.supportsBtFirmwareUpdate);
  assert(d.btAudioIn.bclk == 1 && d.btAudioIn.ws == 2 && d.btAudioIn.din == 17);
  assert(d.dacXsmt == kNoPin);
  assert(d.dacXsmt == fromLegacyPin(VOXONE_DAC_XSMT_PIN));
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
  bad.capabilities.supportsDisplay = !d.capabilities.supportsDisplay;
  assert(!validateDescriptor(bad));
  bad = d;
  bad.display.bus = d.capabilities.supportsDisplay ? BusKind::None : BusKind::Spi;
  assert(!validateDescriptor(bad));
  bad = d;
  bad.displayKind = d.capabilities.supportsDisplay ? DisplayKind::None :
                    DisplayKind::St7789_284x76;
  assert(!validateDescriptor(bad));
  bad = d;
  bad.capabilities.supportsDisplay = true;
  bad.displayKind = DisplayKind::St7789_284x76;
  bad.display.bus = BusKind::Spi;
  bad.display.cs = kNoPin;
  assert(!validateDescriptor(bad));
  bad = d;
  bad.capabilities.supportsEncoder = true;
  bad.encoder.button = kNoPin;
  assert(!validateDescriptor(bad));
  bad = d;
  bad.capabilities.supportsEncoder = true;
  bad.encoder.stepsPerDetent = 0;
  assert(!validateDescriptor(bad));
  std::printf("PASS hardware_descriptor_native (%s)\n", d.board.name);
}
