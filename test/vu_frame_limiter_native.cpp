#include "../src/core/vu_frame_limiter.h"

#include <cassert>
#include <cstdint>

int main() {
  using voxone::vu::frameDue;
  using voxone::vu::kFrameIntervalMs;
  static_assert(kFrameIntervalMs == 50, "VU rendering must not exceed 20 FPS");
  assert(!frameDue(49, 0));
  assert(frameDue(50, 0));
  assert(!frameDue(1049, 1000));
  assert(frameDue(1050, 1000));
  assert(frameDue(25, UINT32_MAX - 30));
  return 0;
}
