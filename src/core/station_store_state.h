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

constexpr size_t kStationCommitReserveBytes = 1024;

inline bool stationCommitHasSpace(size_t totalBytes, size_t usedBytes,
                                  size_t stationsBytes,
                                  size_t stationCount) {
  if (usedBytes > totalBytes ||
      stationCount > (SIZE_MAX - stationsBytes -
                      kStationCommitReserveBytes) / sizeof(uint32_t))
    return false;
  const size_t required = stationsBytes +
                          stationCount * sizeof(uint32_t) +
                          kStationCommitReserveBytes;
  return required <= totalBytes - usedBytes;
}

enum class StationRecoverySource : uint8_t {
  Active,
  Backup,
  InitializeEmpty,
  Legacy,
  Fail
};

inline StationRecoverySource stationRecoverySource(bool activeExists,
                                                    bool activeValid,
                                                    bool backupExists,
                                                    bool backupValid,
                                                    bool legacyExists) {
  if (activeValid) return StationRecoverySource::Active;
  if (backupValid) return StationRecoverySource::Backup;
  if (activeExists || backupExists) return StationRecoverySource::Fail;
  return legacyExists ? StationRecoverySource::Legacy
                      : StationRecoverySource::InitializeEmpty;
}

#endif
