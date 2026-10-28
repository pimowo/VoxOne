#include "ch1116g_driver.h"

#include <new>
#include <string.h>

namespace voxone {
namespace displays {
namespace ch1116g {
namespace {

constexpr uint8_t kCommandControl = 0x00;
constexpr uint8_t kDataControl = 0x40;

}  // namespace

Driver::Driver(Transport& transport) : transport_(transport) {}

Driver::~Driver() { release(); }

bool Driver::validConfig(const Config& config) const {
  return (config.address == 0x3c || config.address == 0x3d) &&
         static_cast<unsigned>(config.columnOffset) + kWidth <= kRamColumns;
}

bool Driver::command(const uint8_t* data, size_t size) {
  if (!busHealthy_ || !data || size == 0) return false;
  if (transport_.write(config_.address, kCommandControl, data, size))
    return true;
  busHealthy_ = false;
  return false;
}

bool Driver::command(uint8_t value) { return command(&value, 1); }

bool Driver::begin(const Config& config) {
  release();
  if (!validConfig(config) || !transport_.probe(config.address)) return false;

  config_ = config;
  buffer_ = new (std::nothrow) uint8_t[kBufferSize];
  if (!buffer_) return false;
  memset(buffer_, 0, kBufferSize);
  busHealthy_ = true;

  // CH1116G commands are deliberately kept independent from SH1106. The
  // selected DC-DC mode is module-dependent; common 4-pin modules use 0x8b.
  const uint8_t init[] = {
      0xae,              // display off / sleep
      0xd5, 0x50,        // documented power-on clock divide / oscillator
      0xa8, 0x3f,        // 1/64 multiplex
      0xd3, 0x00,        // no vertical display offset
      0x40,              // display start line 0
      0xad, static_cast<uint8_t>(
                config.powerMode == PowerMode::InternalDcDc ? 0x8b : 0x8a),
      0xa1,              // segment remap for the supported module orientation
      0xc8,              // reverse COM scan direction
      0xda, 0x12,        // alternative COM configuration, no left/right remap
      0x81, config.contrast,
      0xd9, 0x22,        // pre-charge/discharge period
      0xdb, 0x35,        // documented power-on VCOM deselect level
      0xa4,              // display follows RAM
      0xa6,              // normal, not inverted
      0x2e               // deactivate scroll
  };

  if (!command(init, sizeof(init)) || !display()) {
    release();
    return false;
  }
  transport_.delayMilliseconds(100);
  if (!command(0xaf)) {
    release();
    return false;
  }
  return true;
}

bool Driver::writePage(uint8_t page) {
  if (page >= kPages || !ready()) return false;
  const uint8_t column = config_.columnOffset;
  const uint8_t address[] = {
      static_cast<uint8_t>(0xb0 | page),
      static_cast<uint8_t>(column & 0x0f),
      static_cast<uint8_t>(0x10 | (column >> 4))};
  if (!command(address, sizeof(address))) return false;

  const uint8_t* data = buffer_ + static_cast<size_t>(page) * kWidth;
  for (size_t offset = 0; offset < kWidth; offset += kMaxI2cDataChunk) {
    const size_t remaining = kWidth - offset;
    const size_t chunk = remaining < kMaxI2cDataChunk
                             ? remaining : kMaxI2cDataChunk;
    if (!transport_.write(config_.address, kDataControl, data + offset,
                          chunk)) {
      busHealthy_ = false;
      return false;
    }
  }
  return true;
}

bool Driver::display() {
  if (!ready()) return false;
  for (uint8_t page = 0; page < kPages; ++page)
    if (!writePage(page)) return false;
  return true;
}

bool Driver::sleep() { return ready() && command(0xae); }

bool Driver::wake() { return ready() && command(0xaf); }

bool Driver::invert(bool enabled) {
  return ready() && command(enabled ? 0xa7 : 0xa6);
}

void Driver::shutdown() {
  if (ready()) command(0xae);
  release();
}

void Driver::clear(bool white) {
  if (buffer_) memset(buffer_, white ? 0xff : 0x00, kBufferSize);
}

void Driver::drawPixel(int16_t x, int16_t y, PixelMode mode) {
  if (!buffer_ || x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
  uint8_t& target = buffer_[static_cast<size_t>(x) +
                            static_cast<size_t>(y / 8) * kWidth];
  const uint8_t mask = static_cast<uint8_t>(1u << (y & 7));
  if (mode == PixelMode::White) target |= mask;
  else if (mode == PixelMode::Black) target &= static_cast<uint8_t>(~mask);
  else target ^= mask;
}

void Driver::release() {
  delete[] buffer_;
  buffer_ = nullptr;
  busHealthy_ = false;
}

}  // namespace ch1116g
}  // namespace displays
}  // namespace voxone
