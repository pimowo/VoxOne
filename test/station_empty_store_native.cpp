#include "../src/core/station_format.h"
#include "../src/core/station_store_state.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

int main() {
  // No canonical file, backup, or legacy source is a new empty device.
  assert(stationStoreNeedsEmptyInitialization(false, false, false));
  assert(!stationStoreNeedsEmptyInitialization(true, false, false));
  assert(!stationStoreNeedsEmptyInitialization(false, true, false));
  assert(!stationStoreNeedsEmptyInitialization(false, false, true));

  // First recovery writes the canonical header and a zero-byte index.
  std::string stations = kStationsHeader;
  size_t indexBytes = 0;
  assert(stations == "#VOXONE_STATIONS\t1\n");
  assert(stationIndexSizeMatches(indexBytes, 0));

  // A second boot/recovery recognizes the initialized empty store.
  assert(!stationStoreNeedsEmptyInitialization(true, false, false));
  assert(stationIndexSizeMatches(indexBytes, 0));

  // First ADD serializes and parses through the same v1 record codec.
  char id[17];
  stationFormatId(0x0123456789ABCDEFULL, id);
  const std::string firstLine = std::string(id) +
      "\tStation\thttps://stream.example/radio\t0\tnormal";
  const std::string firstRecord = firstLine + "\n";
  char parseBuffer[128];
  std::memcpy(parseBuffer, firstLine.c_str(), firstLine.size() + 1);
  char* name = nullptr;
  char* url = nullptr;
  uint64_t parsedId = 0;
  int ovol = -1;
  uint8_t metadataMode = 255;
  assert(stationParseFields(parseBuffer, parsedId, name, url, ovol, metadataMode));
  assert(parsedId == 0x0123456789ABCDEFULL);
  assert(std::strcmp(name, "Station") == 0);
  assert(std::strcmp(url, "https://stream.example/radio") == 0);
  assert(ovol == 0 && metadataMode == STATION_META_NORMAL);
  stations += firstRecord;
  indexBytes = sizeof(uint32_t);
  assert(stationIndexSizeMatches(indexBytes, 1));
}
