#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

void assertClientGuarded(const std::string& source,
                         const std::string& signature,
                         const char* operation) {
  const std::string body = functionBody(source, signature);
  const size_t guard = body.find("AsyncWebLockGuard guard(_clientsLock)");
  const size_t lookup = body.find("_clientLocked(id)");
  const size_t use = body.find(operation, lookup);
  assert(guard != std::string::npos);
  assert(lookup != std::string::npos);
  assert(use != std::string::npos);
  assert(guard < lookup && lookup < use);
}

void testProductionLifetimeContract() {
  const std::string header =
      readFile("src/AsyncWebServer/AsyncWebSocket.h");
  const std::string source =
      readFile("src/AsyncWebServer/AsyncWebSocket.cpp");
  const std::string netserver = readFile("src/core/netserver.cpp");
  const std::string synchronization =
      readFile("src/AsyncWebServer/AsyncWebSynchronization.h");

  // A raw lookup or a shallow copy of the client list cannot express a
  // lifetime guarantee and must not remain in the public interface.
  assert(header.find("AsyncWebSocketClient * client(uint32_t id)") ==
         std::string::npos);
  assert(header.find("getClients() const") == std::string::npos);
  assert(source.find("AsyncWebSocket::client(uint32_t id)") ==
         std::string::npos);
  assert(source.find("AsyncWebSocket::getClients() const") ==
         std::string::npos);
  assert(netserver.find("websocket.client(") == std::string::npos);

  const size_t lockedLookup = header.find(
      "AsyncWebSocketClient * _clientLocked(uint32_t id) const;");
  const size_t publicSection = header.find("\n  public:", lockedLookup);
  assert(lockedLookup != std::string::npos);
  assert(publicSection != std::string::npos);
  assert(header.find("valid only while the caller holds _clientsLock") !=
         std::string::npos);

  assertClientGuarded(source, "void AsyncWebSocket::close(", "c->close(");
  assertClientGuarded(source, "void AsyncWebSocket::ping(", "c->ping(");
  assertClientGuarded(
      source,
      "void AsyncWebSocket::text(uint32_t id, const char * message, size_t len)",
      "c->text(");
  assertClientGuarded(
      source,
      "void AsyncWebSocket::binary(uint32_t id, const char * message, size_t len)",
      "c->binary(");
  assertClientGuarded(source, "void AsyncWebSocket::message(", "c->message(");
  assertClientGuarded(source, "size_t AsyncWebSocket::printf(", "c->printf(");
  assertClientGuarded(
      source,
      "void AsyncWebSocket::text(uint32_t id, const __FlashStringHelper *message)",
      "c->text(");
  assertClientGuarded(
      source,
      "void AsyncWebSocket::binary(uint32_t id, const __FlashStringHelper *message, size_t len)",
      "c-> binary(");

  const std::string hasClient =
      functionBody(source, "bool AsyncWebSocket::hasClient(");
  assert(hasClient.find("AsyncWebLockGuard guard(_clientsLock)") <
         hasClient.find("_clientLocked(id)"));

  const std::string cleanup =
      functionBody(source, "void AsyncWebSocket::cleanupClients(");
  const size_t cleanupGuard =
      cleanup.find("AsyncWebLockGuard guard(_clientsLock)");
  assert(cleanupGuard < cleanup.find("_clients.count_if("));
  assert(cleanupGuard < cleanup.find("c->close()"));

  const std::string textAll = functionBody(
      source,
      "void AsyncWebSocket::textAll(AsyncWebSocketMessageBuffer * buffer)");
  assert(textAll.find("AsyncWebLockGuard guard(_clientsLock)") <
         textAll.find("for(const auto& c: _clients)"));

  const std::string message =
      functionBody(source, "void AsyncWebSocket::message(");
  assert(message.find("delete message;") != std::string::npos);

  // NetServer reads/copies the security binding only inside the existing
  // forEachClient lifetime scope.
  for (const char* signature : {
           "ScrollCommandQueueStatus enqueueScrollSettingsCommand(",
           "WebSocketAuthorization queuedScrollCommandAuthorization("}) {
    const std::string body = functionBody(netserver, signature);
    assert(body.find("websocket.forEachClient(") != std::string::npos);
    assert(body.find("webSocketBinding(client)") != std::string::npos);
  }

  // The server-to-client order is _clientsLock then _queueLock. Disconnect
  // drops _queueLock before asking the server to remove/delete the client.
  const std::string disconnect = functionBody(
      source, "void AsyncWebSocketClient::_onDisconnect(");
  assert(disconnect.find("}\n  _server->_handleDisconnect(this);") !=
         std::string::npos);
  const std::string remove =
      functionBody(source, "void AsyncWebSocket::_handleDisconnect(");
  assert(remove.find("AsyncWebLockGuard guard(_clientsLock)") <
         remove.find("_clients.remove_first("));

  for (const char* signature : {"void AsyncWebSocketClient::close(",
                                "void AsyncWebSocketClient::ping("}) {
    const std::string body = functionBody(source, signature);
    assert(body.find("AsyncWebLockGuard guard(_queueLock)") <
           body.find("_status"));
  }

  const std::string onData =
      functionBody(source, "void AsyncWebSocketClient::_onData(");
  const size_t statusGuard =
      onData.find("AsyncWebLockGuard guard(_queueLock)",
                  onData.find("AsyncClient *clientToClose"));
  const size_t statusWrite = onData.find("_status = WS_DISCONNECTED");
  const size_t synchronousClose = onData.find("clientToClose->close(true)");
  const size_t immediateReturn = onData.find("return;", synchronousClose);
  assert(statusGuard < statusWrite);
  assert(statusWrite < synchronousClose);
  assert(synchronousClose < immediateReturn);

  assert(synchronization.find("xSemaphoreCreateRecursiveMutex()") !=
         std::string::npos);
  assert(synchronization.find("xSemaphoreTakeRecursive(") !=
         std::string::npos);
  assert(synchronization.find("xSemaphoreGiveRecursive(") !=
         std::string::npos);
}

struct Counters {
  std::atomic<size_t> clientsCreated{0};
  std::atomic<size_t> clientsDestroyed{0};
  std::atomic<size_t> messagesCreated{0};
  std::atomic<size_t> messagesDestroyed{0};
};

struct Message {
  explicit Message(Counters& value) : counters(value) {
    ++counters.messagesCreated;
  }
  ~Message() { ++counters.messagesDestroyed; }

  Counters& counters;
};

struct Client {
  Client(uint32_t value, Counters& counts,
         const std::shared_ptr<std::atomic<bool>>& lifetime)
      : id(value), counters(counts), alive(lifetime) {
    ++counters.clientsCreated;
  }

  ~Client() {
    alive->store(false);
    for (Message* message : messages) delete message;
    ++counters.clientsDestroyed;
  }

  void enqueue(Message* message) { messages.push_back(message); }

  uint32_t id;
  Counters& counters;
  std::shared_ptr<std::atomic<bool>> alive;
  std::vector<Message*> messages;
};

class LegacyRegistry {
 public:
  explicit LegacyRegistry(Client* client) : client_(client) {}
  ~LegacyRegistry() { disconnect(); }

  Client* client(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return client_ && client_->id == id ? client_ : nullptr;
  }

  void disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    delete client_;
    client_ = nullptr;
  }

 private:
  std::mutex mutex_;
  Client* client_;
};

void testLegacyLookupExposesDeterministicLifetimeWindow() {
  Counters counters;
  auto alive = std::make_shared<std::atomic<bool>>(true);
  LegacyRegistry registry(new Client(7, counters, alive));
  std::mutex barrierMutex;
  std::condition_variable barrier;
  bool lookupComplete = false;
  bool deleteComplete = false;
  bool unsafeWindowObserved = false;

  std::thread taskA([&] {
    Client* client = registry.client(7);
    assert(client != nullptr);
    {
      std::lock_guard<std::mutex> lock(barrierMutex);
      lookupComplete = true;
    }
    barrier.notify_all();
    std::unique_lock<std::mutex> lock(barrierMutex);
    barrier.wait(lock, [&] { return deleteComplete; });
    // Dereferencing client here is the old production UAF. The lifetime token
    // proves the invalid window without intentionally executing UB in the test.
    unsafeWindowObserved = !alive->load();
  });

  std::thread taskB([&] {
    {
      std::unique_lock<std::mutex> lock(barrierMutex);
      barrier.wait(lock, [&] { return lookupComplete; });
    }
    registry.disconnect();
    {
      std::lock_guard<std::mutex> lock(barrierMutex);
      deleteComplete = true;
    }
    barrier.notify_all();
  });

  taskA.join();
  taskB.join();
  assert(unsafeWindowObserved);
  assert(counters.clientsCreated.load() == 1);
  assert(counters.clientsDestroyed.load() == 1);
}

class LockedRegistry {
 public:
  explicit LockedRegistry(Client* client) : client_(client) {}
  ~LockedRegistry() { disconnect(0, nullptr); }

  template <typename Operation>
  bool withClient(uint32_t id, Operation operation) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!client_ || client_->id != id) return false;
    operation(client_);
    return true;
  }

  void message(uint32_t id, Message* message) {
    if (!withClient(id, [message](Client* client) {
          client->enqueue(message);
        }))
      delete message;
  }

  void disconnect(uint32_t id, std::atomic<int>* probe) {
    if (probe) {
      if (mutex_.try_lock()) {
        probe->store(1);
        mutex_.unlock();
      } else {
        probe->store(2);
      }
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (id != 0 && (!client_ || client_->id != id)) return;
    delete client_;
    client_ = nullptr;
  }

 private:
  std::recursive_mutex mutex_;
  Client* client_;
};

void testLockedOperationPinsClientUntilMessageOwnershipTransfers() {
  Counters counters;
  auto alive = std::make_shared<std::atomic<bool>>(true);
  LockedRegistry registry(new Client(7, counters, alive));
  std::mutex barrierMutex;
  std::condition_variable barrier;
  bool operationEntered = false;
  bool allowOperationToFinish = false;
  std::atomic<int> disconnectProbe{0};

  std::thread taskA([&] {
    const bool found = registry.withClient(7, [&](Client* client) {
      {
        std::lock_guard<std::mutex> lock(barrierMutex);
        operationEntered = true;
      }
      barrier.notify_all();
      std::unique_lock<std::mutex> lock(barrierMutex);
      barrier.wait(lock, [&] { return allowOperationToFinish; });
      assert(alive->load());
      client->enqueue(new Message(counters));
    });
    assert(found);
  });

  {
    std::unique_lock<std::mutex> lock(barrierMutex);
    barrier.wait(lock, [&] { return operationEntered; });
  }
  std::thread taskB([&] { registry.disconnect(7, &disconnectProbe); });
  while (disconnectProbe.load() == 0) std::this_thread::yield();

  // Task B reached removal while task A was inside the operation, but could
  // not acquire the registry lock and therefore could not destroy the client.
  assert(disconnectProbe.load() == 2);
  assert(alive->load());
  assert(counters.clientsDestroyed.load() == 0);
  {
    std::lock_guard<std::mutex> lock(barrierMutex);
    allowOperationToFinish = true;
  }
  barrier.notify_all();

  taskA.join();
  taskB.join();
  assert(!alive->load());
  assert(counters.clientsCreated.load() == 1);
  assert(counters.clientsDestroyed.load() == 1);
  assert(counters.messagesCreated.load() == 1);
  assert(counters.messagesDestroyed.load() == 1);

  // The server-level message API owns its pointer even if no matching client
  // exists; the rejected message is released once, not leaked or double freed.
  registry.message(99, new Message(counters));
  assert(counters.messagesCreated.load() == 2);
  assert(counters.messagesDestroyed.load() == 2);
}

class LockedBroadcastRegistry {
 public:
  explicit LockedBroadcastRegistry(std::vector<Client*> clients)
      : clients_(std::move(clients)) {}

  ~LockedBroadcastRegistry() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (Client* client : clients_) delete client;
  }

  void broadcast(Counters& counters, std::mutex& barrierMutex,
                 std::condition_variable& barrier, bool& firstClientPinned,
                 bool& allowBroadcastToFinish) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (Client* client : clients_) {
      if (client->id == 7) {
        {
          std::lock_guard<std::mutex> barrierLock(barrierMutex);
          firstClientPinned = true;
        }
        barrier.notify_all();
        std::unique_lock<std::mutex> barrierLock(barrierMutex);
        barrier.wait(barrierLock, [&] { return allowBroadcastToFinish; });
      }
      client->enqueue(new Message(counters));
    }
  }

  void disconnect(uint32_t id, std::atomic<int>& probe) {
    if (mutex_.try_lock()) {
      probe.store(1);
      mutex_.unlock();
    } else {
      probe.store(2);
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (auto iterator = clients_.begin(); iterator != clients_.end();
         ++iterator) {
      if ((*iterator)->id != id) continue;
      delete *iterator;
      clients_.erase(iterator);
      return;
    }
  }

 private:
  std::recursive_mutex mutex_;
  std::vector<Client*> clients_;
};

void testDisconnectDuringBroadcastWaitsForLifetimeScope() {
  Counters counters;
  auto firstAlive = std::make_shared<std::atomic<bool>>(true);
  auto secondAlive = std::make_shared<std::atomic<bool>>(true);
  std::mutex barrierMutex;
  std::condition_variable barrier;
  bool firstClientPinned = false;
  bool allowBroadcastToFinish = false;
  std::atomic<int> disconnectProbe{0};

  {
    LockedBroadcastRegistry registry(
        {new Client(7, counters, firstAlive),
         new Client(8, counters, secondAlive)});
    std::thread broadcaster([&] {
      registry.broadcast(counters, barrierMutex, barrier, firstClientPinned,
                         allowBroadcastToFinish);
    });

    {
      std::unique_lock<std::mutex> lock(barrierMutex);
      barrier.wait(lock, [&] { return firstClientPinned; });
    }
    std::thread disconnecter(
        [&] { registry.disconnect(7, disconnectProbe); });
    while (disconnectProbe.load() == 0) std::this_thread::yield();

    // An RST/disconnect may arrive while textAll() is iterating, but removal
    // must wait for the server lifetime lock. Both clients therefore receive
    // ownership of their broadcast message before client 7 is destroyed.
    assert(disconnectProbe.load() == 2);
    assert(firstAlive->load() && secondAlive->load());
    {
      std::lock_guard<std::mutex> lock(barrierMutex);
      allowBroadcastToFinish = true;
    }
    barrier.notify_all();

    broadcaster.join();
    disconnecter.join();
    assert(!firstAlive->load());
    assert(secondAlive->load());
    assert(counters.clientsDestroyed.load() == 1);
    assert(counters.messagesCreated.load() == 2);
    assert(counters.messagesDestroyed.load() == 1);
  }

  assert(!secondAlive->load());
  assert(counters.clientsCreated.load() == 2);
  assert(counters.clientsDestroyed.load() == 2);
  assert(counters.messagesCreated.load() == 2);
  assert(counters.messagesDestroyed.load() == 2);
}

}  // namespace

int main() {
  testProductionLifetimeContract();
  testLegacyLookupExposesDeterministicLifetimeWindow();
  testLockedOperationPinsClientUntilMessageOwnershipTransfers();
  testDisconnectDuringBroadcastWaitsForLifetimeScope();
  std::puts("PASS websocket_client_lifetime_native");
  return 0;
}
