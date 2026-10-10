#ifndef VOXONE_PLAYER_DISPLAY_METADATA_H
#define VOXONE_PLAYER_DISPLAY_METADATA_H

struct PlayerDisplayMetadata {
  const char* displayArtist;
  const char* displayTitle;
};

inline PlayerDisplayMetadata normalizePlayerMetadataForDisplay(
    const char* artist, const char* title) {
  const char* rawArtist = artist ? artist : "";
  const char* rawTitle = title ? title : "";
  if (rawArtist[0]) return {rawArtist, rawTitle};
  return {rawTitle, ""};
}

#endif
