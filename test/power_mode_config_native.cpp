#include "../src/core/power_mode_config.h"

#include <array>
#include <cassert>
#include <cstring>

int main() {
  std::array<uint8_t, voxone::ha_name::kStorageBytes> storage{};
  storage.fill(0xa5);

  PowerMode mode = MODE_HARD_POWER;
  assert(!voxone::power_mode::decode(storage.data(), storage.size(), mode));
  assert(mode == MODE_SOFT_STANDBY);

  assert(voxone::ha_name::encode("Salon", 5, storage.data(), storage.size()));
  assert(voxone::tft_theme::encode(
      THEME_YAMAHA, storage.data(), storage.size()));
  assert(voxone::power_mode::encode(
      MODE_HARD_POWER, storage.data(), storage.size()));
  assert(voxone::power_mode::decode(storage.data(), storage.size(), mode));
  assert(mode == MODE_HARD_POWER);

  char name[voxone::ha_name::kRuntimeCapacity]{};
  TftTheme theme = THEME_DEFAULT;
  assert(voxone::ha_name::decode(
      storage.data(), storage.size(), name, sizeof(name)));
  assert(std::strcmp(name, "Salon") == 0);
  assert(voxone::tft_theme::decode(storage.data(), storage.size(), theme));
  assert(theme == THEME_YAMAHA);

  assert(voxone::power_mode::parseApiName("mute_only", mode));
  assert(mode == MODE_MUTE_ONLY);
  assert(std::strcmp(voxone::power_mode::apiName(mode), "mute_only") == 0);
  assert(!voxone::power_mode::parseApiName("invalid", mode));

  storage[voxone::power_mode::kOffset + 4] = 99;
  assert(!voxone::power_mode::decode(storage.data(), storage.size(), mode));
  assert(mode == MODE_SOFT_STANDBY);
  static_assert(voxone::power_mode::kOffset +
                    voxone::power_mode::kEncodedBytes <=
                voxone::ha_name::kStorageBytes,
                "Power mode extension exceeds Config v7 reserved storage");
  return 0;
}
