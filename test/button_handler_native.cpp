#include "../src/core/button_handler.h"

#include <cassert>
#include <stdint.h>

using voxone::power::ButtonEvent;
using voxone::power::ButtonHandler;

namespace {
void expect(ButtonHandler& button, bool pressed, uint32_t nowMs,
            ButtonEvent expected) {
  assert(button.update(pressed, nowMs) == expected);
}
}  // namespace

int main() {
  ButtonHandler button;
  button.reset(false, 0);

  // Contact bounce never becomes a press or a click.
  expect(button, true, 10, ButtonEvent::BUTTON_NONE);
  expect(button, false, 30, ButtonEvent::BUTTON_NONE);
  expect(button, false, 80, ButtonEvent::BUTTON_NONE);

  // A short release is held until the double-click interval expires.
  expect(button, true, 100, ButtonEvent::BUTTON_NONE);
  expect(button, true, 150, ButtonEvent::BUTTON_NONE);
  expect(button, false, 300, ButtonEvent::BUTTON_NONE);
  expect(button, false, 350, ButtonEvent::BUTTON_NONE);
  expect(button, false, 749, ButtonEvent::BUTTON_NONE);
  expect(button, false, 750, ButtonEvent::BUTTON_CLICK);
  expect(button, false, 800, ButtonEvent::BUTTON_NONE);

  // Two short clicks inside the interval emit only a double click.
  expect(button, true, 1000, ButtonEvent::BUTTON_NONE);
  expect(button, true, 1050, ButtonEvent::BUTTON_NONE);
  expect(button, false, 1150, ButtonEvent::BUTTON_NONE);
  expect(button, false, 1200, ButtonEvent::BUTTON_NONE);
  expect(button, true, 1500, ButtonEvent::BUTTON_NONE);
  expect(button, true, 1550, ButtonEvent::BUTTON_NONE);
  expect(button, false, 1650, ButtonEvent::BUTTON_NONE);
  expect(button, false, 1700, ButtonEvent::BUTTON_DOUBLE_CLICK);
  expect(button, false, 2100, ButtonEvent::BUTTON_NONE);

  // A second click at the 400 ms boundary is a separate gesture.
  expect(button, true, 2200, ButtonEvent::BUTTON_NONE);
  expect(button, true, 2250, ButtonEvent::BUTTON_NONE);
  expect(button, false, 2350, ButtonEvent::BUTTON_NONE);
  expect(button, false, 2400, ButtonEvent::BUTTON_NONE);
  expect(button, true, 2800, ButtonEvent::BUTTON_CLICK);
  expect(button, true, 2850, ButtonEvent::BUTTON_NONE);
  expect(button, false, 2950, ButtonEvent::BUTTON_NONE);
  expect(button, false, 3000, ButtonEvent::BUTTON_NONE);
  expect(button, false, 3400, ButtonEvent::BUTTON_CLICK);

  // A press released at 400 ms is not a short click.
  expect(button, true, 3500, ButtonEvent::BUTTON_NONE);
  expect(button, true, 3550, ButtonEvent::BUTTON_NONE);
  expect(button, false, 3900, ButtonEvent::BUTTON_NONE);
  expect(button, false, 3950, ButtonEvent::BUTTON_NONE);
  expect(button, false, 4500, ButtonEvent::BUTTON_NONE);

  // Long press fires once while held and never turns into a click on release.
  expect(button, true, 5000, ButtonEvent::BUTTON_NONE);
  expect(button, true, 5050, ButtonEvent::BUTTON_NONE);
  expect(button, true, 8049, ButtonEvent::BUTTON_NONE);
  expect(button, true, 8050, ButtonEvent::BUTTON_LONG_PRESS);
  expect(button, true, 9000, ButtonEvent::BUTTON_NONE);
  expect(button, false, 9010, ButtonEvent::BUTTON_NONE);
  expect(button, false, 9060, ButtonEvent::BUTTON_NONE);
  expect(button, false, 9500, ButtonEvent::BUTTON_NONE);

  // Unsigned elapsed-time arithmetic keeps all thresholds valid at rollover.
  button.reset(false, UINT32_MAX - 500U);
  expect(button, true, UINT32_MAX - 400U, ButtonEvent::BUTTON_NONE);
  expect(button, true, UINT32_MAX - 350U, ButtonEvent::BUTTON_NONE);
  expect(button, false, UINT32_MAX - 250U, ButtonEvent::BUTTON_NONE);
  expect(button, false, UINT32_MAX - 200U, ButtonEvent::BUTTON_NONE);
  expect(button, false, 198U, ButtonEvent::BUTTON_NONE);
  expect(button, false, 199U, ButtonEvent::BUTTON_CLICK);
  return 0;
}
