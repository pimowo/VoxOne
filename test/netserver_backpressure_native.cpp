#include "../src/core/netserver_backpressure.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

using voxone::netserver_backpressure::ClientIdSet;
using voxone::netserver_backpressure::ClientRequestSet;
using voxone::netserver_backpressure::InsertResult;

enum class Request : uint8_t {
  Other = 1,
  Station = 2,
  StationName = 3,
  Item = 4,
  Title = 5,
  Volume = 6,
  Bitrate = 8,
  Mode = 9,
  Equalizer = 10,
  Balance = 11,
  GetIndex = 14,
  GetSystem = 16,
  WebStatus = 28
};

enum class EnqueueResult { Queued, Coalesced, Deferred, Rejected };

struct Event {
  Request type;
  uint32_t clientId;
};

class NetserverModel {
 public:
  static constexpr size_t kCapacity = 20;

  void connect(uint32_t clientId) {
    assert(connected_.insert(clientId) != InsertResult::Full);
  }

  void disconnect(uint32_t clientId) {
    connected_.erase(clientId);
    snapshots_.erase(clientId);
    requests_.eraseClient(clientId);
    rejected_.erase(clientId);
  }

  EnqueueResult request(Request type, uint32_t clientId) {
    if (type == Request::Volume && clientId == 0) {
      volumePending_ = true;
      return EnqueueResult::Deferred;
    }

    bool snapshotReserved = false;
    bool requestReserved = false;
    if (type == Request::GetIndex) {
      const InsertResult result = snapshots_.insert(clientId);
      if (result == InsertResult::Existing) return EnqueueResult::Coalesced;
      if (result == InsertResult::Full) return reject(type, clientId);
      snapshotReserved = true;
    } else if (clientId != 0) {
      const InsertResult result = requests_.insert(
          clientId, static_cast<uint8_t>(type));
      if (result == InsertResult::Existing) return EnqueueResult::Coalesced;
      if (result == InsertResult::Full) return reject(type, clientId);
      requestReserved = true;
    }

    if (queue_.size() == kCapacity) {
      if (snapshotReserved) snapshots_.erase(clientId);
      if (requestReserved)
        requests_.erase(clientId, static_cast<uint8_t>(type));
      return reject(type, clientId);
    }
    queue_.push_back({type, clientId});
    return EnqueueResult::Queued;
  }

  void service(bool refillBeforeStationChildren = false) {
    if (snapshotActive_ && !snapshots_.contains(snapshotClientId_)) {
      snapshotActive_ = false;
      snapshotIndex_ = 0;
    }

    if (snapshotActive_) {
      delivered_.push_back(
          {kSnapshotRequests[snapshotIndex_], snapshotClientId_});
      if (++snapshotIndex_ == kSnapshotCount) {
        snapshots_.erase(snapshotClientId_);
        snapshotActive_ = false;
        snapshotIndex_ = 0;
      }
      serviceDeferred();
      return;
    }

    if (!queue_.empty()) {
      const Event event = queue_.front();
      queue_.pop_front();
      if (event.clientId != 0 && event.type != Request::GetIndex)
        requests_.erase(event.clientId, static_cast<uint8_t>(event.type));

      if (event.clientId != 0 && !connected_.contains(event.clientId)) {
        if (event.type == Request::GetIndex)
          snapshots_.erase(event.clientId);
      } else if (event.type == Request::GetIndex) {
        if (snapshots_.contains(event.clientId)) {
          snapshotActive_ = true;
          snapshotClientId_ = event.clientId;
          snapshotIndex_ = 0;
        }
      } else if (event.type == Request::Station) {
        if (refillBeforeStationChildren && queue_.size() < kCapacity)
          queue_.push_back({Request::Other, 0});
        request(Request::StationName, event.clientId);
        request(Request::Item, event.clientId);
      } else {
        delivered_.push_back(event);
        if (event.clientId == 0 &&
            (event.type == Request::Item || event.type == Request::Title ||
             event.type == Request::Mode))
          ++mqttStatusPublishes_;
      }
    }
    serviceDeferred();
  }

  void serviceVolume() {
    if (!volumePending_) return;
    volumePending_ = false;
    ++mqttVolumePublishes_;
  }

  size_t queueSize() const { return queue_.size(); }
  size_t pendingFor(uint32_t clientId) const {
    return requests_.sizeForClient(clientId);
  }
  bool snapshotPending(uint32_t clientId) const {
    return snapshots_.contains(clientId);
  }
  bool rejected(uint32_t clientId) const {
    return rejected_.contains(clientId);
  }
  bool deferred(Request type) const {
    return (deferred_ & requestBit(type)) != 0;
  }
  size_t deliveredFor(uint32_t clientId) const {
    size_t count = 0;
    for (const Event& event : delivered_) {
      if (event.clientId == clientId) ++count;
    }
    return count;
  }
  size_t mqttStatusPublishes() const { return mqttStatusPublishes_; }
  size_t mqttVolumePublishes() const { return mqttVolumePublishes_; }

 private:
  static uint32_t requestBit(Request type) {
    return 1UL << static_cast<uint8_t>(type);
  }

  EnqueueResult reject(Request type, uint32_t clientId) {
    if (clientId == 0) {
      deferred_ |= requestBit(type);
      return EnqueueResult::Deferred;
    }
    snapshots_.erase(clientId);
    requests_.eraseClient(clientId);
    rejected_.insert(clientId);
    return EnqueueResult::Rejected;
  }

  void serviceDeferred() {
    for (uint8_t requestId = 0; deferred_ && requestId < 32; ++requestId) {
      const uint32_t bit = 1UL << requestId;
      if ((deferred_ & bit) == 0) continue;
      deferred_ &= ~bit;
      request(static_cast<Request>(requestId), 0);
      return;
    }
  }

  static constexpr Request kSnapshotRequests[] = {
      Request::Title, Request::Volume, Request::Equalizer, Request::Balance,
      Request::Bitrate, Request::Mode, Request::StationName, Request::Item};
  static constexpr size_t kSnapshotCount =
      sizeof(kSnapshotRequests) / sizeof(kSnapshotRequests[0]);

  std::deque<Event> queue_;
  std::vector<Event> delivered_;
  ClientIdSet<8> connected_;
  ClientIdSet<8> snapshots_;
  ClientIdSet<8> rejected_;
  ClientRequestSet<kCapacity> requests_;
  uint32_t deferred_ = 0;
  bool volumePending_ = false;
  bool snapshotActive_ = false;
  uint32_t snapshotClientId_ = 0;
  size_t snapshotIndex_ = 0;
  size_t mqttStatusPublishes_ = 0;
  size_t mqttVolumePublishes_ = 0;
};

constexpr Request NetserverModel::kSnapshotRequests[];

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string functionBody(const std::string& source,
                         const std::string& signature) {
  const size_t begin = source.find(signature);
  assert(begin != std::string::npos);
  const size_t open = source.find('{', begin);
  assert(open != std::string::npos);
  size_t depth = 0;
  for (size_t pos = open; pos < source.size(); ++pos) {
    if (source[pos] == '{') {
      ++depth;
    } else if (source[pos] == '}' && --depth == 0) {
      return source.substr(open, pos - open + 1);
    }
  }
  assert(false);
  return {};
}

void fillQueue(NetserverModel& server) {
  for (size_t index = 0; index < NetserverModel::kCapacity; ++index)
    assert(server.request(Request::Other, 0) == EnqueueResult::Queued);
}

void testFullQueueNeverWaitsInNetworkCallback() {
  NetserverModel server;
  server.connect(1);
  fillQueue(server);
  assert(server.request(Request::GetSystem, 1) == EnqueueResult::Rejected);
  assert(server.rejected(1));
  assert(server.queueSize() == NetserverModel::kCapacity);
}

void testConsumerCannotBlockOnItsOwnFullQueue() {
  NetserverModel server;
  assert(server.request(Request::Station, 0) == EnqueueResult::Queued);
  for (size_t index = 1; index < NetserverModel::kCapacity; ++index)
    assert(server.request(Request::Other, 0) == EnqueueResult::Queued);
  server.service(true);
  assert(server.queueSize() == NetserverModel::kCapacity);
  assert(server.deferred(Request::StationName));
  assert(server.deferred(Request::Item));
}

void testRepeatedGetIndexIsCoalescedAndReusable() {
  NetserverModel server;
  server.connect(7);
  assert(server.request(Request::GetIndex, 7) == EnqueueResult::Queued);
  for (size_t count = 0; count < 100; ++count)
    assert(server.request(Request::GetIndex, 7) == EnqueueResult::Coalesced);
  assert(server.queueSize() == 1 && server.snapshotPending(7));
  for (size_t step = 0; step < 9; ++step) server.service();
  assert(server.deliveredFor(7) == 8);
  assert(!server.snapshotPending(7));
  assert(server.request(Request::GetIndex, 7) == EnqueueResult::Queued);
}

void testFloodingClientDoesNotFillQueueForSecondClient() {
  NetserverModel server;
  server.connect(11);
  server.connect(12);
  assert(server.request(Request::GetIndex, 11) == EnqueueResult::Queued);
  assert(server.request(Request::GetSystem, 11) == EnqueueResult::Queued);
  assert(server.request(Request::WebStatus, 11) == EnqueueResult::Queued);
  for (size_t count = 0; count < 100; ++count) {
    assert(server.request(Request::GetIndex, 11) == EnqueueResult::Coalesced);
    assert(server.request(Request::GetSystem, 11) == EnqueueResult::Coalesced);
    assert(server.request(Request::WebStatus, 11) == EnqueueResult::Coalesced);
  }
  assert(server.pendingFor(11) == 2);
  assert(server.queueSize() == 3);
  assert(server.request(Request::GetIndex, 12) == EnqueueResult::Queued);
  for (size_t step = 0; step < 24; ++step) server.service();
  assert(server.deliveredFor(12) == 8);
  assert(!server.rejected(12));
}

void testDisconnectClearsPendingSnapshot() {
  NetserverModel server;
  server.connect(21);
  assert(server.request(Request::GetIndex, 21) == EnqueueResult::Queued);
  server.service();
  server.service();
  assert(server.snapshotPending(21));
  server.disconnect(21);
  server.service();
  assert(!server.snapshotPending(21));

  server.connect(22);
  assert(server.request(Request::GetIndex, 22) == EnqueueResult::Queued);
  for (size_t step = 0; step < 9; ++step) server.service();
  assert(server.deliveredFor(22) == 8);
  assert(!server.snapshotPending(22));
}

void testPartialSnapshotIsExplicitlyRejectedOnOverload() {
  NetserverModel server;
  server.connect(31);
  assert(server.request(Request::GetIndex, 31) == EnqueueResult::Queued);
  server.service();
  server.service();
  assert(server.deliveredFor(31) == 1 && server.snapshotPending(31));
  fillQueue(server);
  assert(server.request(Request::GetSystem, 31) == EnqueueResult::Rejected);
  assert(server.rejected(31));
  assert(!server.snapshotPending(31));
  server.service();
  assert(server.deliveredFor(31) == 1);
}

void testMqttStateIsRetriedAfterOverload() {
  NetserverModel server;
  fillQueue(server);
  assert(server.request(Request::Mode, 0) == EnqueueResult::Deferred);
  assert(server.request(Request::Volume, 0) == EnqueueResult::Deferred);
  assert(server.deferred(Request::Mode));
  for (size_t step = 0; step < NetserverModel::kCapacity + 2; ++step)
    server.service();
  server.serviceVolume();
  assert(server.mqttStatusPublishes() == 1);
  assert(server.mqttVolumePublishes() == 1);
}

void testProductionBackpressureContract() {
  const std::string source = readFile("src/core/netserver.cpp");
  const std::string asyncTcp = readFile("src/AsyncWebServer/AsyncTCP.cpp");
  assert(source.find("#define NSQ_SEND_DELAY       0") != std::string::npos);
  assert(source.find("xQueueCreate(kNetserverQueueLength") !=
         std::string::npos);

  const std::string enqueue = functionBody(
      source, "void NetServer::requestOnChange(");
  assert(enqueue.find("xQueueSend(nsQueue, &nsrequest, NSQ_SEND_DELAY)") !=
         std::string::npos);
  assert(enqueue.find("serialCli.printf") == std::string::npos);
  assert(enqueue.find("reserveSnapshot(clientId)") != std::string::npos);
  assert(enqueue.find("reserveClientRequest(clientId, request)") !=
         std::string::npos);
  assert(enqueue.find("deferBroadcastRequest(request)") != std::string::npos);
  assert(enqueue.find("scheduleOverloadRejection(clientId)") !=
         std::string::npos);

  const std::string process = functionBody(
      source, "void NetServer::processQueue(");
  const size_t getIndex = process.find("case GETINDEX:");
  const size_t getSystem = process.find("case GETSYSTEM:", getIndex);
  const std::string getIndexCase = process.substr(getIndex, getSystem - getIndex);
  assert(getIndexCase.find("requestOnChange(") == std::string::npos);
  assert(getIndexCase.find("_snapshotActive = true") != std::string::npos);
  assert(process.find("kIndexSnapshotRequests[_snapshotIndex]") !=
         std::string::npos);

  const std::string loop = functionBody(source, "void NetServer::loop(");
  assert(loop.find("processDeferredRequest();") != std::string::npos);
  assert(loop.find("processOverloadRejection();") != std::string::npos);
  assert(source.find("netserver.cancelClientRequests(client->id())") !=
         std::string::npos);

  const size_t clear = asyncTcp.find("static int8_t _tcp_clear_events");
  const size_t connected = asyncTcp.find("static int8_t _tcp_connected", clear);
  const std::string clearBody = asyncTcp.substr(clear, connected - clear);
  assert(clearBody.find(
             "xTaskGetCurrentTaskHandle() == _async_service_task_handle") !=
         std::string::npos);
  assert(clearBody.find("_remove_events_with_arg(arg)") != std::string::npos);
}

}  // namespace

int main() {
  testFullQueueNeverWaitsInNetworkCallback();
  testConsumerCannotBlockOnItsOwnFullQueue();
  testRepeatedGetIndexIsCoalescedAndReusable();
  testFloodingClientDoesNotFillQueueForSecondClient();
  testDisconnectClearsPendingSnapshot();
  testPartialSnapshotIsExplicitlyRejectedOnOverload();
  testMqttStateIsRetriedAfterOverload();
  testProductionBackpressureContract();
  std::puts("PASS netserver_backpressure_native");
  return 0;
}
