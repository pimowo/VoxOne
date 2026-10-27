#include "../src/displays/oled128x32_layout.h"

#include <cassert>

using namespace voxone::oled128x32;

int main() {
  assert(kWidth == 128 && kHeight == 32 && kRowHeight == 8);
  assert(fits(kPlayerStation));
  assert(fits(kPlayerArtist));
  assert(fits(kPlayerTitle));
  assert(fits(kPlayerStatus));
  assert(fits(kVolumeLabel));
  assert(fits(kVolumeValue));
  assert(fits(kVolumeFooter));
  assert(!fits(Bounds{0, 25, 128, 8}));
  assert(!fits(Bounds{121, 0, 8, 8}));
}
