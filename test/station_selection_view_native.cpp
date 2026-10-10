#include "../src/core/station_selection_view.h"

#include <cassert>

int main() {
  auto empty = stationSelectionView(0, 0, 0, false);
  assert(empty.empty());
  assert(stationSelectionRelative(empty, -1) == 0);
  assert(stationSelectionRelative(empty, 1) == 0);
  assert(!empty.selectedIsPlaying());

  auto first = stationSelectionView(5, 1, 1, true);
  assert(!first.empty() && first.selected == 1);
  assert(first.selectedIsPlaying());
  assert(stationSelectionRelative(first, -1) == 5);
  assert(stationSelectionRelative(first, 1) == 2);

  auto last = stationSelectionView(5, 5, 2, true);
  assert(stationSelectionRelative(last, -1) == 4);
  assert(stationSelectionRelative(last, 1) == 1);
  assert(!last.selectedIsPlaying());

  auto stopped = stationSelectionView(5, 2, 2, false);
  assert(!stopped.selectedIsPlaying());

  auto outOfRange = stationSelectionView(5, 9, 2, true);
  assert(outOfRange.selected == 1);
  assert(stationSelectionRelative(outOfRange, -1) == 5);
  assert(stationSelectionRelative(outOfRange, 1) == 2);

  auto noCurrent = stationSelectionView(5, 0, 0, false);
  assert(noCurrent.selected == 1);
  assert(stationSelectionRelative(noCurrent, -1) == 5);
  assert(stationSelectionRelative(noCurrent, 1) == 2);
}
