#include "../src/core/power_manager.h"

#include <cassert>

using voxone::power::ButtonGesture;
using voxone::power::ButtonGestureDetector;

int main() {
  ButtonGestureDetector button;
  button.reset(false, 0);

  // Contact bounce never becomes a press.
  assert(button.update(true, 10) == ButtonGesture::None);
  assert(button.update(false, 30) == ButtonGesture::None);
  assert(button.update(false, 80) == ButtonGesture::None);

  // A debounced press shorter than one second reports once on release.
  assert(button.update(true, 100) == ButtonGesture::None);
  assert(button.update(true, 150) == ButtonGesture::None);
  assert(button.update(false, 600) == ButtonGesture::None);
  assert(button.update(false, 650) == ButtonGesture::ShortPress);
  assert(button.update(false, 700) == ButtonGesture::None);

  // A 1..3 second hold is intentionally neither short nor long.
  assert(button.update(true, 1000) == ButtonGesture::None);
  assert(button.update(true, 1050) == ButtonGesture::None);
  assert(button.update(false, 2550) == ButtonGesture::None);
  assert(button.update(false, 2600) == ButtonGesture::None);

  // Long press fires while held and cannot turn into a short press later.
  assert(button.update(true, 3000) == ButtonGesture::None);
  assert(button.update(true, 3050) == ButtonGesture::None);
  assert(button.update(true, 6050) == ButtonGesture::LongPress);
  assert(button.update(true, 7000) == ButtonGesture::None);
  assert(button.update(false, 7010) == ButtonGesture::None);
  assert(button.update(false, 7060) == ButtonGesture::None);
  return 0;
}
