#ifndef VOXONE_HARDWARE_DESCRIPTOR_H
#define VOXONE_HARDWARE_DESCRIPTOR_H

#include <stdint.h>

namespace voxone {
namespace hardware {

// One neutral representation. The adapter maps both legacy 255 and -1 here.
using Pin = int16_t;
constexpr Pin kNoPin = -1;
constexpr Pin fromLegacyPin(int value) {
  return value == 255 || value == -1 ? kNoPin : static_cast<Pin>(value);
}
constexpr bool hasPin(Pin value) { return value != kNoPin; }

enum class BoardId { BoardX0, BoardB0, BoardC0, BoardA0 };
enum class McuFamily { Esp32, Esp32S3 };
enum class DisplayKind { None, Ssd1306_128x64, Ssd1309_128x64, St7789_284x76, St7796_480x320 };
enum class BusKind { None, Spi, I2c };

struct BoardIdentity {
  BoardId id;
  const char* name;  // Display name, e.g. A0.
  char family;
  uint8_t revision;
  McuFamily mcu;
  uint32_t flashBytes;  // 0 means unconfirmed.
  uint32_t psramBytes;  // 0 means unconfirmed.
};
struct I2sPins { Pin bclk, ws, dout, din; };
struct SpiBusPins { Pin sck, mosi, miso; };
struct I2cBusPins { Pin sda, scl; };
struct UartPins { Pin rx, tx; };
struct EncoderPins {
  Pin a, b, button;
  bool internalPullup;
  bool buttonInternalPullup;
  uint8_t stepsPerDetent;
};
struct DisplayPins { Pin cs, dc, rst, backlight; BusKind bus; };
struct Capabilities {
  bool supportsDisplay;
  bool supportsVoxOneBt;
  bool supportsBtFirmwareUpdate;
  bool supportsPcm5102;
  bool supportsDsp;
  bool supportsMax98357;
  bool supportsEncoder;
  bool supportsRtc;
};
struct HardwareDescriptor {
  BoardIdentity board;
  DisplayKind displayKind;
  SpiBusPins spi;
  I2cBusPins i2c;
  I2sPins audioOut;
  I2sPins btAudioIn;  // May be physically reserved even when runtime RX is disabled.
  bool btAudioRxEnabled;
  UartPins btUart;
  EncoderPins encoder;
  DisplayPins display;
  Pin dacXsmt;
  Capabilities capabilities;
};

// Describes the current legacy profile values for runtime consumers.
const HardwareDescriptor& currentHardware();
bool hasPinConflicts(const HardwareDescriptor& descriptor);
bool validateDescriptor(const HardwareDescriptor& descriptor);

}  // namespace hardware
}  // namespace voxone

#endif
