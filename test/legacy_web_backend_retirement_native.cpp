#include "../src/core/config_format.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

bool contains(const std::string& source, const char* token) {
  return source.find(token) != std::string::npos;
}
}  // namespace

int main() {
  static_assert(voxone::config_format::kConfigV7SerializedSize == 255,
                "Config v7 layout changed");

  const auto server = readFile("src/core/netserver.cpp");
  const auto serverHeader = readFile("src/core/netserver.h");
  const auto commands = readFile("src/core/commandhandler.cpp");
  const auto mqtt = readFile("src/core/mqtt.cpp");
  const auto config = readFile("src/core/config.cpp");
  const auto configHeader = readFile("src/core/config.h");
  const auto currentWeb = readFile("web-src/voxone.js");

  // Legacy routes and the HTTP command bridge are gone.
  for (const char* route : {"/legacy.html", "/settings.html", "/upload",
                            "/favicon.ico"})
    assert(!contains(server, route));
  assert(!contains(server, "strcmp(request->url().c_str(), SSIDS_PATH)"));
  assert(!contains(server, "strcmp(request->url().c_str(), INDEX_PATH)"));
  assert(!contains(server, "TMP_PATH"));
  assert(!contains(server, "cmd.exec(p->name()"));
  assert(!contains(server, "request->hasArg(\"trebble\")"));
  assert(!contains(server, "request->hasArg(\"sleep\")"));

  // The retired legacy WebSocket/import surface has no runtime definitions.
  for (const char* token : {"PLAYLIST=", "GETACTIVE", "GETTIMEZONE", "DSPON",
                            "STARTUP=", "submitplaylist", "IMPL", "IMWIFI",
                            "importPlaylist", "getPlaylist", "importRequest"}) {
    assert(!contains(server, token));
    assert(!contains(serverHeader, token));
  }
  assert(!contains(commands, "\"getactive\""));
  assert(!contains(commands, "\"gettimezone\""));
  assert(!contains(commands, "\"submitplaylist\""));

  // Internal Wi-Fi and station index storage remain, without public routes.
  assert(contains(configHeader, "SSIDS_PATH"));
  assert(contains(configHeader, "INDEX_PATH"));
  assert(!contains(configHeader, "TMP_PATH"));
  assert(contains(config, "SPIFFS.open(SSIDS_PATH"));
  assert(contains(server, "{SSIDS_PATH, \"wifi\"}"));
  assert(contains(server, "{INDEX_PATH, \"stidx\"}"));

  // Current WWW, WebSocket, station API, MQTT, time and recovery remain.
  assert(contains(server, "webserver.on(\"/\", HTTP_ANY, handleIndex)"));
  assert(contains(server, "serveStatic(\"/voxone.html\""));
  assert(contains(server, "AsyncWebSocket websocket(\"/ws\")"));
  assert(contains(server, "/api/stations/import"));
  assert(contains(server, "/api/stations/export"));
  assert(contains(server, "/api/stations/add"));
  assert(contains(server, "/api/mqtt"));
  assert(contains(server, "/api/time"));
  assert(contains(server, "request->url()==\"/update\""));
  assert(contains(server, "request->url()==\"/emergency\""));
  assert(contains(server, "strcmp(request->url().c_str(), PLAYLIST_PATH)"));
  assert(contains(configHeader, "PLAYLIST_PATH"));
  assert(contains(currentWeb, "new WebSocket"));

  // CommandHandler is shared by current WebSocket and MQTT, not retired.
  assert(contains(server, "cmd.exec(_wscmd, _wsval, clientId)"));
  assert(contains(mqtt, "cmd.exec(buf, \"\")"));
  for (const char* command : {"\"start\"", "\"stop\"", "\"prev\"",
                              "\"next\"", "\"volume\""})
    assert(contains(commands, command));
}
