#ifndef VOXONE_PLAYER_DISPLAY_VIEW_H
#define VOXONE_PLAYER_DISPLAY_VIEW_H

#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "display_audio_info.h"
#include "player_display_metadata.h"
#include "station_metadata.h"

// Own the prepared text: SourceManager and Config may update their buffers later.
struct PlayerDisplayView {
  char station[170]{};
  char artist[170]{};
  char title[170]{};
  char audioText[20]{};
  DisplaySourceKind source = DisplaySourceKind::Radio;
  DisplayPlaybackState playback = DisplayPlaybackState::Stopped;
  uint8_t userVolume = 0;
  bool muted = false;
  bool btConnected = false;
  uint8_t wifiLevel = 0;
  uint32_t sampleRate = 0;
  DisplayAudioInfo audioInfo{};
  const char* audioCodecLabel = "";
};

inline uint8_t playerWifiLevel(int rssi) {
  if (rssi >= 0 || rssi < -80) return 0;
  if (rssi >= -50) return 4;
  if (rssi >= -60) return 3;
  if (rssi >= -70) return 2;
  return 1;
}

inline uint32_t playerDisplayNextCodepoint(const char*& text, const char* end) {
  const uint8_t first = static_cast<uint8_t>(*text++);
  if (first < 0x80) return first;
  const uint8_t extra = (first & 0xE0) == 0xC0 ? 1 :
                        (first & 0xF0) == 0xE0 ? 2 :
                        (first & 0xF8) == 0xF0 ? 3 : 0;
  if (extra == 0 || end - text < extra) return first;
  uint32_t codepoint = first & (0x7F >> extra);
  for (uint8_t i = 0; i < extra; ++i) {
    const uint8_t next = static_cast<uint8_t>(text[i]);
    if ((next & 0xC0) != 0x80) return first;
    codepoint = (codepoint << 6) | (next & 0x3F);
  }
  text += extra;
  return codepoint;
}

inline uint32_t playerDisplayFoldCodepoint(uint32_t value) {
  if (value >= 'A' && value <= 'Z') return value + ('a' - 'A');
  if ((value >= 0xC0 && value <= 0xD6) || (value >= 0xD8 && value <= 0xDE) ||
      (value >= 0x410 && value <= 0x42F)) return value + 0x20;
  switch (value) {
    case 0x104: case 0x106: case 0x118: case 0x141:
    case 0x143: case 0x15A: case 0x179: case 0x17B:
      return value + 1;
    case 0x401: return 0x451;
    default: return value;
  }
}

inline bool playerDisplayArtistIsStation(const char* artist, const char* station) {
  const char* artistEnd = artist + strlen(artist);
  const char* stationEnd = station + strlen(station);
  while (artist < artistEnd && isspace(static_cast<unsigned char>(*artist))) ++artist;
  while (station < stationEnd && isspace(static_cast<unsigned char>(*station))) ++station;
  while (artistEnd > artist && isspace(static_cast<unsigned char>(artistEnd[-1]))) --artistEnd;
  while (stationEnd > station && isspace(static_cast<unsigned char>(stationEnd[-1]))) --stationEnd;
  while (artist < artistEnd && station < stationEnd) {
    if (playerDisplayFoldCodepoint(playerDisplayNextCodepoint(artist, artistEnd)) !=
        playerDisplayFoldCodepoint(playerDisplayNextCodepoint(station, stationEnd))) return false;
  }
  return artist == artistEnd && station == stationEnd;
}

inline void makePlayerDisplayView(PlayerDisplayView& out,
                                  const DisplaySourceView& source,
                                  const char* stationName,
                                  const char* rawRadioMetadata,
                                  bool swapArtistTitle,
                                  uint8_t userVolume, bool muted,
                                  bool btConnected, int rssi,
                                  uint16_t radioBitrate,
                                  BitrateFormat radioFormat) {
  const char* name = stationName ? stationName : "";
  snprintf(out.station, sizeof(out.station), "%s", displayPlayerStationText(source, name));
  out.source = source.kind;
  out.playback = source.playback;
  out.userVolume = userVolume;
  out.muted = muted;
  out.btConnected = btConnected;
  out.wifiLevel = playerWifiLevel(rssi);
  out.sampleRate = source.sampleRate;
  out.audioInfo = selectDisplayAudioInfo(source, radioBitrate, radioFormat);
  out.audioCodecLabel = out.audioInfo.bluetooth ? out.audioInfo.top
                                                : displayRadioFormatLabel(out.audioInfo.radioFormat);
  formatDisplayAudioInfo(out.audioText, sizeof(out.audioText), source,
                         radioBitrate, radioFormat);

  const char* artist = "";
  const char* title = "";
  char radioArtist[170]{};
  char radioTitle[170]{};
  if (source.kind == DisplaySourceKind::Bluetooth) {
    artist = source.artist;
    title = source.title;
  } else if (rawRadioMetadata && rawRadioMetadata[0]) {
    const StationMetadataParts parts = playerStationMetadata(rawRadioMetadata, swapArtistTitle);
    stationMetaCopy(radioArtist, sizeof(radioArtist), parts.artist, parts.artistLength);
    stationMetaCopy(radioTitle, sizeof(radioTitle), parts.title, parts.titleLength);
    artist = radioArtist;
    title = radioTitle;
    if (parts.split && playerDisplayArtistIsStation(artist, name)) artist = "";
    if (!parts.split && playerDisplayArtistIsStation(title, name)) title = "";
  }
  const PlayerDisplayMetadata shown = normalizePlayerMetadataForDisplay(artist, title);
  snprintf(out.artist, sizeof(out.artist), "%s", shown.displayArtist);
  snprintf(out.title, sizeof(out.title), "%s", shown.displayTitle);
}

void capturePlayerDisplayView(PlayerDisplayView& out, int rssi);

#endif
