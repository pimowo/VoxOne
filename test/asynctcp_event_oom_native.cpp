#include "../src/AsyncWebServer/AsyncTCPLifecycle.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr size_t kQueueSize = 32;
constexpr size_t kFallbackCount = kQueueSize + 2;

enum class EventType { Clear, Connected, Poll, Sent, Dns, Accept, Error };

struct Pcb {
  Pcb() { ++created; }
  ~Pcb() { ++destroyed; }

  void abort() {
    assert(!aborted);
    aborted = true;
    ++aborts;
  }

  bool aborted = false;
  static int created;
  static int destroyed;
  static int aborts;
};

int Pcb::created = 0;
int Pcb::destroyed = 0;
int Pcb::aborts = 0;

struct Client {
  explicit Client(Pcb* initialPcb) : pcb(initialPcb) { ++created; }
  ~Client() { ++destroyed; }

  void invalidate() {
    pcb = nullptr;
    invalidated = true;
  }

  Pcb* pcb;
  bool invalidated = false;
  int connectedCallbacks = 0;
  int pollCallbacks = 0;
  int sentCallbacks = 0;
  int dnsCallbacks = 0;
  int acceptedCallbacks = 0;
  int errorCallbacks = 0;
  int disconnectCallbacks = 0;
  static int created;
  static int destroyed;
};

int Client::created = 0;
int Client::destroyed = 0;

struct OwnedPayload {
  OwnedPayload() { ++created; }
  ~OwnedPayload() { ++destroyed; }
  static int created;
  static int destroyed;
};

int OwnedPayload::created = 0;
int OwnedPayload::destroyed = 0;

struct Event {
  EventType type;
  bool fallback;
  Client* owner;
  Pcb* pcb;
  Client* acceptedClient;
  OwnedPayload* payload;
};

class EventAllocator {
 public:
  Event* allocate() {
    ++allocationAttempts;
    if (!failHeapAllocation) {
      Event* event = static_cast<Event*>(std::malloc(sizeof(Event)));
      assert(event != nullptr);
      event->fallback = false;
      ++heapAllocations;
      return event;
    }
    Event* event = fallback.acquire();
    if (event) {
      event->fallback = true;
      ++fallbackAllocations;
    }
    return event;
  }

  void release(Event* event) {
    if (!event) return;
    if (event->payload) {
      delete event->payload;
      event->payload = nullptr;
    }
    if (event->fallback) {
      assert(fallback.release(event));
      ++fallbackReleases;
      return;
    }
    std::free(event);
    ++heapReleases;
  }

  size_t availableFallback() const { return fallback.available(); }

  bool failHeapAllocation = true;
  int allocationAttempts = 0;
  int heapAllocations = 0;
  int heapReleases = 0;
  int fallbackAllocations = 0;
  int fallbackReleases = 0;
  asynctcp_lifecycle::FixedEventPool<Event, kFallbackCount> fallback;
};

class Queue {
 public:
  bool send(Event* event, bool prepend) {
    ++sendAttempts;
    if (rejectNext) {
      rejectNext = false;
      ++rejections;
      return false;
    }
    if (prepend) {
      events.push_front(event);
    } else {
      events.push_back(event);
    }
    return true;
  }

  Event* pop() {
    if (events.empty()) return nullptr;
    Event* event = events.front();
    events.pop_front();
    return event;
  }

  size_t size() const { return events.size(); }

  bool rejectNext = false;
  int sendAttempts = 0;
  int rejections = 0;
  std::deque<Event*> events;
};

Event* allocatePacket(EventAllocator& allocator, EventType type, Client* owner,
                      Pcb* pcb = nullptr, Client* accepted = nullptr,
                      OwnedPayload* payload = nullptr) {
  Event* event = allocator.allocate();
  if (!event) return nullptr;
  event->type = type;
  event->owner = owner;
  event->pcb = pcb;
  event->acceptedClient = accepted;
  event->payload = payload;
  return event;
}

void abortConnection(EventAllocator& allocator, Queue& queue, Client* client,
                     Pcb* pcb) {
  if (client) client->invalidate();
  if (pcb) pcb->abort();
  Event* error = allocatePacket(allocator, EventType::Error, client);
  assert(error != nullptr);
  assert(queue.send(error, true));
}

void discardAcceptedClient(Client* client, Pcb* pcb) {
  if (client) client->invalidate();
  if (pcb) pcb->abort();
  delete client;
}

void handleEvent(EventAllocator& allocator, Event* event) {
  assert(event != nullptr);
  switch (event->type) {
    case EventType::Connected:
      ++event->owner->connectedCallbacks;
      break;
    case EventType::Poll:
      ++event->owner->pollCallbacks;
      break;
    case EventType::Sent:
      ++event->owner->sentCallbacks;
      break;
    case EventType::Dns:
      ++event->owner->dnsCallbacks;
      break;
    case EventType::Accept:
      ++event->acceptedClient->acceptedCallbacks;
      delete event->acceptedClient;
      event->acceptedClient = nullptr;
      break;
    case EventType::Error:
      ++event->owner->errorCallbacks;
      ++event->owner->disconnectCallbacks;
      break;
    case EventType::Clear:
      break;
  }
  allocator.release(event);
}

void purgeOwner(EventAllocator& allocator, Queue& queue, Client* owner) {
  const size_t queued = queue.size();
  for (size_t i = 0; i < queued; ++i) {
    Event* event = queue.pop();
    assert(event != nullptr);
    if (event->owner == owner) {
      allocator.release(event);
    } else {
      assert(queue.send(event, false));
    }
  }
}

void clearCallback(EventAllocator& allocator, Queue& queue, Client* owner,
                   bool allocationFails, bool queueRejects) {
  if (allocationFails) {
    ++allocator.allocationAttempts;
    purgeOwner(allocator, queue, owner);
    return;
  }
  allocator.failHeapAllocation = false;
  Event* event = allocatePacket(allocator, EventType::Clear, owner);
  allocator.failHeapAllocation = true;
  assert(event != nullptr);
  queue.rejectNext = queueRejects;
  if (!queue.send(event, true)) {
    allocator.release(event);
    purgeOwner(allocator, queue, owner);
  }
}

bool connectedCallback(EventAllocator& allocator, Queue& queue, Client* client,
                       Pcb* pcb) {
  Event* event = allocatePacket(allocator, EventType::Connected, client, pcb);
  if (!event || !queue.send(event, true)) {
    allocator.release(event);
    abortConnection(allocator, queue, client, pcb);
    return false;
  }
  return true;
}

bool notificationCallback(EventAllocator& allocator, Queue& queue,
                          EventType type, Client* client, Pcb* pcb) {
  Event* event = allocatePacket(allocator, type, client, pcb);
  if (!event || !queue.send(event, false)) {
    allocator.release(event);
    abortConnection(allocator, queue, client, pcb);
    return false;
  }
  return true;
}

bool dnsCallback(EventAllocator& allocator, Queue& queue, Client* client) {
  Event* event = allocatePacket(allocator, EventType::Dns, client);
  if (!event) return false;
  if (!queue.send(event, true)) {
    allocator.release(event);
    return false;
  }
  return true;
}

bool acceptCallback(EventAllocator& allocator, Queue& queue, Client* client,
                    Pcb* pcb) {
  Event* event =
      allocatePacket(allocator, EventType::Accept, nullptr, pcb, client);
  if (!event || !queue.send(event, true)) {
    allocator.release(event);
    discardAcceptedClient(client, pcb);
    return false;
  }
  return true;
}

bool errorCallback(EventAllocator& allocator, Queue& queue, Client* client) {
  client->invalidate();
  Event* event = allocatePacket(allocator, EventType::Error, client);
  if (!event) return false;
  if (!queue.send(event, true)) {
    allocator.release(event);
    return false;
  }
  return true;
}

void testEveryOomCallbackUsesSafeOwnership() {
  EventAllocator allocator;
  Queue queue;
  Pcb* pcb = new Pcb;
  Client* client = new Client(pcb);
  Pcb* otherPcb = new Pcb;
  Client* other = new Client(otherPcb);

  Event* owned = allocatePacket(allocator, EventType::Poll, client, pcb,
                                nullptr, new OwnedPayload);
  assert(owned != nullptr && queue.send(owned, false));
  Event* survivor = allocatePacket(allocator, EventType::Poll, other, otherPcb);
  assert(survivor != nullptr && queue.send(survivor, false));
  clearCallback(allocator, queue, client, true, false);
  assert(OwnedPayload::created == OwnedPayload::destroyed);
  assert(queue.size() == 1 && queue.pop() == survivor);
  allocator.release(survivor);

  assert(connectedCallback(allocator, queue, client, pcb));
  handleEvent(allocator, queue.pop());
  assert(client->connectedCallbacks == 1 && !client->invalidated);

  assert(notificationCallback(allocator, queue, EventType::Poll, client, pcb));
  assert(notificationCallback(allocator, queue, EventType::Sent, client, pcb));
  handleEvent(allocator, queue.pop());
  handleEvent(allocator, queue.pop());
  assert(client->pollCallbacks == 1 && client->sentCallbacks == 1);

  assert(dnsCallback(allocator, queue, client));
  handleEvent(allocator, queue.pop());
  assert(client->dnsCallbacks == 1);

  Pcb* acceptedPcb = new Pcb;
  Client* accepted = new Client(acceptedPcb);
  assert(acceptCallback(allocator, queue, accepted, acceptedPcb));
  handleEvent(allocator, queue.pop());
  delete acceptedPcb;

  delete pcb;
  pcb = nullptr;
  assert(errorCallback(allocator, queue, client));
  assert(client->invalidated && client->pcb == nullptr);
  handleEvent(allocator, queue.pop());
  assert(client->errorCallbacks == 1 && client->disconnectCallbacks == 1);

  assert(allocator.heapAllocations == 0);
  assert(allocator.allocationAttempts == 9);
  assert(allocator.fallbackAllocations == allocator.fallbackReleases);
  assert(allocator.availableFallback() == kFallbackCount);
  delete client;
  delete other;
  delete otherPcb;
}

void testRejectedEventsReleaseExactlyOnce() {
  EventAllocator allocator;
  Queue queue;

  Pcb* clearPcb = new Pcb;
  Client* clearClient = new Client(clearPcb);
  Event* queued = allocatePacket(allocator, EventType::Poll, clearClient,
                                 clearPcb, nullptr, new OwnedPayload);
  assert(queued != nullptr && queue.send(queued, false));
  clearCallback(allocator, queue, clearClient, false, true);
  assert(queue.size() == 0);
  assert(OwnedPayload::created == OwnedPayload::destroyed);

  Pcb* connectedPcb = new Pcb;
  Client* connectedClient = new Client(connectedPcb);
  queue.rejectNext = true;
  assert(!connectedCallback(allocator, queue, connectedClient, connectedPcb));
  assert(connectedClient->invalidated && connectedPcb->aborted);
  handleEvent(allocator, queue.pop());
  assert(connectedClient->errorCallbacks == 1);
  assert(connectedClient->disconnectCallbacks == 1);

  Pcb* pollPcb = new Pcb;
  Client* pollClient = new Client(pollPcb);
  queue.rejectNext = true;
  assert(!notificationCallback(allocator, queue, EventType::Poll,
                               pollClient, pollPcb));
  assert(pollClient->invalidated && pollPcb->aborted);
  handleEvent(allocator, queue.pop());

  Pcb* sentPcb = new Pcb;
  Client* sentClient = new Client(sentPcb);
  queue.rejectNext = true;
  assert(!notificationCallback(allocator, queue, EventType::Sent,
                               sentClient, sentPcb));
  assert(sentClient->invalidated && sentPcb->aborted);
  handleEvent(allocator, queue.pop());

  Pcb* dnsPcb = new Pcb;
  Client* dnsClient = new Client(dnsPcb);
  queue.rejectNext = true;
  assert(!dnsCallback(allocator, queue, dnsClient));
  assert(!dnsClient->invalidated && !dnsPcb->aborted);

  Pcb* errorPcb = new Pcb;
  Client* errorClient = new Client(errorPcb);
  queue.rejectNext = true;
  assert(!errorCallback(allocator, queue, errorClient));
  assert(errorClient->invalidated && errorClient->errorCallbacks == 0);

  Pcb* acceptedPcb = new Pcb;
  Client* accepted = new Client(acceptedPcb);
  queue.rejectNext = true;
  assert(!acceptCallback(allocator, queue, accepted, acceptedPcb));
  assert(acceptedPcb->aborted);

  assert(queue.size() == 0);
  assert(allocator.fallbackAllocations == allocator.fallbackReleases);
  assert(allocator.availableFallback() == kFallbackCount);

  delete clearClient;
  delete clearPcb;
  delete connectedClient;
  delete connectedPcb;
  delete pollClient;
  delete pollPcb;
  delete sentClient;
  delete sentPcb;
  delete dnsClient;
  delete dnsPcb;
  delete errorClient;
  delete errorPcb;
  delete acceptedPcb;
}

void testFallbackCapacityCoversQueueHandlerAndCurrentCallback() {
  asynctcp_lifecycle::FixedEventPool<Event, kFallbackCount> pool;
  std::vector<Event*> packets;
  for (size_t i = 0; i < kFallbackCount; ++i) {
    Event* packet = pool.acquire();
    assert(packet != nullptr);
    packets.push_back(packet);
  }
  assert(pool.available() == 0);
  assert(pool.acquire() == nullptr);
  for (Event* packet : packets) assert(pool.release(packet));
  assert(pool.available() == kFallbackCount);
  assert(!pool.release(packets.front()));
  Event external{};
  assert(!pool.release(&external));
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string functionBody(const std::string& source, const char* begin,
                         const char* end) {
  const size_t first = source.find(begin);
  const size_t last = source.find(end, first);
  assert(first != std::string::npos && last != std::string::npos);
  return source.substr(first, last - first);
}

void testProductionCallbackContract() {
  const std::string source = readFile("src/AsyncWebServer/AsyncTCP.cpp");
  assert(source.find("_async_event_fallback_count = XQUEUE_SIZE + 2") !=
         std::string::npos);
  assert(source.find("_async_event_fallback.acquire()") != std::string::npos);
  assert(source.find("_async_event_fallback.release(event)") !=
         std::string::npos);
  assert(source.find("event->fallback = true") !=
         std::string::npos);

  const std::string clear = functionBody(source, "static int8_t _tcp_clear_events",
                                         "static int8_t _abort_lwip_connection");
  assert(clear.find("if(!e)") < clear.find("e->event"));
  assert(clear.find("_remove_events_with_arg(arg)") != std::string::npos);

  const std::string connected = functionBody(source, "static int8_t _tcp_connected",
                                             "static int8_t _tcp_poll");
  assert(connected.find("_allocate_async_event()") != std::string::npos);
  assert(connected.find("if(!e)") < connected.find("e->event"));
  assert(connected.find("_abort_lwip_connection(pcb)") !=
         std::string::npos);

  const std::string poll = functionBody(source, "static int8_t _tcp_poll",
                                        "static int8_t _tcp_recv");
  const std::string sent = functionBody(source, "static int8_t _tcp_sent",
                                        "static void _tcp_error");
  const std::string error = functionBody(source, "static void _tcp_error",
                                         "static void _tcp_dns_found");
  const std::string dns = functionBody(source, "static void _tcp_dns_found",
                                       "static int8_t _tcp_accept");
  const std::string accept = functionBody(source, "static int8_t _tcp_accept",
                                          "/*\n * TCP/IP API Calls");
  for (const std::string* body : {&poll, &sent, &error, &dns, &accept}) {
    assert(body->find("_allocate_async_event()") != std::string::npos);
    assert(body->find("if(!e)") < body->find("e->event"));
  }
  assert(poll.find("return _abort_lwip_connection(pcb)") !=
         std::string::npos);
  assert(sent.find("return _abort_lwip_connection(pcb)") !=
         std::string::npos);
  assert(dns.find("_prepend_async_event(&e)") != std::string::npos);
  assert(dns.find("_send_async_event(&e)") == std::string::npos);
  assert(accept.find("_discard_accepted_client(client)") !=
         std::string::npos);
  assert(error.find("AsyncClient::_s_lwip_error(arg);") <
         error.find("_allocate_async_event()"));
}

}  // namespace

int main() {
  testEveryOomCallbackUsesSafeOwnership();
  testRejectedEventsReleaseExactlyOnce();
  testFallbackCapacityCoversQueueHandlerAndCurrentCallback();
  testProductionCallbackContract();

  assert(Pcb::created == Pcb::destroyed);
  assert(Client::created == Client::destroyed);
  assert(OwnedPayload::created == OwnedPayload::destroyed);
  std::puts("PASS asynctcp_event_oom_native");
  return 0;
}
