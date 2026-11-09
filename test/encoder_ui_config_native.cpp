#include "../src/core/encoder_ui_config.h"

#include <array>
#include <cassert>

int main() {
  std::array<uint8_t, voxone::ha_name::kStorageBytes> storage{};
  bool reversed = true;

  // Missing record is the Config v7 migration path and defaults to NORMAL.
  assert(!voxone::encoder_ui::decode(
      storage.data(), storage.size(), reversed));
  assert(!reversed);

  assert(voxone::ha_name::encode(
      "Salon", 5, storage.data(), storage.size()));
  assert(voxone::tft_theme::encode(
      THEME_YAMAHA, storage.data(), storage.size()));
  assert(voxone::power_mode::encode(
      MODE_MUTE_ONLY, storage.data(), storage.size()));
  assert(voxone::encoder_ui::encode(
      true, storage.data(), storage.size()));
  assert(voxone::encoder_ui::decode(
      storage.data(), storage.size(), reversed));
  assert(reversed);

  PowerMode powerMode = MODE_SOFT_STANDBY;
  TftTheme theme = THEME_DEFAULT;
  char name[voxone::ha_name::kRuntimeCapacity]{};
  assert(voxone::ha_name::decode(
      storage.data(), storage.size(), name, sizeof(name)));
  assert(voxone::tft_theme::decode(
      storage.data(), storage.size(), theme));
  assert(voxone::power_mode::decode(
      storage.data(), storage.size(), powerMode));
  assert(theme == THEME_YAMAHA && powerMode == MODE_MUTE_ONLY);

  storage[voxone::encoder_ui::kOffset + 4] = 2;
  assert(!voxone::encoder_ui::decode(
      storage.data(), storage.size(), reversed));
  assert(!reversed);
  static_assert(voxone::encoder_ui::kOffset +
                    voxone::encoder_ui::kEncodedBytes <=
                voxone::ha_name::kStorageBytes,
                "encoder UI extension exceeds Config v7 reserved bytes");
}
