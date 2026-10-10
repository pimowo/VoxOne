#include "../src/core/st7796_player_scroll.h"
#include "../src/displays/widgets/scroll_text_state.h"

#include <cassert>
#include <cstdio>
#include <cstring>

using Action = St7796PlayerScroll::Action;

static void expect(St7796PlayerScroll::Event event, Action action, int row) {
  assert(event.action == action && event.row == row);
}

int main() {
  // AP startup reports bootStep 2 without creating the PLAYER page.
  int meta = 0, title1 = 0, title2 = 0;
  assert(!st7796PlayerScrollReady(false, true, false, &meta, &title1, nullptr));
  assert(!st7796PlayerScrollReady(false, true, true, &meta, &title1, nullptr));
  assert(!st7796PlayerScrollReady(true, true, true, &meta, &title1, nullptr));
  assert(!st7796PlayerScrollReady(true, true, false, &meta, &title1, &title2));
  assert(st7796PlayerScrollReady(true, true, true, &meta, &title1, &title2));
  assert(!st7796PlayerScrollReady(true, false, true, &meta, &title1, &title2));

  St7796PlayerScroll scroll;
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
  // ST7796 allocation (27 bytes requested for a 460 px row at 18 px/char).
  const size_t st7796Capacity = scrollWindowCapacity(480, 18);
  const size_t st7796Frame = scrollWindowPrintCapacity(st7796Capacity, 460, 18, 2);
  assert(st7796Capacity == 28 && st7796Frame == 27);
  char window[29];
  std::memset(window, '#', sizeof(window));
  std::snprintf(window, st7796Frame, "%s", "abcdefghijklmnopqrstuvwxyz");
  assert(window[st7796Frame - 1] == '\0');
  assert(window[st7796Capacity] == '#');
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
  assert(titleOffset == 2);  // ST7789 284x76 starts at its row's left edge.
}
