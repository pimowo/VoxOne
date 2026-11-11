#ifndef VOXONE_CAROUSEL_LIST_WINDOW_H
#define VOXONE_CAROUSEL_LIST_WINDOW_H

#include <stdint.h>

namespace voxone {
namespace carousel {

struct ListWindow {
  uint16_t first;
  uint16_t count;
};

// selected is one-based to match the carousel counter shown to the user.
inline ListWindow listWindow(uint16_t total, uint16_t selected,
                             uint16_t capacity) {
  if (total == 0 || capacity == 0) return {0, 0};
  if (selected == 0) selected = 1;
  if (selected > total) selected = total;
  const uint16_t visible = total < capacity ? total : capacity;
  const uint16_t selectedIndex = selected - 1;
  uint16_t first = selectedIndex > visible / 2
                       ? selectedIndex - visible / 2
                       : 0;
  if (first + visible > total) first = total - visible;
  return {first, visible};
}

}  // namespace carousel
}  // namespace voxone

#endif
