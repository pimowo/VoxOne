#include "../src/core/carousel_marquee.h"

#include <cassert>

int main() {
  voxone::carousel::MarqueeState marquee;
  marquee.reset(120, 80, 6, 1000);
  assert(marquee.active());
  assert(marquee.offset() == 0);
  assert(marquee.cycleWidth() == 138);
  assert(!marquee.step(1999));
  assert(marquee.step(2000));
  assert(marquee.offset() == 1);
  assert(!marquee.step(2039));
  assert(marquee.step(2040));
  assert(marquee.offset() == 2);

  for (uint16_t offset = 2; offset < 138; ++offset)
    assert(marquee.step(2040 + (offset - 1) * 40));
  assert(marquee.offset() == 0);
  const uint32_t wrapTime = 2040 + 136 * 40;
  assert(!marquee.step(wrapTime + 999));
  assert(marquee.step(wrapTime + 1000));

  marquee.reset(80, 80, 6, 0xFFFFFFF0u);
  assert(!marquee.active());
  assert(!marquee.step(2000));

  marquee.reset(81, 80, 6, 0xFFFFFF00u);
  assert(!marquee.step(0x000002E7u));
  assert(marquee.step(0x000002E8u));
}
