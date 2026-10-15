#include "../src/displays/tools/boot_branding_codec.h"
#include "../src/displays/fonts/voxone_boot_logo_oled_128x64.h"
#include "../src/displays/fonts/voxone_boot_logo_st7789_284x76.h"
#include "../src/displays/fonts/voxone_boot_logo_st7796_480x320.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace {

uint32_t crc32Byte(uint32_t crc, uint8_t value) {
  crc ^= value;
  for (uint8_t bit = 0; bit < 8; ++bit)
    crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  return crc;
}

uint32_t crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < size; ++i) crc = crc32Byte(crc, data[i]);
  return crc ^ 0xFFFFFFFFU;
}

template <size_t Size>
void verifyTft(const uint8_t (&encoded)[Size], uint16_t width,
               uint16_t height, uint32_t expectedDecodedCrc) {
  uint32_t crc = 0xFFFFFFFFU;
  uint32_t runs = 0;
  const auto result = voxone::display_boot_branding::decodeRle565(
      Size, static_cast<uint32_t>(width) * height,
      [&encoded](size_t offset) { return encoded[offset]; },
      [&crc, &runs, width](uint32_t pixel, uint32_t count, uint16_t color) {
        assert(pixel % width + count <= width);
        ++runs;
        for (uint32_t i = 0; i < count; ++i) {
          crc = crc32Byte(crc, static_cast<uint8_t>(color));
          crc = crc32Byte(crc, static_cast<uint8_t>(color >> 8));
        }
      });
  assert(result.valid);
  assert(result.bytes == Size);
  assert(result.pixels == static_cast<uint32_t>(width) * height);
  assert(runs > height);
  assert((crc ^ 0xFFFFFFFFU) == expectedDecodedCrc);
}

}  // namespace

int main() {
  using namespace voxone::display_boot_branding;

  static_assert(oled::kBootLogoWidth == 108 && oled::kBootLogoHeight == 23,
                "OLED crop must preserve the official wordmark bounds");
  static_assert(oled::kBootLogoRowBytes == 14,
                "OLED data must remain row-major MSB-first");
  assert(crc32(oled::kBootLogoBitmap, sizeof(oled::kBootLogoBitmap)) ==
         0x82D6520CU);

  static_assert(st7789::kBootLogoLeft == 53 &&
                    st7789::kBootLogoTop == 4 &&
                    st7789::kBootLogoWidth == 178 &&
                    st7789::kBootLogoHeight == 40,
                "ST7789 boot-logo placement changed");
  verifyTft(st7789::kBootLogoRle565, st7789::kBootLogoWidth,
            st7789::kBootLogoHeight, 0xF14F08A8U);

  static_assert(st7796::kBootLogoLeft == 36 &&
                    st7796::kBootLogoTop == 115 &&
                    st7796::kBootLogoWidth == 408 &&
                    st7796::kBootLogoHeight == 89,
                "ST7796 boot-logo placement changed");
  verifyTft(st7796::kBootLogoRle565, st7796::kBootLogoWidth,
            st7796::kBootLogoHeight, 0x31703177U);

  const uint8_t zeroRun[] = {0x00, 0x00, 0x00};
  const auto invalid = decodeRle565(
      sizeof(zeroRun), 1U, [&zeroRun](size_t offset) { return zeroRun[offset]; },
      [](uint32_t, uint32_t, uint16_t) {});
  assert(!invalid.valid);

  const uint8_t truncated[] = {0x01, 0x34};
  const auto missingColorByte = decodeRle565(
      sizeof(truncated), 1U,
      [&truncated](size_t offset) { return truncated[offset]; },
      [](uint32_t, uint32_t, uint16_t) {});
  assert(!missingColorByte.valid);

  const uint8_t overrun[] = {0x02, 0x34, 0x12};
  const auto tooManyPixels = decodeRle565(
      sizeof(overrun), 1U,
      [&overrun](size_t offset) { return overrun[offset]; },
      [](uint32_t, uint32_t, uint16_t) {});
  assert(!tooManyPixels.valid);
}
