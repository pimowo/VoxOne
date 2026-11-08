#include "../src/core/tft_theme_config.h"
#include "../src/displays/yamaha_tft_layout.h"

#include <array>
#include <cassert>
#include <cstring>

int main() {
  std::array<uint8_t, voxone::ha_name::kStorageBytes> storage{};
  storage.fill(0xa5);

  TftTheme theme = THEME_YAMAHA;
  assert(!voxone::tft_theme::decode(storage.data(), storage.size(), theme));
  assert(theme == THEME_DEFAULT);

  assert(voxone::ha_name::encode("Salon", 5, storage.data(), storage.size()));
  assert(voxone::tft_theme::encode(
      THEME_YAMAHA, storage.data(), storage.size()));
  assert(voxone::tft_theme::decode(storage.data(), storage.size(), theme));
  assert(theme == THEME_YAMAHA);
  char name[voxone::ha_name::kRuntimeCapacity]{};
  assert(voxone::ha_name::decode(
      storage.data(), storage.size(), name, sizeof(name)));
  assert(std::strcmp(name, "Salon") == 0);

  assert(voxone::ha_name::encode("Kuchnia", 7, storage.data(), storage.size()));
  assert(voxone::tft_theme::decode(storage.data(), storage.size(), theme));
  assert(theme == THEME_YAMAHA);

  storage[voxone::tft_theme::kOffset + 4] = 99;
  assert(!voxone::tft_theme::decode(storage.data(), storage.size(), theme));
  assert(theme == THEME_DEFAULT);
  assert(!voxone::tft_theme::parseApiName("other", theme));
  assert(voxone::tft_theme::parseApiName("yamaha", theme));
  assert(theme == THEME_YAMAHA);

  static_assert(voxone::yamaha_tft::kWidth == 480, "TFT width changed");
  static_assert(voxone::yamaha_tft::kHeight == 320, "TFT height changed");
  static_assert(voxone::yamaha_tft::kBackground == 0x0000,
                "Yamaha background must be pure black");
  static_assert(voxone::yamaha_tft::kAmber == 0xfd80,
                "Yamaha amber must encode #FFB000");
  assert(voxone::yamaha_tft::boundedSignalLevel(9) == 4);
  return 0;
}
