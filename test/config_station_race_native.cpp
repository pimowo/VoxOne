#include "../src/core/playlist_mapping.h"

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

std::string section(const std::string& source, const char* begin,
                    const char* end) {
  const std::size_t first = source.rfind(begin);
  assert(first != std::string::npos);
  const std::size_t last = source.find(end, first + 1);
  assert(last != std::string::npos && last > first);
  return source.substr(first, last - first);
}

}  // namespace

int main() {
  assert(stationRuntimeAction(false, true, false) ==
         StationRuntimeAction::None);
  assert(stationRuntimeAction(true, true, false) ==
         StationRuntimeAction::Refresh);
  assert(stationRuntimeAction(true, true, true) ==
         StationRuntimeAction::Play);
  assert(stationRuntimeAction(true, false, false) ==
         StationRuntimeAction::Stop);

  StationRefreshChanges changes = stationRefreshChanges(0, 0, 0, 0);
  assert(!changes.volume && !changes.metadataMode);
  changes = stationRefreshChanges(-4, 0, 6, 0);
  assert(changes.volume && !changes.metadataMode);
  changes = stationRefreshChanges(3, 0, 3, 1);
  assert(!changes.volume && changes.metadataMode);
  changes = stationRefreshChanges(-2, 0, 8, 1);
  assert(changes.volume && changes.metadataMode);

  const std::string server = readFile("src/core/netserver.cpp");
  const std::string player = readFile("src/core/player.cpp");
  const std::string playerHeader = readFile("src/core/player.h");
  const std::string store = readFile("src/core/playlist_store.cpp");

  const std::string importHandler = section(
      server, "void handleStationsImport(AsyncWebServerRequest* request)",
      "static bool stationNumber");
  const std::string metadataHandler = section(
      server, "void handleStationMetadata(AsyncWebServerRequest* request)",
      "void handleStationsMutation");
  const std::string mutationHandler = section(
      server, "void handleStationsMutation(AsyncWebServerRequest *request)",
      "void NetServer::processQueue");

  for (const std::string* handler :
       {&importHandler, &metadataHandler, &mutationHandler}) {
    assert(handler->find("config.station") == std::string::npos);
    const std::size_t release = handler->find("guard.release();");
    const std::size_t command = handler->find("player.sendCommand", release);
    assert(release != std::string::npos && command != std::string::npos &&
           release < command);
  }

  assert(playerHeader.find("PR_STATION_REFRESH") != std::string::npos);
  assert(playerHeader.find("PR_STOP_CLEAR_STATION") != std::string::npos);
  assert(player.find("case PR_STATION_REFRESH:") != std::string::npos);
  assert(player.find("request.type == PR_STATION_REFRESH\n"
                     "      ? portMAX_DELAY : PLQ_SEND_DELAY") !=
         std::string::npos);
  assert(player.find("xQueueSend(playerQueue, &request, sendWait)") !=
         std::string::npos);
  assert(player.find("_refreshStation(static_cast<uint16_t>(requestP.payload))") !=
         std::string::npos);
  assert(player.find("if (!config.loadStation(stationId)) return;") !=
         std::string::npos);
  assert(player.find("const StationRefreshChanges changes =") !=
         std::string::npos);
  assert(player.find("case PR_STOP:\n        if (requestP.payload <") !=
         std::string::npos);
  assert(store.find("PlaylistGuard::~PlaylistGuard() { release(); }") !=
         std::string::npos);

  // The earlier HIL instrumentation remains intact after ownership transfer.
  assert(mutationHandler.find("##[PLAYLIST-DIAG]#") != std::string::npos);
  assert(mutationHandler.find("PR_STATION_REFRESH") != std::string::npos);
  assert(mutationHandler.find("buf=%lu->%lu") != std::string::npos);
}
