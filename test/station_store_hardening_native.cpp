#include "../src/core/playlist_mapping.h"
#include "../src/core/playlist_validation.h"
#include "../src/core/station_format.h"
#include "../src/core/station_store_state.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace {

struct Row {
  uint64_t id;
  std::string name;
  std::string url;
  int ovol;
  uint8_t metadataMode;
};

bool operator==(const Row& left, const Row& right) {
  return left.id == right.id && left.name == right.name &&
         left.url == right.url && left.ovol == right.ovol &&
         left.metadataMode == right.metadataMode;
}

uint32_t crc32(const std::string& bytes) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (unsigned char byte : bytes) crc = playlistCrcByte(crc, byte);
  return crc ^ 0xFFFFFFFFUL;
}

std::string revision(const std::string& bytes) {
  char text[9];
  std::snprintf(text, sizeof(text), "%08X", crc32(bytes));
  return text;
}

std::string serialize(const std::vector<Row>& rows) {
  std::string output = kStationsHeader;
  for (const Row& row : rows) {
    char id[17];
    stationFormatId(row.id, id);
    output += id;
    output += '\t';
    output += row.name;
    output += '\t';
    output += row.url;
    output += '\t';
    output += std::to_string(row.ovol);
    output += '\t';
    output += row.metadataMode == STATION_META_SWAP ? "swap\n" : "normal\n";
  }
  return output;
}

bool parseRecord(const std::string& text, Row& row) {
  std::vector<char> mutableText(text.begin(), text.end());
  mutableText.push_back('\0');
  char* name = nullptr;
  char* url = nullptr;
  if (!stationParseFields(mutableText.data(), row.id, name, url, row.ovol,
                          row.metadataMode)) return false;
  row.name = name;
  row.url = url;
  return true;
}

bool parseStore(const std::string& bytes, std::vector<Row>& rows,
                std::vector<uint32_t>& offsets) {
  const std::string header = kStationsHeader;
  if (bytes.compare(0, header.size(), header) != 0) return false;
  std::vector<Row> parsed;
  std::vector<uint32_t> parsedOffsets;
  std::set<uint64_t> ids;
  std::size_t position = header.size();
  while (position < bytes.size()) {
    std::size_t end = bytes.find('\n', position);
    if (end == std::string::npos) end = bytes.size();
    if (end == position) return false;
    std::string record = bytes.substr(position, end - position);
    if (!record.empty() && record.back() == '\r') record.pop_back();
    Row row{};
    if (!parseRecord(record, row) || !ids.insert(row.id).second) return false;
    parsedOffsets.push_back(static_cast<uint32_t>(position));
    parsed.push_back(row);
    position = end == bytes.size() ? end : end + 1;
  }
  rows.swap(parsed);
  offsets.swap(parsedOffsets);
  return true;
}

bool validIndex(const std::string& bytes, const std::vector<uint32_t>& index) {
  std::vector<Row> rows;
  std::vector<uint32_t> expected;
  return parseStore(bytes, rows, expected) && expected == index &&
         stationIndexSizeMatches(index.size() * sizeof(uint32_t), rows.size());
}

std::vector<Row> makeRows(std::size_t count, bool maximumFields = false) {
  std::vector<Row> rows;
  rows.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    char name[32];
    char url[64];
    std::snprintf(name, sizeof(name), "Radio %03u", static_cast<unsigned>(i));
    std::snprintf(url, sizeof(url), "https://stream.example/%03u",
                  static_cast<unsigned>(i));
    rows.push_back({static_cast<uint64_t>(i + 1),
                    maximumFields ? std::string(169, 'N') : name,
                    maximumFields ? std::string(169, 'U') : url,
                    maximumFields ? -30 : static_cast<int>(i % 7) - 3,
                    i % 2 ? STATION_META_SWAP : STATION_META_NORMAL});
  }
  return rows;
}

struct Store {
  std::string stations;
  std::vector<uint32_t> index;
  uint16_t current = 0;

  bool commit(const std::vector<Row>& rows, uint16_t selected,
              std::size_t totalBytes = SIZE_MAX,
              std::size_t usedBytes = 0) {
    const std::string candidate = serialize(rows);
    if (selected > rows.size() ||
        !stationCommitHasSpace(totalBytes, usedBytes, candidate.size(),
                               rows.size())) return false;
    std::vector<Row> verified;
    std::vector<uint32_t> candidateIndex;
    if (!parseStore(candidate, verified, candidateIndex) || verified != rows)
      return false;
    stations = candidate;
    index = candidateIndex;
    current = selected;
    return true;
  }
};

struct InterruptedFiles {
  std::string active;
  std::string backup;
  std::string temporary;
  std::vector<uint32_t> index;
  std::vector<uint32_t> indexBackup;
  std::vector<uint32_t> indexTemporary;
};

bool validStations(const std::string& bytes) {
  std::vector<Row> rows;
  std::vector<uint32_t> offsets;
  return !bytes.empty() && parseStore(bytes, rows, offsets);
}

bool recover(InterruptedFiles& files) {
  const bool activeValid = validStations(files.active);
  const bool backupValid = validStations(files.backup);
  const StationRecoverySource source = stationRecoverySource(
      !files.active.empty(), activeValid, !files.backup.empty(), backupValid,
      false);
  if (source == StationRecoverySource::Backup) files.active = files.backup;
  else if (source != StationRecoverySource::Active) return false;
  std::vector<Row> rows;
  std::vector<uint32_t> offsets;
  if (!parseStore(files.active, rows, offsets)) return false;
  if (!validIndex(files.active, files.index)) files.index = offsets;
  files.backup.clear();
  files.temporary.clear();
  files.indexBackup.clear();
  files.indexTemporary.clear();
  return true;
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
  for (const std::size_t count : {0u, 1u, 50u, 100u, 250u}) {
    const std::vector<Row> source = makeRows(count);
    Store store;
    const uint16_t selected = count ? static_cast<uint16_t>(count / 2 + 1) : 0;
    assert(store.commit(source, selected));
    assert(store.current == selected);
    assert(store.index.size() == count && validIndex(store.stations, store.index));
    std::vector<Row> imported;
    std::vector<uint32_t> offsets;
    assert(parseStore(store.stations, imported, offsets));
    assert(imported == source && offsets == store.index);
    for (std::size_t i = 0; i < source.size(); ++i) {
      uint16_t position = 0;
      assert(stationFindPositionById(imported, source[i].id, position));
      assert(position == i + 1);
    }
    assert(serialize(imported) == store.stations);
  }

  const std::vector<Row> representative = makeRows(250);
  const std::vector<Row> maximum = makeRows(250, true);
  assert(serialize(representative).size() == 15627);
  assert(serialize(maximum).size() == 91769);
  assert(250 * sizeof(uint32_t) == 1000);

  std::vector<Row> rows = makeRows(10);
  const std::vector<Row> original = rows;
  const uint64_t activeId = rows[4].id;
  uint16_t current = 5;
  auto move = [&](std::size_t from, std::size_t to) {
    const Row moved = rows[from - 1];
    rows.erase(rows.begin() + from - 1);
    rows.insert(rows.begin() + to - 1, moved);
    current = stationAfterMove(current, from, to);
    uint16_t position = 0;
    assert(stationFindPositionById(rows, activeId, position));
    assert(position == current);
    std::set<uint64_t> ids;
    for (const Row& row : rows) assert(ids.insert(row.id).second);
    Store store;
    assert(store.commit(rows, current));
    assert(validIndex(store.stations, store.index));
  };
  move(1, 2);   // adjacent
  move(1, 10);  // first -> last
  move(10, 1);  // last -> first
  move(5, 1);   // middle -> first
  move(5, 10);  // middle -> last
  move(3, 7);
  move(8, 2);
  assert(rows.size() == original.size());

  assert(stationAfterDelete(5, 2, 9) == 4);
  assert(stationAfterDelete(5, 8, 9) == 5);
  assert(stationAfterDelete(5, 5, 9) == 5);
  assert(stationAfterDelete(1, 1, 9) == 1);
  assert(stationAfterDelete(10, 10, 9) == 9);
  assert(stationAfterDelete(1, 1, 0) == 0);
  assert(stationAfterDelete(0, 1, 0) == 0);
  std::vector<Row> deleting = makeRows(5);
  uint16_t deletingCurrent = 3;
  while (!deleting.empty()) {
    const uint16_t removed = static_cast<uint16_t>(deleting.size());
    deleting.erase(deleting.end() - 1);
    deletingCurrent = stationAfterDelete(
        deletingCurrent, removed, static_cast<uint16_t>(deleting.size()));
    assert(deletingCurrent <= deleting.size());
  }
  assert(deletingCurrent == 0);

  rows = makeRows(3);
  const uint64_t preserved = rows[1].id;
  rows[1].name = "Renamed";
  rows[1].url = "https://changed";
  rows[1].ovol = 12;
  rows[1].metadataMode = STATION_META_SWAP;
  assert(rows[1].id == preserved);

  const std::string oldBytes = serialize(makeRows(3));
  const std::string newBytes = serialize(makeRows(4));
  std::vector<Row> ignoredRows;
  std::vector<uint32_t> oldIndex, newIndex;
  assert(parseStore(oldBytes, ignoredRows, oldIndex));
  assert(parseStore(newBytes, ignoredRows, newIndex));
  std::vector<InterruptedFiles> cuts = {
      {oldBytes, "", "", oldIndex, {}, {}},                    // A
      {oldBytes, "", newBytes, oldIndex, {}, {}},              // B
      {"", oldBytes, newBytes, {}, oldIndex, {}},              // C
      {newBytes, oldBytes, "", oldIndex, {}, newIndex},        // D/E
      {newBytes, oldBytes, "", {}, oldIndex, newIndex},        // F
      {newBytes, oldBytes, "", newIndex, oldIndex, {}},        // G
      {newBytes, oldBytes, "", newIndex, oldIndex, newIndex}}; // H
  for (InterruptedFiles files : cuts) {
    assert(recover(files));
    assert(files.active == oldBytes || files.active == newBytes);
    assert(validIndex(files.active, files.index));
    assert(files.backup.empty() && files.temporary.empty() &&
           files.indexBackup.empty() && files.indexTemporary.empty());
  }

  for (std::vector<uint32_t> broken : {
           std::vector<uint32_t>{}, std::vector<uint32_t>{1},
           std::vector<uint32_t>{oldIndex[0], oldIndex[1]},
           std::vector<uint32_t>{oldIndex[0], oldIndex[1], 0xFFFFFFFFu}}) {
    InterruptedFiles files{oldBytes, "", "", broken, {}, {}};
    assert(recover(files));
    assert(files.index == oldIndex);
  }

  Store noSpace;
  assert(noSpace.commit(makeRows(3), 2));
  const std::string oldActive = noSpace.stations;
  const std::vector<uint32_t> oldActiveIndex = noSpace.index;
  const uint16_t oldCurrent = noSpace.current;
  const std::string larger = serialize(makeRows(250));
  const std::size_t required = larger.size() + 250 * sizeof(uint32_t) +
                               kStationCommitReserveBytes;
  assert(stationCommitHasSpace(196608, 196608 - required, larger.size(), 250));
  assert(!stationCommitHasSpace(196608, 196608 - required + 1,
                                larger.size(), 250));
  assert(!stationCommitHasSpace(196608, 196608 - larger.size() - 1000,
                                larger.size(), 250));
  assert(!noSpace.commit(makeRows(250), 125, 196608,
                         196608 - larger.size() - 1000));
  assert(noSpace.stations == oldActive && noSpace.index == oldActiveIndex &&
         noSpace.current == oldCurrent);

  std::vector<Row> parsed;
  std::vector<uint32_t> offsets;
  const std::string valid = serialize(makeRows(2));
  assert(parseStore(valid, parsed, offsets));
  const std::vector<Row> unchanged = parsed;
  assert(!parseStore("#VOXONE_STATIONS\t2\n", parsed, offsets));
  assert(!parseStore(std::string(kStationsHeader) +
      "0000000000000000\tName\thttps://url\t0\tnormal\n", parsed, offsets));
  assert(!parseStore(std::string(kStationsHeader) +
      "0000000000000001\tName\thttps://url\t31\tnormal\n", parsed, offsets));
  assert(!parseStore(std::string(kStationsHeader) +
      "0000000000000001\tName\thttps://url\t0\twrong\n", parsed, offsets));
  assert(!parseStore(std::string(kStationsHeader) +
      "0000000000000001\tBad\xC0\xAF\thttps://url\t0\tnormal\n", parsed, offsets));
  assert(!parseStore(std::string(kStationsHeader) +
      "0000000000000001\tBad\tName\thttps://url\t0\tnormal\n", parsed, offsets));
  assert(!parseStore(std::string(kStationsHeader) +
      "0000000000000001\tBad\nName\thttps://url\t0\tnormal\n", parsed, offsets));
  assert(!parseStore(valid + valid.substr(std::strlen(kStationsHeader)),
                     parsed, offsets));
  assert(!parseStore(valid.substr(0, valid.size() - 3), parsed, offsets));
  assert(parsed == unchanged);

  const std::string changedOrder = serialize({makeRows(2)[1], makeRows(2)[0]});
  assert(revision(valid) != revision(changedOrder));
  assert(revision(valid) == revision(valid));
  assert(revision(valid) == revision(serialize(unchanged)));
  std::vector<Row> edited = unchanged;
  edited[0].name += " changed";
  assert(revision(valid) != revision(serialize(edited)));

  assert(stationRecoverySource(true, true, true, true, false) ==
         StationRecoverySource::Active);
  assert(stationRecoverySource(false, false, true, true, false) ==
         StationRecoverySource::Backup);
  assert(stationRecoverySource(false, false, false, false, false) ==
         StationRecoverySource::InitializeEmpty);
  assert(stationRecoverySource(false, false, false, false, true) ==
         StationRecoverySource::Legacy);
  assert(stationRecoverySource(true, false, false, false, false) ==
         StationRecoverySource::Fail);

  const std::string storeSource = readFile("src/core/playlist_store.cpp");
  const std::string storeHeader = readFile("src/core/playlist_store.h");
  const std::string serverSource = readFile("src/core/netserver.cpp");
  assert(storeSource.find("xSemaphoreCreateRecursiveMutex") != std::string::npos);
  assert(storeSource.find("xSemaphoreTakeRecursive") != std::string::npos);
  assert(storeSource.find("PlaylistWriteError PlaylistStore::commit") !=
         std::string::npos);
  assert(storeSource.find("PlaylistReadError PlaylistStore::readImport") !=
         std::string::npos);
  assert(storeSource.find("scanRows(kCsvTmp, &newRevision, &rows") !=
         std::string::npos);
  for (const char* timingField : {"lockUs", "validateUs", "stageUs", "writeUs",
                                  "verifyUs", "indexUs", "installUs",
                                  "persistUs", "cleanupUs", "totalUs"}) {
    assert(storeHeader.find(timingField) != std::string::npos);
    assert(storeSource.find("timing." + std::string(timingField)) !=
           std::string::npos);
  }
  for (const char* diagnosticField : {"##[PLAYLIST-DIAG]#", "commitUs=",
                                      "buf=%lu->%lu", "action=%s",
                                      "reconnect=%u", "active=%016llX->%016llX",
                                      "ovol=%d->%d", "targetOvol=%d"})
    assert(serverSource.find(diagnosticField) != std::string::npos);
  assert(serverSource.find("playlistStore.commit(rows, newCurrent, newRevision, "
                           "&commitDiagnostics)") != std::string::npos);
  for (const char* responseField : {"\\\"revision\\\"", "\\\"current\\\"",
                                    "\\\"count\\\""})
    assert(serverSource.find(responseField) != std::string::npos);
}
