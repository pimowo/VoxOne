#ifndef VOXONE_PLAYLIST_MAPPING_H
#define VOXONE_PLAYLIST_MAPPING_H

#include <stdint.h>

enum class StationRuntimeAction : uint8_t { None, Refresh, Play, Stop };

constexpr StationRuntimeAction stationRuntimeAction(bool affectsActive,
                                                     bool hasSelection,
                                                     bool reconnect) {
  return !affectsActive ? StationRuntimeAction::None :
         !hasSelection ? StationRuntimeAction::Stop :
         reconnect ? StationRuntimeAction::Play : StationRuntimeAction::Refresh;
}

struct StationRefreshChanges {
  bool volume;
  bool metadataMode;
};

constexpr StationRefreshChanges stationRefreshChanges(
    int previousOvol, uint8_t previousMetadataMode,
    int currentOvol, uint8_t currentMetadataMode) {
  return {previousOvol != currentOvol,
          previousMetadataMode != currentMetadataMode};
}

constexpr uint16_t stationAfterMove(uint16_t current, uint16_t from, uint16_t to) {
  return current == from ? to :
         from < to && current > from && current <= to ? current - 1 :
         from > to && current >= to && current < from ? current + 1 : current;
}

constexpr uint16_t stationAfterDelete(uint16_t current, uint16_t removed,
                                      uint16_t remaining) {
  return current == 0 ? 0 : removed < current ? current - 1 :
         removed > current ? current : remaining == 0 ? 0 :
         removed <= remaining ? removed : remaining;
}

static_assert(stationAfterMove(3, 1, 5) == 2, "move forward: inside");
static_assert(stationAfterMove(1, 3, 5) == 1, "move forward: before");
static_assert(stationAfterMove(6, 1, 5) == 6, "move forward: after");
static_assert(stationAfterMove(1, 1, 5) == 5, "move current forward");
static_assert(stationAfterMove(3, 5, 1) == 4, "move backward: inside");
static_assert(stationAfterMove(6, 5, 1) == 6, "move backward: after");
static_assert(stationAfterMove(5, 5, 1) == 1, "move current backward");
static_assert(stationAfterDelete(5, 1, 12) == 4, "delete before current");
static_assert(stationAfterDelete(5, 8, 12) == 5, "delete after current");
static_assert(stationAfterDelete(5, 5, 12) == 5, "delete current middle");
static_assert(stationAfterDelete(13, 13, 12) == 12, "delete current last");
static_assert(stationAfterDelete(1, 1, 0) == 0, "delete sole station");
static_assert(stationAfterDelete(0, 1, 0) == 0, "empty selection");
static_assert(stationRuntimeAction(false, true, false) ==
              StationRuntimeAction::None, "inactive mutation");
static_assert(stationRuntimeAction(true, true, false) ==
              StationRuntimeAction::Refresh, "active metadata/OVOL refresh");
static_assert(stationRuntimeAction(true, true, true) ==
              StationRuntimeAction::Play, "active reconnect");
static_assert(stationRuntimeAction(true, false, false) ==
              StationRuntimeAction::Stop, "active station removed");

#endif
