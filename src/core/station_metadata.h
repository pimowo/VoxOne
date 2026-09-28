#ifndef VOXONE_STATION_METADATA_H
#define VOXONE_STATION_METADATA_H

#include <algorithm>
#include <cstddef>
#include <cstring>
#include "station_format.h"

struct StationMetadataParts {
  const char* artist;
  size_t artistLength;
  const char* title;
  size_t titleLength;
  bool split;
};

inline StationMetadataParts parseStationMetadata(const char* raw, bool swapArtistTitle) {
  if (!raw) raw = "";
  const char* separator = std::strstr(raw, " - ");
  if (!separator) return {raw, std::strlen(raw), "", 0, false};
  const char* right = separator + 3;
  const size_t leftLength = static_cast<size_t>(separator - raw);
  const size_t rightLength = std::strlen(right);
  return swapArtistTitle
      ? StationMetadataParts{right, rightLength, raw, leftLength, true}
      : StationMetadataParts{raw, leftLength, right, rightLength, true};
}

inline void stationMetaCopy(char* output, size_t capacity, const char* text, size_t length) {
  if (!capacity) return;
  const size_t copy = std::min(length, capacity - 1);
  std::memcpy(output, text, copy);
  output[copy] = '\0';
}

inline void stationMetaDisplay(const char* raw, bool swapArtistTitle,
                               char* output, size_t capacity) {
  if (!capacity) return;
  const StationMetadataParts parts = parseStationMetadata(raw, swapArtistTitle);
  if (!parts.split) {
    stationMetaCopy(output, capacity, parts.artist, parts.artistLength);
    return;
  }
  size_t used = std::min(parts.artistLength, capacity - 1);
  std::memcpy(output, parts.artist, used);
  for (const char ch : {' ', '-', ' '})
    if (used < capacity - 1) output[used++] = ch;
  const size_t titleBytes = std::min(parts.titleLength, capacity - 1 - used);
  std::memcpy(output + used, parts.title, titleBytes);
  output[used + titleBytes] = '\0';
}

#endif
