#ifndef VOXONE_CH1116G_DRIVER_H
#define VOXONE_CH1116G_DRIVER_H

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace displays {
namespace ch1116g {

constexpr uint8_t kWidth = 128;
constexpr uint8_t kHeight = 64;
constexpr uint8_t kPages = 8;
constexpr uint8_t kRamColumns = 132;
constexpr size_t kBufferSize = static_cast<size_t>(kWidth) * kPages;
constexpr uint8_t kDefaultAddress = 0x3c;
constexpr uint8_t kDefaultColumnOffset = 2;
constexpr size_t kMaxI2cDataChunk = 16;

enum class PowerMode : uint8_t {
  ExternalVpp,
  InternalDcDc
};

enum class PixelMode : uint8_t {
  Black,
  White,
  Invert
};

struct Config {
  uint8_t address = kDefaultAddress;
  uint8_t columnOffset = kDefaultColumnOffset;
  uint8_t contrast = 0x80;
  PowerMode powerMode = PowerMode::InternalDcDc;
};

class Transport {
 public:
  virtual ~Transport() {}
  virtual bool probe(uint8_t address) = 0;
  virtual bool write(uint8_t address, uint8_t control,
                     const uint8_t* data, size_t size) = 0;
  virtual void delayMilliseconds(uint16_t milliseconds) = 0;
};

class Driver {
 public:
  explicit Driver(Transport& transport);
  ~Driver();

  Driver(const Driver&) = delete;
  Driver& operator=(const Driver&) = delete;

  bool begin(const Config& config = Config());
  bool display();
  bool sleep();
  bool wake();
  bool invert(bool enabled);
  void shutdown();

  void clear(bool white = false);
  void drawPixel(int16_t x, int16_t y, PixelMode mode);

  bool ready() const { return buffer_ != nullptr && busHealthy_; }
  const uint8_t* buffer() const { return buffer_; }
  uint8_t* buffer() { return buffer_; }
  const Config& config() const { return config_; }

 private:
  bool command(const uint8_t* data, size_t size);
  bool command(uint8_t value);
  bool writePage(uint8_t page);
  bool validConfig(const Config& config) const;
  void release();

  Transport& transport_;
  Config config_;
  uint8_t* buffer_ = nullptr;
  bool busHealthy_ = false;
};

}  // namespace ch1116g
}  // namespace displays
}  // namespace voxone

#endif
