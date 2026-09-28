#include "../src/core/station_metadata.h"
#include <cassert>
#include <cstring>

int main() {
  char output[64];
  const auto normal = parseStationMetadata("Artist - Title", false);
  assert(normal.split && normal.artistLength == 6 && normal.titleLength == 5);
  stationMetaDisplay("Artist - Title", false, output, sizeof(output));
  assert(std::strcmp(output, "Artist - Title") == 0);
  const auto swapped = parseStationMetadata("Title - Artist", true);
  assert(swapped.split && swapped.artistLength == 6 && swapped.titleLength == 5);
  stationMetaDisplay("Title - Artist", true, output, sizeof(output));
  assert(std::strcmp(output, "Artist - Title") == 0);
  const auto unsplit = parseStationMetadata("One field", true);
  assert(!unsplit.split && unsplit.titleLength == 0);
  stationMetaDisplay("One field", true, output, sizeof(output));
  assert(std::strcmp(output, "One field") == 0);
}
