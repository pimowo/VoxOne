#include "hardware_descriptor.h"
#include "../../profiles/profile.h"

namespace voxone {
namespace hardware {
namespace {
#if defined(VOXONE_PROFILE_X0)
constexpr BoardId kBoardId = BoardId::BoardX0;
constexpr char kBoardFamily = 'X';
constexpr DisplayKind kDisplayKind = DisplayKind::St7789_284x76;
// The legacy X0 profile does not state its default SPI bus pins.
constexpr SpiBusPins kSpi = {kNoPin, kNoPin, kNoPin};
constexpr I2cBusPins kI2c = {kNoPin, kNoPin};
constexpr UartPins kBtUart = {kNoPin, kNoPin};
constexpr Pin kBacklight = kNoPin;
constexpr Pin kDacXsmt = kNoPin;
constexpr uint32_t kFlashBytes = 0;
constexpr uint32_t kPsramBytes = 0;
constexpr HardwareCapabilities kCapabilities = {
  displaySupport(DisplayKind::St7789_284x76),
  audioOutputSupport(AudioOutputKind::Pcm5102a),
  true, true, false, false, false, false, false, false, false,
  false, false, false, false
};
#elif defined(VOXONE_PROFILE_B0)
constexpr BoardId kBoardId = BoardId::BoardB0;
constexpr char kBoardFamily = 'B';
constexpr DisplayKind kDisplayKind = DisplayKind::None;
constexpr SpiBusPins kSpi = {kNoPin, kNoPin, kNoPin};
constexpr I2cBusPins kI2c = {kNoPin, kNoPin};
constexpr UartPins kBtUart = {fromLegacyPin(VOXONE_BT_UART_RX_PIN),
                               fromLegacyPin(VOXONE_BT_UART_TX_PIN)};
constexpr Pin kBacklight = kNoPin;
constexpr Pin kDacXsmt = kNoPin;
constexpr uint32_t kFlashBytes = 4u * 1024u * 1024u;
constexpr uint32_t kPsramBytes = 0;  // Size not established by the current profile.
constexpr HardwareCapabilities kCapabilities = {
  0,
  audioOutputSupport(AudioOutputKind::Pcm5102a),
  false, false, true, false, true, false, false, true, false,
  false, false, false, false
};
#elif defined(VOXONE_PROFILE_C0)
constexpr BoardId kBoardId = BoardId::BoardC0;
constexpr char kBoardFamily = 'C';
#if defined(VOXONE_C0_DISPLAY_SSD1309)
constexpr DisplayKind kDisplayKind = DisplayKind::Ssd1309_128x64;
#else
constexpr DisplayKind kDisplayKind = DisplayKind::Ssd1306_128x64;
#endif
constexpr SpiBusPins kSpi = {kNoPin, kNoPin, kNoPin};
constexpr I2cBusPins kI2c = {fromLegacyPin(I2C_SDA), fromLegacyPin(I2C_SCL)};
constexpr UartPins kBtUart = {kNoPin, kNoPin};
constexpr Pin kBacklight = kNoPin;
constexpr Pin kDacXsmt = kNoPin;
constexpr uint32_t kFlashBytes = 4u * 1024u * 1024u;
constexpr uint32_t kPsramBytes = 2u * 1024u * 1024u;
constexpr HardwareCapabilities kCapabilities = {
  displaySupport(DisplayKind::Ssd1306_128x64) |
      displaySupport(DisplayKind::Ssd1309_128x64),
  audioOutputSupport(AudioOutputKind::Pcm5102a),
  true, true, false, false, false, false, false, true, false,
  false, false, false, false
};
#elif defined(VOXONE_PROFILE_A0)
constexpr BoardId kBoardId = BoardId::BoardA0;
constexpr char kBoardFamily = 'A';
constexpr DisplayKind kDisplayKind = DisplayKind::St7796_480x320;
// SPI defaults are stated in a0.h; MISO belongs to the bus, not to LCD.
constexpr SpiBusPins kSpi = {12, 11, 13};
constexpr I2cBusPins kI2c = {fromLegacyPin(RTC_SDA), fromLegacyPin(RTC_SCL)};
constexpr UartPins kBtUart = {fromLegacyPin(VOXONE_BT_UART_RX_PIN),
                               fromLegacyPin(VOXONE_BT_UART_TX_PIN)};
constexpr Pin kBacklight = fromLegacyPin(BRIGHTNESS_PIN);
constexpr Pin kDacXsmt = fromLegacyPin(VOXONE_DAC_XSMT_PIN);
constexpr uint32_t kFlashBytes = 16u * 1024u * 1024u;
constexpr uint32_t kPsramBytes = 0;  // Size not established by the current profile.
constexpr HardwareCapabilities kCapabilities = {
  displaySupport(DisplayKind::St7796_480x320),
  audioOutputSupport(AudioOutputKind::Pcm5102a),
  true, true, true, true, true, true, true, true, false,
  false, false, false, false
};
#else
// The unfinished a0_dsp profile has no verified complete pin map.
#error "HardwareDescriptor requires a complete X0, B0, C0 or A0 profile"
#endif

constexpr HardwareDescriptor kCurrent = {
  {kBoardId, VOXONE_PROFILE_NAME, kBoardFamily, 0,
   VOXONE_PROFILE_MCU == Mcu::Esp32 ? McuFamily::Esp32 : McuFamily::Esp32S3,
   kFlashBytes, kPsramBytes},
  kDisplayKind,
  VOXONE_PROFILE_AUDIO,
  kSpi,
  kI2c,
  {fromLegacyPin(I2S_BCLK), fromLegacyPin(I2S_LRC),
   fromLegacyPin(I2S_DOUT), kNoPin},
  {fromLegacyPin(VOXONE_BT_I2S_BCLK_PIN),
   fromLegacyPin(VOXONE_BT_I2S_WS_PIN), kNoPin,
   fromLegacyPin(VOXONE_BT_I2S_DATA_PIN)},
  VOXONE_BT_I2S_RX_ENABLED != 0,
  kBtUart,
  {fromLegacyPin(ENC_BTNL), fromLegacyPin(ENC_BTNR),
   fromLegacyPin(ENC_BTNB), ENC_INTERNALPULLUP, ENC_BUTTON_INTERNALPULLUP,
   ENC_HALFQUARD == 255 ? 1 : (ENC_HALFQUARD ? 2 : 4)},
  {fromLegacyPin(TFT_CS), fromLegacyPin(TFT_DC),
   fromLegacyPin(TFT_RST), kBacklight,
   (kDisplayKind == DisplayKind::Ssd1306_128x64 ||
    kDisplayKind == DisplayKind::Ssd1309_128x64) ? BusKind::I2c :
     (kCapabilities.hasLocalDisplay() ? BusKind::Spi : BusKind::None)},
  kDacXsmt,
  kCapabilities
};

static_assert(bool(VOXONE_HAS_DISPLAY) == kCapabilities.hasLocalDisplay(),
              "Display compile gate must match board capabilities");
static_assert(kDisplayKind == DisplayKind::None
                  ? !kCapabilities.hasLocalDisplay()
                  : kCapabilities.supportsDisplay(kDisplayKind),
              "Selected display backend must be supported by board capabilities");
static_assert(bool(VOXONE_HAS_ENCODER) == kCapabilities.supportsEncoder,
              "Encoder compile gate must match board capabilities");
static_assert(bool(VOXONE_HAS_LOCAL_UI) == kCapabilities.supportsLocalUi,
              "Local UI compile gate must match board capabilities");
static_assert(bool(VOXONE_HAS_BT) == kCapabilities.supportsVoxOneBt,
              "BT compile gate must match board capabilities");
static_assert(bool(VOXONE_HAS_VU) == kCapabilities.supportsVu,
              "VU compile gate must match board capabilities");
static_assert(bool(VOXONE_HAS_AUX) == kCapabilities.supportsAux,
              "AUX compile gate must match board capabilities");
static_assert(bool(VOXONE_HAS_SPDIF) == kCapabilities.supportsSpdif,
              "SPDIF compile gate must match board capabilities");
static_assert(bool(VOXONE_HAS_TDA7719) == kCapabilities.supportsTda7719,
              "TDA7719 compile gate must match board capabilities");
static_assert(bool(VOXONE_BT_I2S_RX_ENABLED) == kCapabilities.supportsBtAudioRx,
              "BT audio RX compile gate must match board capabilities");
#if defined(ARDUINO) && defined(BOARD_HAS_PSRAM)
static_assert(kCapabilities.supportsPsram,
              "PSRAM build flag requires board PSRAM capability");
#elif defined(ARDUINO)
static_assert(!kCapabilities.supportsPsram,
              "Board PSRAM capability requires the PSRAM build flag");
#endif
static_assert(!kCapabilities.supportsVoxOneBt ||
              (hasPin(kCurrent.btUart.rx) && hasPin(kCurrent.btUart.tx)),
              "BT capable profile requires UART RX and TX pins");
static_assert(!kCapabilities.supportsBtAudioRx ||
              (hasPin(kCurrent.btAudioIn.bclk) &&
               hasPin(kCurrent.btAudioIn.ws) &&
               hasPin(kCurrent.btAudioIn.din)),
              "BT I2S RX requires a complete pin map");

bool validPin(Pin pin, McuFamily mcu) {
  if (!hasPin(pin)) return true;
  if (pin < 0) return false;
  if (mcu == McuFamily::Esp32) return pin <= 39;
  return pin <= 48 && (pin <= 21 || pin >= 26);
}
}  // namespace

const HardwareDescriptor& currentHardware() { return kCurrent; }
const HardwareCapabilities& hardwareCapabilities() {
  return kCurrent.capabilities;
}

bool hasPinConflicts(const HardwareDescriptor& d) {
  // Bus pins are listed once. Device CS/DC and UART pins are separate claims.
  const Pin claims[] = {
    d.spi.sck, d.spi.mosi, d.spi.miso, d.i2c.sda, d.i2c.scl,
    d.audioOut.bclk, d.audioOut.ws, d.audioOut.dout, d.audioOut.din,
    d.btAudioIn.bclk, d.btAudioIn.ws, d.btAudioIn.dout, d.btAudioIn.din,
    d.btUart.rx, d.btUart.tx,
    d.encoder.a, d.encoder.b, d.encoder.button,
    d.display.cs, d.display.dc, d.display.rst, d.display.backlight,
    d.dacXsmt
  };
  for (unsigned i = 0; i < sizeof(claims) / sizeof(claims[0]); ++i) {
    if (!hasPin(claims[i])) continue;
    for (unsigned j = i + 1; j < sizeof(claims) / sizeof(claims[0]); ++j)
      if (claims[i] == claims[j]) return true;
  }
  return false;
}

bool validateDescriptor(const HardwareDescriptor& d) {
  const bool display = d.displayKind != DisplayKind::None;
  if ((display && !d.capabilities.supportsDisplay(d.displayKind)) ||
      (!display && d.capabilities.hasLocalDisplay()) ||
      !d.capabilities.supportsAudioOutput(d.audioOutputKind) ||
      (d.capabilities.supportsRtc &&
       (!hasPin(d.i2c.sda) || !hasPin(d.i2c.scl))) ||
      d.capabilities.supportsVoxOneBt !=
        (hasPin(d.btUart.rx) && hasPin(d.btUart.tx)) ||
      d.capabilities.supportsAudioOutput(AudioOutputKind::Pcm5102a) !=
        (hasPin(d.audioOut.bclk) && hasPin(d.audioOut.ws) &&
         hasPin(d.audioOut.dout)) ||
      d.capabilities.supportsDacHardwareMute != hasPin(d.dacXsmt) ||
      (d.board.psramBytes != 0 && !d.capabilities.supportsPsram) ||
      (d.capabilities.supportsLocalUi &&
       (!d.capabilities.hasLocalDisplay() || !d.capabilities.supportsEncoder)))
    return false;
  if (d.displayKind == DisplayKind::Ssd1306_128x64 ||
      d.displayKind == DisplayKind::Ssd1309_128x64) {
    if (d.display.bus != BusKind::I2c ||
        !hasPin(d.i2c.sda) || !hasPin(d.i2c.scl)) return false;
  } else if (display) {
    if (!hasPin(d.display.cs) || !hasPin(d.display.dc) ||
        d.display.bus != BusKind::Spi) return false;
  } else if (d.display.bus != BusKind::None) return false;
  if (d.capabilities.supportsEncoder &&
      (!hasPin(d.encoder.a) || !hasPin(d.encoder.b) ||
       !hasPin(d.encoder.button) || d.encoder.stepsPerDetent == 0)) return false;
  if (d.btAudioRxEnabled != d.capabilities.supportsBtAudioRx ||
      (d.btAudioRxEnabled &&
      (!d.capabilities.supportsVoxOneBt || !hasPin(d.btAudioIn.bclk) ||
       !hasPin(d.btAudioIn.ws) || !hasPin(d.btAudioIn.din)))) return false;
  const Pin pins[] = {
    d.spi.sck, d.spi.mosi, d.spi.miso, d.i2c.sda, d.i2c.scl,
    d.audioOut.bclk, d.audioOut.ws, d.audioOut.dout, d.audioOut.din,
    d.btAudioIn.bclk, d.btAudioIn.ws, d.btAudioIn.dout, d.btAudioIn.din,
    d.btUart.rx, d.btUart.tx, d.encoder.a, d.encoder.b, d.encoder.button,
    d.display.cs, d.display.dc, d.display.rst, d.display.backlight, d.dacXsmt
  };
  for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i)
    if (!validPin(pins[i], d.board.mcu)) return false;
  return !hasPinConflicts(d);
}
}  // namespace hardware
}  // namespace voxone
