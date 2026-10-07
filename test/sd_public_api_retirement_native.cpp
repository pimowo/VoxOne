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

  const auto commands = readFile("src/core/commandhandler.cpp");
  const auto serialCli = readFile("src/core/serialcli.cpp");
  const auto server = readFile("src/core/netserver.cpp");
  const auto serverHeader = readFile("src/core/netserver.h");
  const auto currentWeb = readFile("web-src/voxone.js");
  const auto config = readFile("src/core/config.cpp");
  const auto configHeader = readFile("src/core/config.h");

  hasNone(commands, {"strEquals(command, \"mode\")",
                     "strEquals(command, \"newmode\")",
                     "strEquals(command, \"sdpos\")",
                     "strEquals(command, \"snuffle\")"});
  hasNone(serialCli, {"sscanf(str, \"mode %d\""});

  for (const auto& source : {server, serverHeader}) {
    hasNone(source, {"SDPOS", "SDLEN", "SDSNUFFLE", "SDINIT",
                     "GETPLAYERMODE", "CHANGEMODE", "\"sdpos\"",
                     "\"sdend\"", "\"sdtpos\"", "\"sdtend\"",
                     "\"sdmin\"", "\"sdmax\"", "\"snuffle\"",
                     "\"sdinit\"", "\"playermode\"", "modesd",
                     "modeweb"});
  }
  hasNone(server, {"PLAYLIST_SD_PATH", "INDEX_SD_PATH", "playlistsd"});
  hasNone(currentWeb, {"playermode", "modesd", "modeweb", "sdpos",
                       "snuffle"});
  for (const auto& source : {config, configHeader}) {
    hasNone(source, {"newConfigMode", "setSDpos", "setSnuffle"});
  }

  hasAll(server, {"/api/stations/import", "/api/stations/export",
                  "handleStationsMutation", "stationDirectory::kDirectorySearchRoute",
                  "request->url()==\"/update\"", "handleWebUpdateUpload",
                  "request->url()==\"/emergency\"", "handleIndex"});
  hasAll(configHeader, {"PLAYLIST_PATH", "INDEX_PATH"});
}
