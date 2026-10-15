#ifndef VOXONE_BOOT_BRANDING_CODEC_H
#define VOXONE_BOOT_BRANDING_CODEC_H

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace display_boot_branding {

struct DecodeResult {
  bool valid;
  uint32_t pixels;
  size_t bytes;
};

// RLE stream: unsigned LEB128 run length followed by little-endian RGB565.
// The caller supplies byte access so firmware can read PROGMEM without making
// a RAM copy, while native tests can use ordinary arrays.
template <typename Reader, typename Sink>
DecodeResult decodeRle565(size_t encodedSize, uint32_t expectedPixels,
                          Reader readByte, Sink emitRun) {
  size_t offset = 0;
  uint32_t pixels = 0;
  while (offset < encodedSize) {
    uint32_t run = 0;
    uint8_t shift = 0;
    uint8_t byte = 0;
    do {
      if (offset >= encodedSize || shift > 21)
        return {false, pixels, offset};
      byte = readByte(offset++);
      run |= static_cast<uint32_t>(byte & 0x7FU) << shift;
      shift += 7;
    } while (byte & 0x80U);

    if (run == 0 || offset + 2 > encodedSize ||
        run > expectedPixels - pixels)
      return {false, pixels, offset};
    const uint16_t color = static_cast<uint16_t>(readByte(offset)) |
                           (static_cast<uint16_t>(readByte(offset + 1)) << 8);
    offset += 2;
    emitRun(pixels, run, color);
    pixels += run;
  }
  return {pixels == expectedPixels, pixels, offset};
}

}  // namespace display_boot_branding
}  // namespace voxone

#endif
