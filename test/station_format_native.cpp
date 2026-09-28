#include "../src/core/station_format.h"
#include "../src/core/playlist_mapping.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <set>
#include <string>
#include <vector>

struct Row {
  uint64_t id;
  std::string name;
  std::string url;
  int ovol;
  uint8_t metadataMode;
};

static bool parse(std::string input, Row& row) {
  std::vector<char> line(input.begin(), input.end());
  line.push_back('\0');
  char* name = nullptr;
  char* url = nullptr;
  if (!stationParseFields(line.data(), row.id, name, url, row.ovol, row.metadataMode)) return false;
  row.name = name;
  row.url = url;
  return true;
}

static bool import(std::string text, std::vector<Row>& rows, std::vector<uint32_t>& offsets) {
  const std::string header = kStationsHeader;
  if (text.compare(0, header.size(), header) != 0) return false;
  std::vector<Row> staged;
  std::vector<uint32_t> stagedOffsets;
  std::set<uint64_t> ids;
  size_t pos = header.size();
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    Row row{};
    if (!parse(text.substr(pos, end - pos), row) || !ids.insert(row.id).second)
      return false;
    stagedOffsets.push_back(static_cast<uint32_t>(pos));
    staged.push_back(row);
    pos = end == text.size() ? end : end + 1;
  }
  rows.swap(staged);
  offsets.swap(stagedOffsets);
  return true;
}

int main() {
  assert(std::strcmp(kStationsHeader, "#VOXONE_STATIONS\t1\n") == 0);
  uint64_t id = 0;
  assert(stationParseId("8F23A17C4D9012AA", id) && id != 0);
  char idText[17];
  stationFormatId(id, idText);
  assert(std::strcmp(idText, "8F23A17C4D9012AA") == 0);
  assert(!stationParseId("0000000000000000", id));
  assert(!stationParseId("GG23A17C4D9012AA", id));
  assert(!stationParseId("8f23a17c4d9012aa", id));

  std::vector<Row> rows;
  std::vector<uint32_t> offsets;
  const std::string first = "8F23A17C4D9012AA\tRadio 357\thttps://example/stream\t0\tnormal\n";
  const std::string second = "C13B0718D22A1134\tRadio XYZ\thttps://example/xyz\t-3\tswap\n";
  assert(import(std::string(kStationsHeader) + first + second, rows, offsets));
  assert(rows.size() == 2 && rows[0].metadataMode == STATION_META_NORMAL);
  assert(rows[1].metadataMode == STATION_META_SWAP && rows[1].ovol == -3);
  assert(offsets[0] == std::strlen(kStationsHeader));
  assert(offsets[1] == std::strlen(kStationsHeader) + first.size());
  const auto initialOffsets = offsets;
  offsets.clear(); // Rebuild from data after losing the binary index.
  assert(import(std::string(kStationsHeader) + first + second, rows, offsets));
  assert(offsets == initialOffsets);
  assert(stationImportCurrent(rows, 0, 0) == 0); // Imported, but no station selected.
  assert(stationImportCurrent(rows, 9, 0) == 2);
  assert(stationImportCurrent(rows, 0, rows[1].id) == 2);
  assert(stationImportCurrent(std::vector<Row>{}, 0, 0) == 0);
  const std::string deviceIds =
      std::string(kStationsHeader) +
      "A26179BA77EC6119\tNET default\thttps://example/net\t0\tnormal\n" +
      "AA96D10A5B27DC64\tItalo4you\thttps://example/italo\t-3\tswap\n" +
      "4AFC02DC3F08F3AC\tRMF Dla dzieci\thttps://example/rmf\t0\tnormal\n";
  std::vector<Row> roundTripRows;
  std::vector<uint32_t> roundTripOffsets;
  assert(import(deviceIds, roundTripRows, roundTripOffsets));
  std::string exported = kStationsHeader;
  for (const Row& row : roundTripRows) {
    char preservedId[17];
    stationFormatId(row.id, preservedId);
    exported += std::string(preservedId) + "\t" + row.name + "\t" + row.url +
                "\t" + std::to_string(row.ovol) + "\t" +
                (row.metadataMode == STATION_META_SWAP ? "swap\n" : "normal\n");
  }
  assert(exported == deviceIds);
  assert(stationImportCurrent(roundTripRows, 0, 0) == 0);
  assert(stationImportCurrent(roundTripRows, 0, roundTripRows[2].id) == 3);
  assert(std::string(kStationsHeader) + first + second.substr(0, 16) ==
         (std::string(kStationsHeader) + first + second).substr(0, offsets[1] + 16));
  const auto original = rows;
  assert(!import("#VOXONE_STATIONS\t2\n" + first, rows, offsets));
  assert(!import("bad header\n" + first, rows, offsets));
  assert(!import(std::string(kStationsHeader) + first + first, rows, offsets));
  assert(!import(std::string(kStationsHeader) +
      "8F23A17C4D9012AA\tName\thttps://url\t0\twrong\n", rows, offsets));
  assert(!import(std::string(kStationsHeader) +
      "8F23A17C4D9012AA\tName\thttps://url\t31\tnormal\n", rows, offsets));
  assert(!import(std::string(kStationsHeader) +
      "0000000000000000\tName\thttps://url\t0\tnormal\n", rows, offsets));
  assert(!import(std::string(kStationsHeader) +
      "8F23A17C4D9012AA\tName\thttps://url\t0\tnormal\textra\n", rows, offsets));
  assert(rows.size() == original.size() && rows[0].id == original[0].id);

  std::swap(rows[0], rows[1]);
  assert(stationAfterMove(1, 1, 2) == 2 && rows[1].id == original[0].id);
  uint16_t position = 0;
  assert(stationFindPositionById(rows, original[0].id, position) && position == 2);
  assert(!stationFindPositionById(rows, 0, position));
  unsigned randomStep = 0;
  const uint64_t fresh = stationGenerateId(rows, [&randomStep]() -> uint32_t {
    const uint32_t values[] = {0, 0, 0xC13B0718, 0xD22A1134, 0x12345678, 0xABCDEF01};
    return values[randomStep++];
  });
  assert(fresh == 0x12345678ABCDEF01ULL); // Zero and collision are retried.
  assert(stationRevisionMatches("a1B2c3d4", "A1B2C3D4"));
  assert(!stationRevisionMatches("A1B2C3D5", "A1B2C3D4"));
  for (const size_t count : {size_t(50), size_t(100), size_t(250)}) {
    std::vector<Row> many;
    std::set<uint64_t> ids;
    for (size_t i = 1; i <= count; ++i) {
      Row row{static_cast<uint64_t>(i), "Radio", "https://stream", 0, STATION_META_NORMAL};
      assert(ids.insert(row.id).second);
      many.push_back(row);
    }
    assert(many.size() == count);
    uint16_t last = 0;
    assert(stationFindPositionById(many, count, last) && last == count);
  }
  rows[1].name = "Renamed";
  rows[1].url = "https://changed";
  rows[1].metadataMode = STATION_META_SWAP;
  assert(rows[1].id == original[0].id);
  rows.erase(rows.begin());
  assert(rows.size() == 1 && rows[0].id == original[0].id);
  char legacy[] = "Old radio\thttps://old\t4";
  char* name = nullptr;
  char* url = nullptr;
  int ovol = 0;
  assert(stationParseLegacyFields(legacy, name, url, ovol));
  assert(std::strcmp(name, "Old radio") == 0 && ovol == 4);
  assert(!stationParseLegacyFields(legacy, name, url, ovol)); // Already split in place.
  std::vector<Row> migrated{{0, "Old A", "https://a", 0, 255},
                            {0, "Old B", "https://b", -3, 255}};
  uint32_t nextId = 1;
  assert(stationAssignMigratedIds(migrated, [&nextId](const std::vector<Row>& current) {
    return stationGenerateId(current, [&nextId]() -> uint32_t {
      const uint32_t part = nextId++;
      return part;
    });
  }));
  assert(migrated[0].id && migrated[1].id && migrated[0].id != migrated[1].id);
  assert(migrated[0].name == "Old A" && migrated[1].name == "Old B");
  assert(migrated[0].metadataMode == STATION_META_NORMAL &&
         migrated[1].metadataMode == STATION_META_NORMAL);
  const std::vector<Row> legacyUntouched{{0, "Kept", "https://old", 0, 255}};
  auto failedMigration = legacyUntouched;
  assert(!stationAssignMigratedIds(failedMigration, [](const std::vector<Row>&) {
    return uint64_t(0);
  }));
  assert(legacyUntouched[0].name == "Kept" && legacyUntouched[0].id == 0);
}
