#include "../src/core/return_player_timeout.h"
#include "../src/core/ui_timeout_config.h"

#include <cassert>

int main() {
  UiTimeoutSettings settings{};
  assert(settings.stationListSeconds == 10);
  assert(settings.btTransportSeconds == 20);

  ReturnPlayerTimeout timer;
  timer.armForMode(1000, settings.stationListSeconds, STATIONS);
  assert(!timer.poll(10999, STATIONS));
  assert(timer.poll(11000, STATIONS));
  assert(!timer.poll(11001, STATIONS));

  // A local action restarts the idle period from that action.
  timer.armForMode(1000, settings.stationListSeconds, STATIONS);
  timer.armForMode(9000, settings.stationListSeconds, STATIONS);
  assert(!timer.poll(11000, STATIONS));
  assert(timer.poll(19000, STATIONS));

  settings.stationListSeconds = 0;
  timer.armForMode(1000, settings.stationListSeconds, STATIONS);
  assert(!timer.poll(100000, STATIONS));

  timer.armForMode(1000, settings.btTransportSeconds, BT_TRANSPORT);
  assert(!timer.poll(20999, BT_TRANSPORT));
  assert(timer.poll(21000, BT_TRANSPORT));
  // Both rotation and click use the same idle reset request in BT TRANSPORT.
  timer.armForMode(1000, settings.btTransportSeconds, BT_TRANSPORT);
  timer.armForMode(15000, settings.btTransportSeconds, BT_TRANSPORT);  // rotation
  assert(!timer.poll(21000, BT_TRANSPORT));
  timer.armForMode(25000, settings.btTransportSeconds, BT_TRANSPORT);  // click
  assert(!timer.poll(35000, BT_TRANSPORT));
  assert(timer.poll(45000, BT_TRANSPORT));

  settings.btTransportSeconds = 0;
  timer.armForMode(1000, settings.btTransportSeconds, BT_TRANSPORT);
  assert(!timer.poll(100000, BT_TRANSPORT));
  assert(settings.stationListSeconds == 0 && settings.btTransportSeconds == 0);

  // Leaving a mode cancels its pending return, including across millis wrap.
  timer.armForMode(1000, 20, BT_TRANSPORT);
  assert(!timer.poll(2000, PLAYER));
  assert(!timer.poll(22000, BT_TRANSPORT));
  timer.armForMode(0xFFFFFFF0UL, 10, STATIONS);
  assert(!timer.poll(0x000026FFUL, STATIONS));
  assert(timer.poll(0x00002700UL, STATIONS));

  uint8_t parsed = 255;
  assert(parseUiTimeout("0", parsed) && parsed == 0);
  assert(parseUiTimeout("120", parsed) && parsed == 120);
  assert(!parseUiTimeout("121", parsed));
  assert(!parseUiTimeout("-1", parsed));
  assert(!parseUiTimeout("1.5", parsed));
  assert(!parseUiTimeout("", parsed));
  assert(!parseUiTimeout("999999999999", parsed));
}
