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

class ControlAckInterleavingModel {
 public:
  explicit ControlAckInterleavingModel(bool suppressDuplicateControl)
      : suppressDuplicateControl_(suppressDuplicateControl) {}

  void queueControl() {
    controlQueued_ = true;
    runQueue();
  }

  void queueMessage(size_t wireLength) {
    messageQueued_ = true;
    messageWireLength_ = wireLength;
    runQueue();
  }

  void acknowledge(size_t length) {
    if (controlQueued_ && controlSent_) {
      assert(length >= kControlWireLength);
      length -= kControlWireLength;
      controlQueued_ = false;
    }
    if (length != 0 && messageQueued_) messageAcked_ += length;
    if (messageSent_ && messageAcked_ == messageWireLength_) {
      messageFinished_ = true;
    }
    runQueue();
  }

  size_t controlSends() const { return controlSends_; }
  size_t messageAcked() const { return messageAcked_; }
  bool messageFinished() const { return messageFinished_; }

  // Two-byte frame header plus the 13-byte HIL PONG payload.
  static constexpr size_t kControlWireLength = 15;

 private:
  void runQueue() {
    if (controlQueued_) {
      if (!suppressDuplicateControl_ || !controlSent_) {
        controlSent_ = true;
        ++controlSends_;
      }
      return;
    }
    if (messageQueued_ && !messageSent_) messageSent_ = true;
  }

  const bool suppressDuplicateControl_;
  bool controlQueued_ = false;
  bool controlSent_ = false;
  bool messageQueued_ = false;
  bool messageSent_ = false;
  bool messageFinished_ = false;
  size_t controlSends_ = 0;
  size_t messageWireLength_ = 0;
  size_t messageAcked_ = 0;
};

void testLegacyDuplicatePongPoisonsFollowingMessageAck() {
  ControlAckInterleavingModel queue(false);
  queue.queueControl();
  queue.queueMessage(42);
  assert(queue.controlSends() == 2);

  // A single cumulative ACK covers both copies, but the legacy queue removes
  // only one 15-byte control frame and credits the surplus to the data frame
  // before that frame has been sent.
  queue.acknowledge(2 * ControlAckInterleavingModel::kControlWireLength);
  assert(queue.messageAcked() == ControlAckInterleavingModel::kControlWireLength);
  queue.acknowledge(42);
  assert(!queue.messageFinished());
}

void testControlFrameIsSentOnceBeforeFollowingMessage() {
  ControlAckInterleavingModel queue(true);
  queue.queueControl();
  queue.queueMessage(42);
  assert(queue.controlSends() == 1);

  queue.acknowledge(ControlAckInterleavingModel::kControlWireLength);
  assert(queue.messageAcked() == 0);
  queue.acknowledge(42);
  assert(queue.messageFinished());
}

void testProductionControlSendIsIdempotent() {
  const std::string source =
      readFile("src/AsyncWebServer/AsyncWebSocket.cpp");
  const size_t controlClass = source.find("class AsyncWebSocketControl");
  const size_t send = source.find("size_t send(AsyncClient *client)", controlClass);
  const size_t nextSection = source.find("Basic Buffered Message", send);
  assert(controlClass != std::string::npos);
  assert(send != std::string::npos);
  assert(nextSection != std::string::npos);
  const std::string body = source.substr(send, nextSection - send);
  const size_t guard = body.find("if(_finished)");
  const size_t markSent = body.find("_finished = true;");
  const size_t write = body.find("webSocketSendFrame(");
  assert(guard != std::string::npos);
  assert(guard < markSent && markSent < write);
}

}  // namespace

int main() {
  testProductionQueueSynchronizationContract();
  testUnserializedRemovalScheduleHasTwoOwners();
  testAckEnqueueAndDisconnectHaveSingleOwnership();
  testLegacyDuplicatePongPoisonsFollowingMessageAck();
  testControlFrameIsSentOnceBeforeFollowingMessage();
  testProductionControlSendIsIdempotent();
  std::puts("PASS websocket_queue_lifecycle_native");
  return 0;
}
