#include "../src/core/player_pending_refresh.h"
#include "../src/core/player_station_navigation.h"
#include "../src/core/source_manager_state.h"

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

enum class Command { Noise, Play, Stop };

struct QueuedCommand {
  Command type;
  uint16_t station;
};

struct ReliabilityModel {
  explicit ReliabilityModel(std::size_t queueCapacity)
      : capacity(queueCapacity), playlist(5, 0) {}

  bool enqueue(Command type, uint16_t station = 0) {
    if (queue.size() == capacity) return false;
    queue.push_back({type, station});
    if (type == Command::Play) sources.recordRadioCommand(true, station);
    if (type == Command::Stop) sources.recordRadioCommand(false);
    return true;
  }

  void commit(uint16_t newCurrent, int identity,
              bool resetOvolIfEmpty = false) {
    current = newCurrent;
    if (newCurrent != 0) playlist[newCurrent] = identity;
    pending.publish(resetOvolIfEmpty);
  }

  void serviceOnce() {
    if (!queue.empty()) {
      const QueuedCommand command = queue.front();
      queue.pop_front();
      if (command.type == Command::Play) {
        current = command.station;
        runtime = playlist[current];
        playing = true;
        sources.radioPlayConsumed();
      } else if (command.type == Command::Stop) {
        playing = false;
        sources.radioStopConsumed();
      }
    }
    if (!queue.empty()) return;
    const PlayerPendingRefreshRequest request = pending.take();
    if (!request) return;
    if (current == 0) {
      sources.playlistSelectionCleared();
      runtime = 0;
      playing = false;
      ovolReset = request.resetOvolIfEmpty;
      return;
    }
    const bool identityChanged = runtime != playlist[current];
    runtime = playlist[current];
    if (identityChanged && playing && sources.radioResumeAllowed())
      ++reconnects;
  }

  void drain() {
    while (!queue.empty() || pending.pending()) serviceOnce();
  }

  const std::size_t capacity;
  std::deque<QueuedCommand> queue;
  std::vector<int> playlist;
  PlayerPendingRefresh pending;
  SourceManagerState sources;
  uint16_t current = 1;
  int runtime = 10;
  bool playing = true;
  bool ovolReset = false;
  unsigned reconnects = 0;
};

}  // namespace

int main() {
  using voxone::player_navigation::next;
  using voxone::player_navigation::previous;

  assert(next(0, 0) == 0 && previous(0, 0) == 0);
  assert(next(0, 5) == 1 && previous(0, 5) == 5);
  assert(next(5, 5) == 1 && previous(1, 5) == 5);
  assert(next(3, 5) == 4 && previous(3, 5) == 2);
  assert(next(1, 1) == 1 && previous(1, 1) == 1);

  // Directions are resolved by the FIFO consumer, so consecutive commands
  // advance from the result of the preceding command rather than enqueueing
  // duplicate absolute station numbers from the producer's stale snapshot.
  std::deque<bool> directions{true, true, false, true};
  uint16_t selected = 4;
  while (!directions.empty()) {
    const bool forward = directions.front();
    directions.pop_front();
    selected = forward ? next(selected, 5) : previous(selected, 5);
  }
  assert(selected == 1);  // 4 -> 5 -> 1 -> 5 -> 1.

  // Historical bounded-FIFO baseline for playlist reconciliation. This model
  // intentionally has no priority STOP: production STOP/admission/consumer are
  // exercised by player_queue_execution_native.py (including this full case).
  ReliabilityModel full(5);
  full.sources.recordRadioCommand(true, 1);
  full.sources.radioPlayConsumed();
  for (std::size_t i = 0; i < full.capacity; ++i)
    assert(full.enqueue(Command::Noise));
  assert(!full.enqueue(Command::Stop));
  assert(full.sources.radioPlayIntent());
  full.commit(1, 11);
  assert(full.pending.pending() && full.queue.size() == full.capacity);

  // A stopped consumer keeps the request. On resume, all older FIFO commands
  // run first and the latest committed station is reconciled exactly once.
  full.commit(1, 12);
  full.commit(1, 13);
  assert(full.runtime == 10 && full.reconnects == 0);
  full.drain();
  assert(full.runtime == 13 && full.reconnects == 1 &&
         !full.pending.pending());

  // A later accepted PLAY wins before an older reconcile and selects its own
  // current station; the reconcile then observes that final persisted choice.
  ReliabilityModel refreshThenPlay(5);
  refreshThenPlay.playlist[2] = 22;
  refreshThenPlay.commit(1, 11);
  assert(refreshThenPlay.enqueue(Command::Play, 2));
  refreshThenPlay.drain();
  assert(refreshThenPlay.playing && refreshThenPlay.current == 2 &&
         refreshThenPlay.runtime == 22 &&
         refreshThenPlay.sources.radioPlayIntent());

  // A later manual STOP is consumed before reconciliation and prevents an
  // automatic reconnect even though station metadata still refreshes.
  ReliabilityModel refreshThenStop(5);
  refreshThenStop.sources.recordRadioCommand(true, 1);
  refreshThenStop.sources.radioPlayConsumed();
  refreshThenStop.commit(1, 14);
  assert(refreshThenStop.enqueue(Command::Stop));
  refreshThenStop.drain();
  assert(!refreshThenStop.playing && refreshThenStop.runtime == 14 &&
         !refreshThenStop.sources.radioPlayIntent() &&
         refreshThenStop.reconnects == 0);

  // PLAY accepted before the commit also runs first. It loads the already
  // committed station, so reconciliation does not repeat that operation.
  ReliabilityModel playThenRefresh(5);
  playThenRefresh.playlist[2] = 20;
  assert(playThenRefresh.enqueue(Command::Play, 2));
  playThenRefresh.commit(2, 21);
  playThenRefresh.drain();
  assert(playThenRefresh.current == 2 && playThenRefresh.runtime == 21 &&
         playThenRefresh.reconnects == 0);

  // Empty playlist is a required physical stop. Reset-OVOL survives
  // coalescing, while a newer accepted PLAY intent is never overwritten.
  ReliabilityModel empty(5);
  empty.sources.recordRadioCommand(true, 1);
  empty.sources.radioPlayConsumed();
  empty.commit(0, 0, true);
  empty.pending.publishRefresh();
  empty.drain();
  assert(!empty.playing && empty.runtime == 0 && empty.ovolReset &&
         !empty.sources.radioPlayIntent());

  SourceManagerState ordering;
  ordering.recordRadioCommand(true, 1);
  ordering.radioPlayConsumed();
  ordering.recordRadioCommand(true, 2);
  ordering.playlistSelectionCleared();
  assert(ordering.radioPlayIntent() && ordering.radioStationForResume(1) == 2);
  ordering.recordRadioCommand(false);
  ordering.playlistSelectionCleared();
  assert(!ordering.radioPlayIntent());

  // Manual source priority suppresses reconnect; reconciliation only refreshes
  // the station data and never forces RADIO active.
  ReliabilityModel bluetooth(5);
  bluetooth.sources.recordRadioCommand(true, 1);
  bluetooth.sources.radioPlayConsumed();
  BtLinkState bt{};
  bt.runtimeAvailable = true;
  bluetooth.sources.cycle(bt);
  bluetooth.commit(1, 15);
  bluetooth.drain();
  assert(bluetooth.sources.active() == ActiveSource::Bluetooth &&
         bluetooth.runtime == 15 && bluetooth.reconnects == 0);

  const std::string server = readFile("src/core/netserver.cpp");
  const std::string player = readFile("src/core/player.cpp");
  const std::string playerHeader = readFile("src/core/player.h");
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
    const std::size_t publish = handler->find("player.requestPlaylistSync");
    const std::size_t release = handler->find("guard.release();");
    assert(publish != std::string::npos && release != std::string::npos &&
           publish < release);
    assert(handler->find("player.sendCommand({PR_PLAY") == std::string::npos);
    assert(handler->find("player.sendCommand({PR_STOP") == std::string::npos);
  }

  const std::string sendCommand = section(
      player, "bool Player::sendCommand(playerRequestParams_t request)",
      "void Player::resetQueue()");
  assert(sendCommand.find("portMAX_DELAY") == std::string::npos);
  assert(sendCommand.find("xQueueSend(playerQueue, &request, 0)") !=
         std::string::npos);
  assert(playerHeader.find("requestPlaylistSync") != std::string::npos);
  assert(player.find("if (uxQueueMessagesWaiting(playerQueue) == 0)\n"
                     "    _servicePendingStationRefresh();") !=
         std::string::npos);
  assert(player.find("sourceManagerRadioResumeAllowed()") != std::string::npos);
  assert(player.find("_applyStop(PR_STOP_KEEP_STATION, false);") !=
         std::string::npos);

  const std::string previousCommand = section(
      player, "bool Player::prev()", "bool Player::next()");
  const std::string nextCommand = section(
      player, "bool Player::next()", "bool Player::toggle()");
  assert(previousCommand.find("sendCommand({PR_PREV, 0})") != std::string::npos);
  assert(nextCommand.find("sendCommand({PR_NEXT, 0})") != std::string::npos);
  assert(previousCommand.find("config.lastStation(") == std::string::npos);
  assert(nextCommand.find("config.lastStation(") == std::string::npos);
  assert(sendCommand.find("case PR_PREV:") != std::string::npos);
  assert(sendCommand.find("case PR_NEXT:") != std::string::npos);

  const std::string consumer = section(
      player, "bool Player::processPlayerCommands()", "void Player::serviceSourceMedia()");
  assert(consumer.find("case PR_PREV:") != std::string::npos);
  assert(consumer.find("case PR_NEXT:") != std::string::npos);
  assert(consumer.find("config.setLastStation(target);") != std::string::npos);
}
