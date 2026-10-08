#include "../src/core/config_format.h"

#include <cassert>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>

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
  static_assert(voxone::config_format::kConfigV7SerializedSize == 255,
                "Config v7 layout changed");

  assert(!std::ifstream("src/core/sdmanager.cpp").good());
  assert(!std::ifstream("src/core/sdmanager.h").good());

  const auto audio = readFile("src/audioI2S/Audio.cpp");
  const auto audioHeader = readFile("src/audioI2S/AudioEx.h");
  const auto handlers = readFile("src/core/audiohandlers.h");
  const auto player = readFile("src/core/player.cpp");
  const auto playerHeader = readFile("src/core/player.h");
  const auto config = readFile("src/core/config.cpp");
  const auto configHeader = readFile("src/core/config.h");
  const auto playlistStore = readFile("src/core/playlist_store.cpp");

  const std::initializer_list<const char*> retired = {
      "connecttoFS", "connecttoSD", "AUDIO_LOCALFILE", "m_resumeFilePos",
      "audio_beginSDread", "audio_progress", "processLocalFile", "SDManager",
      "sdman", "SDFS", "<SD.h>", "<SD_MMC.h>", "setFilePos",
      "getFilePos", "getFileSize", "getAudioFileDuration", "audioFileSeek"};
  for (const auto& source : {audio, audioHeader, handlers, player, playerHeader}) {
    hasNone(source, retired);
  }
  hasNone(player, {"initHeaders"});
  hasNone(playerHeader, {"initHeaders", "sd_min", "sd_max"});

  hasAll(config, {"SPIFFS.open(PLAYLIST_PATH", "File playlist", "File index"});
  hasAll(configHeader, {"#include <SPIFFS.h>", "PLAYLIST_PATH", "INDEX_PATH"});
  hasAll(playlistStore, {"PlaylistStore::", "SPIFFS.open", "File file"});

  // ID3 parsing is shared with HTTP web files and remains intentionally active.
  hasAll(audio, {"Audio::read_ID3_Header", "Audio::processWebFile"});
  hasAll(audioHeader, {"audio_id3data", "ST_WEBFILE"});
}
