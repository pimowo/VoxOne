#ifndef VOXONE_STATION_STORE_STATE_H
#define VOXONE_STATION_STORE_STATE_H

#include <cstddef>
#include <cstdint>

inline bool stationStoreNeedsEmptyInitialization(bool hasStations,
                                                 bool hasBackup,
                                                 bool hasLegacyPlaylist) {
  return !hasStations && !hasBackup && !hasLegacyPlaylist;
}

inline bool stationIndexSizeMatches(size_t indexBytes, size_t stationCount) {
  return indexBytes == stationCount * sizeof(uint32_t);
}

#endif
