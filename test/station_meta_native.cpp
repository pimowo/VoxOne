#include "../src/core/station_metadata.h"
#include "../src/core/player_display_metadata.h"
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

  const auto playerSplit = playerStationMetadata("Artist - Long title", false);
  assert(playerSplit.split && playerSplit.artistLength == 6 && playerSplit.titleLength == 10);
  const auto playerSwapped = playerStationMetadata("Title - Artist", true);
  assert(playerSwapped.split && playerSwapped.artistLength == 6 && playerSwapped.titleLength == 5);
  const auto playerUnsplit = playerStationMetadata("Song without separator", false);
  assert(!playerUnsplit.split && playerUnsplit.artistLength == 0 &&
         playerUnsplit.titleLength == std::strlen("Song without separator"));
  stationMetaCopy(output, sizeof(output), playerUnsplit.title, playerUnsplit.titleLength);
  assert(std::strcmp(output, "Song without separator") == 0);
  const auto playerEmpty = playerStationMetadata("", false);
  assert(!playerEmpty.split && playerEmpty.artistLength == 0 && playerEmpty.titleLength == 0);
  stationMetaCopy(output, sizeof(output), playerEmpty.title, playerEmpty.titleLength);
  assert(output[0] == '\0');
  const auto playerChangedAgain = playerStationMetadata("New song", false);
  stationMetaCopy(output, sizeof(output), playerChangedAgain.title, playerChangedAgain.titleLength);
  assert(std::strcmp(output, "New song") == 0);
  const auto playerShort = playerStationMetadata("New", false);
  stationMetaCopy(output, sizeof(output), playerShort.title, playerShort.titleLength);
  assert(std::strcmp(output, "New") == 0 && output[3] == '\0');

  const char rawArtist[] = "Italove";
  const char rawTitle[] = "Magic Night";
  const auto both = normalizePlayerMetadataForDisplay(rawArtist, rawTitle);
  assert(std::strcmp(both.displayArtist, "Italove") == 0);
  assert(std::strcmp(both.displayTitle, "Magic Night") == 0);
  const auto artistOnly = normalizePlayerMetadataForDisplay(rawArtist, "");
  assert(std::strcmp(artistOnly.displayArtist, "Italove") == 0);
  assert(std::strcmp(artistOnly.displayTitle, "") == 0);
  const auto titleOnly = normalizePlayerMetadataForDisplay("", rawTitle);
  assert(std::strcmp(titleOnly.displayArtist, "Magic Night") == 0);
  assert(std::strcmp(titleOnly.displayTitle, "") == 0);
  const auto neither = normalizePlayerMetadataForDisplay("", "");
  assert(std::strcmp(neither.displayArtist, "") == 0);
  assert(std::strcmp(neither.displayTitle, "") == 0);
  assert(std::strcmp(rawArtist, "Italove") == 0);
  assert(std::strcmp(rawTitle, "Magic Night") == 0);
}
