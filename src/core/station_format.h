#ifndef VOXONE_STATION_FORMAT_H
#define VOXONE_STATION_FORMAT_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "playlist_validation.h"

constexpr char kStationsHeader[] = "#VOXONE_STATIONS\t1\n";
constexpr uint8_t STATION_META_NORMAL = 0;
constexpr uint8_t STATION_META_SWAP = 1;

inline bool stationParseId(const char* text, uint64_t& id) {
  if (!text || std::strlen(text) != 16) return false;
  id = 0;
  for (unsigned i = 0; i < 16; ++i) {
    const char c = text[i];
    unsigned digit;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
    else return false;
    id = (id << 4) | digit;
  }
  return id != 0;
}

inline void stationFormatId(uint64_t id, char output[17]) {
  constexpr char hex[] = "0123456789ABCDEF";
  for (int i = 15; i >= 0; --i) {
    output[i] = hex[id & 15];
    id >>= 4;
  }
  output[16] = '\0';
}

template <typename Rows>
inline bool stationFindPositionById(const Rows& rows, uint64_t id, uint16_t& position) {
  if (!id) return false;
  for (size_t i = 0; i < rows.size(); ++i)
    if (rows[i].id == id) { position = static_cast<uint16_t>(i + 1); return true; }
  return false;
}

template <typename Rows>
inline uint16_t stationImportCurrent(const Rows& rows, uint16_t previousCurrent,
                                    uint64_t activeId) {
  if (rows.empty()) return 0;
  uint16_t position = 0;
  if (stationFindPositionById(rows, activeId, position)) return position;
  return previousCurrent > rows.size() ? static_cast<uint16_t>(rows.size()) :
                                         previousCurrent;
}

template <typename Rows, typename Random32>
inline uint64_t stationGenerateId(const Rows& rows, Random32 random32) {
  for (unsigned attempt = 0; attempt < 32; ++attempt) {
    const uint64_t id = (static_cast<uint64_t>(random32()) << 32) | random32();
    if (!id) continue;
    uint16_t ignored = 0;
    if (!stationFindPositionById(rows, id, ignored)) return id;
  }
  return 0;
}

template <typename Rows, typename Generate>
inline bool stationAssignMigratedIds(Rows& rows, Generate generate) {
  for (auto& row : rows) {
    row.id = generate(rows);
    row.metadataMode = STATION_META_NORMAL;
    if (!row.id) return false;
  }
  return true;
}

inline bool stationRevisionMatches(const char* requested, const char* current) {
  if (!requested || !current || std::strlen(requested) != 8 ||
      std::strlen(current) != 8) return false;
  for (size_t i = 0; i < 8; ++i) {
    char ch = requested[i];
    if (ch >= 'a' && ch <= 'f') ch -= 'a' - 'A';
    if (ch != current[i]) return false;
  }
  return true;
}

inline bool stationParseFields(char* line, uint64_t& id, char*& name, char*& url,
                              int& ovol, uint8_t& metadataMode) {
  char* fields[5] = {line, nullptr, nullptr, nullptr, nullptr};
  for (int i = 1; i < 5; ++i) {
    char* tab = std::strchr(fields[i - 1], '\t');
    if (!tab) return false;
    *tab = '\0';
    fields[i] = tab + 1;
  }
  if (std::strchr(fields[4], '\t') || !stationParseId(fields[0], id) ||
      !playlistValidField(fields[1], std::strlen(fields[1]), true) ||
      !playlistValidField(fields[2], std::strlen(fields[2]), false) ||
      !playlistParseInteger(fields[3], std::strlen(fields[3]), ovol) ||
      !playlistValidOvol(ovol)) return false;
  if (std::strcmp(fields[4], "normal") == 0) metadataMode = STATION_META_NORMAL;
  else if (std::strcmp(fields[4], "swap") == 0) metadataMode = STATION_META_SWAP;
  else return false;
  name = fields[1];
  url = fields[2];
  return true;
}

inline bool stationParseLegacyFields(char* line, char*& name, char*& url, int& ovol) {
  char* first = std::strchr(line, '\t');
  char* second = first ? std::strchr(first + 1, '\t') : nullptr;
  if (!first || !second || std::strchr(second + 1, '\t')) return false;
  *first = *second = '\0';
  name = line;
  url = first + 1;
  return playlistValidField(name, std::strlen(name), true) &&
         playlistValidField(url, std::strlen(url), false) &&
         playlistParseInteger(second + 1, std::strlen(second + 1), ovol) &&
         playlistValidOvol(ovol);
}

#endif
