#include "../src/core/salon_player_scroll.h"
#include "../src/displays/widgets/scroll_text_state.h"

#include <cassert>
#include <cstdio>
#include <cstring>

using Action = SalonPlayerScroll::Action;

static void expect(SalonPlayerScroll::Event event, Action action, int row) {
  assert(event.action == action && event.row == row);
}

int main() {
  // AP startup reports bootStep 2 without creating the PLAYER page.
  int meta = 0, title1 = 0, title2 = 0;
  assert(!salonPlayerScrollReady(false, true, false, &meta, &title1, nullptr));
  assert(!salonPlayerScrollReady(false, true, true, &meta, &title1, nullptr));
  assert(!salonPlayerScrollReady(true, true, true, &meta, &title1, nullptr));
  assert(!salonPlayerScrollReady(true, true, false, &meta, &title1, &title2));
  assert(salonPlayerScrollReady(true, true, true, &meta, &title1, &title2));
  assert(!salonPlayerScrollReady(true, false, true, &meta, &title1, &title2));

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

  // A framebuffer frame needs one more byte than the old MAX_WIDTH-based
  // allocation on SALON (27 bytes requested for a 460 px row at 18 px/char).
  const size_t salonCapacity = scrollWindowCapacity(480, 18);
  const size_t salonFrame = scrollWindowPrintCapacity(salonCapacity, 460, 18, 2);
  assert(salonCapacity == 28 && salonFrame == 27);
  char window[29];
  std::memset(window, '#', sizeof(window));
  std::snprintf(window, salonFrame, "%s", "abcdefghijklmnopqrstuvwxyz");
  assert(window[salonFrame - 1] == '\0');
  assert(window[salonCapacity] == '#');
  assert(scrollWindowPrintCapacity(scrollWindowCapacity(284, 12), 280, 12, 1) == 24);
  assert(scrollWindowPrintCapacity(scrollWindowCapacity(480, 30), 480, 30, 2) == 18);

  int16_t stationOffset = -20, artistOffset = -40, titleOffset = -60;
  assert(scrollTextChangedAndResetOffset("Radio A", "Telefon", stationOffset, 0));
  assert(stationOffset == 0 && artistOffset == -40 && titleOffset == -60);
  assert(scrollTextChangedAndResetOffset("Artist - previous", "Artist", artistOffset, 0));
  assert(artistOffset == 0 && titleOffset == -60);
  assert(scrollTextChangedAndResetOffset("Old title", "", titleOffset, 0));
  assert(titleOffset == 0);
  titleOffset = -12;
  assert(!scrollTextChangedAndResetOffset("", "", titleOffset, 0) && titleOffset == -12);
  // The last drawn value may equal the new value after changes on another page.
  assert(scrollTextChangedAndResetOffset("Temporary title", "Old title", titleOffset, 0));
  assert(titleOffset == 0);
  titleOffset = -9;
  assert(scrollTextChangedAndResetOffset("Old title", "New title", titleOffset, 2));
  assert(titleOffset == 2);  // DESK starts at its row's left edge.
}
