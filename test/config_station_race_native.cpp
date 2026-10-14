#include "../src/core/playlist_mapping.h"
#include "../src/core/player_pending_refresh.h"

#include <cassert>
#include <deque>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

enum class ModelCommand { Play, Stop };

struct QueuedModelCommand {
  ModelCommand type;
  uint16_t station;
};

struct PlayerModel {
  explicit PlayerModel(std::size_t capacity)
      : capacity(capacity), playlist(4, 0) {}

  bool enqueue(ModelCommand type, uint16_t station = 0) {
    if (queue.size() == capacity) return false;
    queue.push_back({type, station});
    return true;
  }

  void service() {
    if (!queue.empty()) {
      const QueuedModelCommand command = queue.front();
      queue.pop_front();
      if (command.type == ModelCommand::Play) {
        current = command.station;
        runtime = playlist[current];
        playing = true;
      } else {
        playing = false;
      }
    }
    if (queue.empty() && pending.take()) runtime = playlist[current];
  }

  const std::size_t capacity;
  std::deque<QueuedModelCommand> queue;
  std::vector<int> playlist;
  PlayerPendingRefresh pending;
  uint16_t current = 1;
  int runtime = 0;
  bool playing = false;
};

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

  // Publishing never waits for the Player queue. A stopped consumer retains
  // the latest refresh and coalesces all earlier publications.
  PlayerModel full(5);
  for (std::size_t i = 0; i < full.capacity; ++i)
    assert(full.enqueue(ModelCommand::Stop));
  full.pending.publish();
  assert(full.queue.size() == full.capacity);
  assert(full.pending.pending());
  assert(!full.enqueue(ModelCommand::Play, 2));
  assert(!full.enqueue(ModelCommand::Stop));
  assert(full.queue.size() == full.capacity);
  full.pending.publish();
  full.pending.publish();
  assert(full.pending.pending());
  while (!full.queue.empty()) full.service();
  assert(!full.pending.pending());

  // A publication concurrent with consumption remains pending for the next
  // Player iteration rather than being erased by the earlier take().
  full.pending.publish();
  assert(full.pending.take());
  full.pending.publish();
  assert(full.pending.take());
  assert(!full.pending.take());

  // The latest committed transaction owns reset-OVOL semantics, while a
  // legacy refresh cannot erase an already published import requirement.
  full.pending.publish(true);
  full.pending.publish(false);
  PlayerPendingRefreshRequest resetRequest = full.pending.take();
  assert(resetRequest && !resetRequest.resetOvolIfEmpty);
  full.pending.publish(true);
  full.pending.publishRefresh();
  resetRequest = full.pending.take();
  assert(resetRequest && resetRequest.resetOvolIfEmpty);

  PlayerModel refreshThenPlay(5);
  refreshThenPlay.playlist[1] = 11;
  refreshThenPlay.playlist[2] = 22;
  refreshThenPlay.runtime = 10;
  refreshThenPlay.pending.publish();
  assert(refreshThenPlay.enqueue(ModelCommand::Play, 2));
  refreshThenPlay.service();
  assert(refreshThenPlay.playing && refreshThenPlay.current == 2 &&
         refreshThenPlay.runtime == 22);

  PlayerModel refreshThenStop(5);
  refreshThenStop.playlist[1] = 12;
  refreshThenStop.runtime = 10;
  refreshThenStop.playing = true;
  refreshThenStop.pending.publish();
  assert(refreshThenStop.enqueue(ModelCommand::Stop));
  refreshThenStop.service();
  assert(!refreshThenStop.playing && refreshThenStop.runtime == 12);

  PlayerModel playThenRefresh(5);
  playThenRefresh.playlist[2] = 23;
  assert(playThenRefresh.enqueue(ModelCommand::Play, 2));
  playThenRefresh.pending.publish();
  playThenRefresh.service();
  assert(playThenRefresh.playing && playThenRefresh.current == 2 &&
         playThenRefresh.runtime == 23);

  PlayerModel resumed(5);
  resumed.playlist[1] = 31;
  resumed.runtime = 30;
  resumed.pending.publish();
  resumed.playlist[1] = 32;
  assert(resumed.runtime == 30 && resumed.pending.pending());
  resumed.service();
  assert(resumed.runtime == 32 && !resumed.pending.pending());

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
    const std::size_t publish = handler->find("player.requestPlaylistSync");
    const std::size_t release = handler->find("guard.release();");
    assert(publish != std::string::npos && release != std::string::npos &&
           publish < release);
    assert(handler->find("player.sendCommand({PR_PLAY") == std::string::npos);
    assert(handler->find("player.sendCommand({PR_STOP") == std::string::npos);
  }

  assert(playerHeader.find("PR_STATION_REFRESH") != std::string::npos);
  assert(playerHeader.find("PR_STOP_CLEAR_STATION") != std::string::npos);
  assert(player.find("case PR_STATION_REFRESH:") != std::string::npos);
  const std::string sendCommand = section(
      player, "void Player::sendCommand(playerRequestParams_t request)",
      "void Player::resetQueue()");
  const std::size_t publish = sendCommand.find("_pendingStationRefresh.publish");
  const std::size_t publishReturn = sendCommand.find("return;", publish);
  const std::size_t queueSend = sendCommand.find("xQueueSend", publishReturn);
  assert(publish != std::string::npos && publishReturn != std::string::npos &&
         queueSend != std::string::npos && publish < publishReturn &&
         publishReturn < queueSend);
  assert(sendCommand.find("portMAX_DELAY") == std::string::npos);
  assert(sendCommand.find("xQueueSend(playerQueue, &request, PLQ_SEND_DELAY)") !=
         std::string::npos);
  assert(sendCommand.find("sourceManagerRadioCommandQueued", queueSend) !=
         std::string::npos);
  assert(player.find("if (uxQueueMessagesWaiting(playerQueue) == 0)\n"
                     "    _servicePendingStationRefresh();\n  char url") !=
         std::string::npos);
  assert(player.find("_refreshStation(static_cast<uint16_t>(requestP.payload))") !=
         std::string::npos);
  assert(player.find("PlaylistGuard guard(0);\n  if (!guard) return;") !=
         std::string::npos);
  assert(player.find("if (!config.loadStation(stationId)) return false;") !=
         std::string::npos);
  assert(player.find("const StationRefreshChanges changes =") !=
         std::string::npos);
  assert(player.find("case PR_STOP:\n        _applyStop(requestP.payload, true);") !=
         std::string::npos);
  assert(store.find("PlaylistGuard::~PlaylistGuard() { release(); }") !=
         std::string::npos);

  // The earlier HIL instrumentation remains intact after ownership transfer.
  assert(mutationHandler.find("##[PLAYLIST-DIAG]#") != std::string::npos);
  assert(mutationHandler.find("PR_STATION_REFRESH") != std::string::npos);
  assert(mutationHandler.find("buf=%lu->%lu") != std::string::npos);
}
