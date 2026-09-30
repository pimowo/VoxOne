#include "../src/core/salon_player_scroll.h"

#include <cassert>

using Action = SalonPlayerScroll::Action;

static void expect(SalonPlayerScroll::Event event, Action action, int row) {
  assert(event.action == action && event.row == row);
}

int main() {
  SalonPlayerScroll scroll;
  const bool allLong[3] = {true, true, true};
  const bool stationOnly[3] = {true, false, false};
  const bool skipArtist[3] = {true, false, true};
  const bool allShort[3] = {false, false, false};

  expect(scroll.tick(0, allLong), Action::None, -1);
  scroll.enter();
  expect(scroll.tick(100, allLong), Action::Start, 0);
  expect(scroll.tick(2599, allLong), Action::None, 0);
  expect(scroll.tick(2600, allLong), Action::None, 0);
  expect(scroll.tick(2649, allLong), Action::None, 0);
  expect(scroll.tick(2650, allLong), Action::Step, 0);
  expect(scroll.tick(2699, allLong), Action::None, 0);
  expect(scroll.tick(2700, allLong), Action::Step, 0);
  scroll.cycleFinished();
  expect(scroll.tick(2710, allLong), Action::Start, 1);
  scroll.cycleFinished();
  expect(scroll.tick(2720, allLong), Action::Start, 2);
  scroll.cycleFinished();
  expect(scroll.tick(2730, allLong), Action::Start, 0);

  scroll.enter();
  expect(scroll.tick(0, stationOnly), Action::Start, 0);
  scroll.cycleFinished();
  expect(scroll.tick(1000, stationOnly), Action::Start, 0);
  expect(scroll.tick(3499, stationOnly), Action::None, 0);
  expect(scroll.tick(3500, stationOnly), Action::None, 0);
  expect(scroll.tick(3550, stationOnly), Action::Step, 0);

  scroll.enter();
  expect(scroll.tick(0, skipArtist), Action::Start, 0);
  scroll.cycleFinished();
  expect(scroll.tick(10, skipArtist), Action::Start, 2);
  scroll.enter();
  expect(scroll.tick(0, allShort), Action::None, -1);

  scroll.enter();
  expect(scroll.tick(0, allLong), Action::Start, 0);
  scroll.textChanged(1);
  expect(scroll.tick(100, allLong), Action::Start, 1);
  expect(scroll.tick(2599, allLong), Action::None, 1);
  scroll.textChanged(1);
  scroll.textChanged(2);
  expect(scroll.tick(2600, allLong), Action::Start, 2);

  scroll.leave();
  expect(scroll.tick(10000, allLong), Action::None, -1);
  scroll.enter();
  expect(scroll.tick(10010, allLong), Action::Start, 0);

  // Radio and Bluetooth supply the same three text fields to this scheduler.
  scroll.textChanged(1);
  expect(scroll.tick(10100, allLong), Action::Start, 1);
  scroll.textChanged(0);
  expect(scroll.tick(10200, allLong), Action::Start, 0);
}
