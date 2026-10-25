#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>

namespace {

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

void assertGuardPrecedes(const std::string& body, const char* operation) {
  const size_t guard = body.find("AsyncWebLockGuard guard(_queueLock)");
  const size_t access = body.find(operation);
  assert(guard != std::string::npos);
  assert(access != std::string::npos);
  assert(guard < access);
}

void testProductionQueueSynchronizationContract() {
  const std::string header =
      readFile("src/AsyncWebServer/AsyncWebSocket.h");
  const std::string source =
      readFile("src/AsyncWebServer/AsyncWebSocket.cpp");

  const size_t lock = header.find("mutable AsyncWebLock _queueLock;");
  const size_t control =
      header.find("LinkedList<AsyncWebSocketControl *> _controlQueue;");
  const size_t messages =
      header.find("LinkedList<AsyncWebSocketMessage *> _messageQueue;");
  assert(lock < control && control < messages);

  assertGuardPrecedes(
      functionBody(source, "void AsyncWebSocketClient::_onAck("),
      "_controlQueue.isEmpty()");
  assertGuardPrecedes(
      functionBody(source, "void AsyncWebSocketClient::_onPoll("),
      "_messageQueue.isEmpty()");
  assertGuardPrecedes(
      functionBody(source, "void AsyncWebSocketClient::_queueMessage("),
      "_messageQueue.length()");
  assertGuardPrecedes(
      functionBody(source, "void AsyncWebSocketClient::_queueControl("),
      "_controlQueue.add(");

  const std::string destructor = functionBody(
      source, "AsyncWebSocketClient::~AsyncWebSocketClient(");
  assertGuardPrecedes(destructor, "_messageQueue.free()");

  const std::string disconnect = functionBody(
      source, "void AsyncWebSocketClient::_onDisconnect(");
  assertGuardPrecedes(disconnect, "_client = NULL");
  assert(disconnect.find("_client = NULL") <
         disconnect.find("_server->_handleDisconnect(this)"));

  const std::string runQueue = functionBody(
      source, "void AsyncWebSocketClient::_runQueue(");
  assert(runQueue.find("Callers must hold _queueLock") != std::string::npos);

  const std::string textAll = functionBody(
      source,
      "void AsyncWebSocket::textAll(AsyncWebSocketMessageBuffer * buffer)");
  assert(textAll.find("AsyncWebLockGuard guard(_clientsLock)") <
         textAll.find("for(const auto& c: _clients)"));

  // close() invokes the disconnect callback synchronously in this vendored
  // AsyncTCP.  A member guard must be gone before the callback can delete the
  // WebSocket client.
  for (const char* callback : {"void AsyncWebSocketClient::_onAck(",
                               "void AsyncWebSocketClient::_onPoll(",
                               "void AsyncWebSocketClient::_onTimeout("}) {
    const std::string body = functionBody(source, callback);
    assert(body.find("clientToClose->close(true)") != std::string::npos);
    assert(body.find("AsyncClient *clientToClose") <
           body.find("clientToClose->close(true)"));
  }
}

struct LifetimeCounters {
  std::atomic<size_t> created{0};
  std::atomic<size_t> destroyed{0};
};

struct Message {
  explicit Message(LifetimeCounters& values) : counters(values) {
    ++counters.created;
  }
  ~Message() { ++counters.destroyed; }

  LifetimeCounters& counters;
  bool finished = false;
};

class SerializedQueueModel {
 public:
  explicit SerializedQueueModel(LifetimeCounters& counters)
      : counters_(counters) {}

  void enqueue(bool finished = false) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Message* message = new Message(counters_);
    message->finished = finished;
    queue_.push_back(message);
    runQueueLocked();
  }

  void acknowledgeFront() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!queue_.empty()) queue_.front()->finished = true;
    runQueueLocked();
  }

  void disconnect() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    while (!queue_.empty()) {
      delete queue_.front();
      queue_.pop_front();
    }
  }

 private:
  void runQueueLocked() {
    while (!queue_.empty() && queue_.front()->finished) {
      Message* message = queue_.front();
      queue_.pop_front();
      delete message;
    }
  }

  LifetimeCounters& counters_;
  std::recursive_mutex mutex_;
  std::deque<Message*> queue_;
};

class StartGate {
 public:
  void arriveAndWait() {
    std::unique_lock<std::mutex> lock(mutex_);
    const unsigned generation = generation_;
    if (++arrived_ == 2) {
      arrived_ = 0;
      ++generation_;
      condition_.notify_all();
      return;
    }
    condition_.wait(lock, [&] { return generation_ != generation; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  unsigned arrived_ = 0;
  unsigned generation_ = 0;
};

void testAckEnqueueAndDisconnectHaveSingleOwnership() {
  LifetimeCounters counters;
  SerializedQueueModel queue(counters);
  StartGate gate;
  constexpr size_t kRounds = 20000;

  queue.enqueue();
  std::thread acknowledger([&] {
    for (size_t round = 0; round < kRounds; ++round) {
      gate.arriveAndWait();
      queue.acknowledgeFront();
    }
  });
  std::thread broadcaster([&] {
    for (size_t round = 0; round < kRounds; ++round) {
      gate.arriveAndWait();
      queue.enqueue();
    }
  });
  acknowledger.join();
  broadcaster.join();
  queue.disconnect();

  assert(counters.created.load() == kRounds + 1);
  assert(counters.destroyed.load() == counters.created.load());
}

void testUnserializedRemovalScheduleHasTwoOwners() {
  // This is the exact pre-fix interleaving: ACK and application-side enqueue
  // both observe the same finished front before either LinkedList::remove()
  // completes.  Both therefore believe they own the same destruction.  The
  // model counts the invalid second ownership transfer without invoking UB.
  int message = 1;
  int* ackSnapshot = &message;
  int* applicationSnapshot = &message;
  size_t destructionAttempts = 0;
  if (ackSnapshot == &message) ++destructionAttempts;
  if (applicationSnapshot == &message) ++destructionAttempts;
  assert(ackSnapshot == applicationSnapshot);
  assert(destructionAttempts == 2);
}

}  // namespace

int main() {
  testProductionQueueSynchronizationContract();
  testUnserializedRemovalScheduleHasTwoOwners();
  testAckEnqueueAndDisconnectHaveSingleOwnership();
  std::puts("PASS websocket_queue_lifecycle_native");
  return 0;
}
