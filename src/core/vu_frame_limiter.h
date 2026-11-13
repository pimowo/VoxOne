#ifndef VOXONE_VU_FRAME_LIMITER_H
#define VOXONE_VU_FRAME_LIMITER_H

#include <stdint.h>

namespace voxone {
namespace vu {

constexpr uint32_t kFrameIntervalMs = 50;  // 20 FPS maximum.

inline bool frameDue(uint32_t nowMs, uint32_t lastFrameMs) {
  return static_cast<uint32_t>(nowMs - lastFrameMs) >= kFrameIntervalMs;
}

}  // namespace vu
}  // namespace voxone

#endif
