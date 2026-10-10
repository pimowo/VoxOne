#ifndef VOXONE_STATION_SELECTION_VIEW_H
#define VOXONE_STATION_SELECTION_VIEW_H

#include <stdint.h>

struct StationSelectionView {
  uint16_t total = 0;
  uint16_t selected = 0;
  uint16_t playing = 0;
  bool playerRunning = false;

  bool empty() const { return total == 0; }
  bool selectedIsPlaying() const {
    return playerRunning && selected != 0 && selected == playing;
  }
};

inline StationSelectionView stationSelectionView(uint16_t total,
                                                 uint16_t selected,
                                                 uint16_t playing,
                                                 bool playerRunning) {
  StationSelectionView view;
  view.total = total;
  view.selected = total != 0 && (selected < 1 || selected > total) ? 1 : selected;
  view.playing = playing;
  view.playerRunning = playerRunning;
  return view;
}

inline uint16_t stationSelectionRelative(const StationSelectionView& view,
                                         int8_t offset) {
  if (view.empty()) return 0;
  int32_t index = static_cast<int32_t>(view.selected) + offset;
  while (index < 1) index += view.total;
  while (index > view.total) index -= view.total;
  return static_cast<uint16_t>(index);
}

#endif
