#include "../src/core/config_format.h"

#include <cassert>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>

using namespace voxone::config_format;

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void hasNone(const std::string& source,
             std::initializer_list<const char*> tokens) {
  for (const char* token : tokens) {
    assert(source.find(token) == std::string::npos);
  }
}

void hasAll(const std::string& source,
            std::initializer_list<const char*> tokens) {
  for (const char* token : tokens) {
    assert(source.find(token) != std::string::npos);
  }
}
}  // namespace

int main() {
  static_assert(sizeof(config_v5_t) == 250, "Config v5 size changed");
  static_assert(sizeof(config_v6_t) == 254, "Config v6 size changed");
  static_assert(kConfigV7SerializedSize == 255, "Config v7 size changed");
  static_assert(offsetof(config_v5_t, reservedSdStation) == 184,
                "Config v5 reserved SD station offset changed");
  static_assert(offsetof(config_v5_t, reservedSdFlags) == 186,
                "Config v5 reserved SD flags offset changed");
  static_assert(offsetof(config_v5_t, reservedPlayMode) == 190,
                "Config v5 reserved play mode offset changed");
  static_assert(offsetof(config_v6_t, reservedSdStation) == 184,
                "Config v6 reserved SD station offset changed");
  static_assert(offsetof(config_v6_t, reservedSdFlags) == 186,
                "Config v6 reserved SD flags offset changed");
  static_assert(offsetof(config_v6_t, reservedPlayMode) == 190,
                "Config v6 reserved play mode offset changed");
  static_assert(kConfigV7ReservedSdStationOffset == 181,
                "Config v7 reserved SD station offset changed");
  static_assert(kConfigV7ReservedSdFlagsOffset == 183,
                "Config v7 reserved SD flags offset changed");
  static_assert(kConfigV7ReservedPlayModeOffset == 187,
                "Config v7 reserved play mode offset changed");

  const auto configHeader = readFile("src/core/config.h");
  const auto configSource = readFile("src/core/config.cpp");
  const auto playerHeader = readFile("src/core/player.h");
  const auto playerSource = readFile("src/core/player.cpp");
  const auto networkHeader = readFile("src/core/network.h");
  const auto networkSource = readFile("src/core/network.cpp");
  const auto common = readFile("src/core/common.h");
  const auto display = readFile("src/core/display.cpp");
  const auto timekeeper = readFile("src/core/timekeeper.cpp");
  const auto mainSource = readFile("src/main.cpp");
  const auto widgets = readFile("src/displays/widgets/widgets.cpp");
  const auto server = readFile("src/core/netserver.cpp");
  const auto serverHeader = readFile("src/core/netserver.h");
  const auto playlistStore = readFile("src/core/playlist_store.cpp");
  const auto audio = readFile("src/audioI2S/Audio.cpp");
  const auto audioHeader = readFile("src/audioI2S/AudioEx.h");

  const std::initializer_list<const char*> retired = {
      "PM_SDCARD", "PM_WEB", "MAX_PLAY_MODE", "changeMode(",
      "lastSdStation", "sdsnuffle", "play_mode", "sdResumePos",
      "initSDPlaylist", "SDPLFS", "REAL_PLAYL", "REAL_INDEX",
      "PLAYLIST_SD_PATH", "INDEX_SD_PATH", "SDREADY", "WAITFORSD",
      "SDFILEINDEX", "PR_CHECKSD"};
  for (const auto& source : {configHeader, configSource, playerHeader,
                             playerSource, networkHeader, networkSource,
                             common, display, timekeeper, mainSource,
                             widgets, server, serverHeader}) {
    hasNone(source, retired);
  }
  hasNone(configHeader, {"getMode("});
  hasNone(configSource, {"getMode("});

  hasAll(configHeader, {"PLAYLIST_PATH", "INDEX_PATH", "reservedSdStation",
                        "reservedSdFlags", "reservedPlayMode"});
  hasAll(configSource, {"SPIFFS.open(PLAYLIST_PATH", "SPIFFS.open(INDEX_PATH",
                        "PlaylistGuard"});
  hasAll(widgets, {"SPIFFS.open(PLAYLIST_PATH", "SPIFFS.open(INDEX_PATH",
                   "substring(firstTab + 1, secondTab)"});
  hasAll(playlistStore, {"PlaylistStore::", "PLAYLIST_PATH", "INDEX_PATH"});
  hasAll(server, {"/api/stations/import", "/api/stations/export"});

  // The generic local-file backend is deliberately retained for 2G.2C.
  hasAll(audio, {"Audio::connecttoFS", "Audio::connecttoSD", "AUDIO_LOCALFILE"});
  hasAll(audioHeader, {"connecttoFS", "connecttoSD", "AUDIO_LOCALFILE",
                       "<SD.h>", "<SD_MMC.h>"});
}
