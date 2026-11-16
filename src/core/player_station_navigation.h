#ifndef VOXONE_PLAYER_STATION_NAVIGATION_H
#define VOXONE_PLAYER_STATION_NAVIGATION_H

#include <stdint.h>

namespace voxone {
namespace player_navigation {

inline uint16_t previous(uint16_t current, uint16_t count) {
  if (count == 0) return 0;
  return current <= 1 || current > count ? count : current - 1;
}

inline uint16_t next(uint16_t current, uint16_t count) {
  if (count == 0) return 0;
  return current == 0 || current >= count ? 1 : current + 1;
}

}  // namespace player_navigation
}  // namespace voxone

#endif
