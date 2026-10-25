#include "../src/AsyncWebServer/AsyncTCPLifecycle.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iterator>
#include <string>

namespace {

enum class EventType { Rx, Sent, Poll, Error, Fin };

struct Pbuf {
  explicit Pbuf(size_t size) : len(size) { ++created; }
  ~Pbuf() { ++destroyed; }

  size_t len;
  static int created;
  static int destroyed;
};

int Pbuf::created = 0;
int Pbuf::destroyed = 0;

struct Pcb {
  Pcb() { ++created; }
  ~Pcb() { ++destroyed; }
  void touch() { ++touches; }

  static int created;
  static int destroyed;
  static int touches;
};

int Pcb::created = 0;
int Pcb::destroyed = 0;
int Pcb::touches = 0;

struct Client;

struct Event {
  Event(EventType eventType, Client* eventOwner, Pcb* eventPcb = nullptr,
        Pbuf* eventPbuf = nullptr)
      : type(eventType), owner(eventOwner), pcb(eventPcb), pbuf(eventPbuf) {
    ++created;
  }
  ~Event() { ++destroyed; }

  EventType type;
  Client* owner;
  Pcb* pcb;
  Pbuf* pbuf;
  static int created;
  static int destroyed;
};

int Event::created = 0;
int Event::destroyed = 0;

class Queue {
 public:
  size_t count() const { return events_.size(); }

  Event* pop() {
    if (events_.empty()) return nullptr;
    Event* event = events_.front();
    events_.pop_front();
    return event;
  }

  void push(Event* event) { events_.push_back(event); }
  void pushFront(Event* event) { events_.push_front(event); }

 private:
  std::deque<Event*> events_;
};

void releaseEvent(Event* event) {
  if (event->type == EventType::Rx && event->pbuf) {
    delete event->pbuf;
    event->pbuf = nullptr;
  }
  delete event;
}

size_t removeEventsForClient(Queue& queue, Client* client) {
  return asynctcp_lifecycle::removeQueuedEvents<Queue, Event>(
      queue, client,
      [](Event* event, Client* owner) { return event->owner == owner; },
      [](Event* event) { releaseEvent(event); });
}

struct Client {
  explicit Client(Pcb* initialPcb) : pcb(initialPcb) {}

  void invalidateFromLwipError() {
    asynctcp_lifecycle::invalidateClosedSlot(slotState);
    pcb = nullptr;
  }

  void receive(Event* event) {
    ++dataCallbacks;
    if (pcb &&
        asynctcp_lifecycle::closedSlotIsActive(slotState)) {
      pcb->touch();
      ++tcpRecvedCalls;
      tcpRecvedBytes += event->pbuf->len;
    }
  }

  Pcb* pcb;
  uint32_t slotState = asynctcp_lifecycle::kActiveClosedSlot;
  int tcpRecvedCalls = 0;
  size_t tcpRecvedBytes = 0;
  int dataCallbacks = 0;
  int errorCallbacks = 0;
  int disconnectCallbacks = 0;
};

void handleEvent(Event* event) {
  Client* client = event->owner;
  if (event->type == EventType::Rx) {
    client->receive(event);
  } else if (event->type == EventType::Error) {
    ++client->errorCallbacks;
    ++client->disconnectCallbacks;
  } else if (event->type == EventType::Fin) {
    ++client->disconnectCallbacks;
  }
  releaseEvent(event);
}

void lwipError(Queue& queue, Client& client, Pcb*& pcb) {
  // lwIP documents the PCB as already freed before invoking this callback.
  delete pcb;
  pcb = nullptr;
  client.invalidateFromLwipError();
  removeEventsForClient(queue, &client);
  queue.pushFront(new Event(EventType::Error, &client));
}

void lwipFin(Queue& queue, Client& client, Pcb*& pcb) {
  asynctcp_lifecycle::invalidateClosedSlot(client.slotState);
  client.pcb = nullptr;
  delete pcb;
  pcb = nullptr;
  queue.push(new Event(EventType::Fin, &client));
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void testQueuedRxIsPurgedBeforeErrorCallback() {
  Queue queue;
  Pcb* pcb = new Pcb;
  Client client(pcb);
  Pcb* otherPcb = new Pcb;
  Client other(otherPcb);
  const int touchesBefore = Pcb::touches;

  queue.push(new Event(EventType::Rx, &client, pcb, new Pbuf(27)));
  Event* otherPoll = new Event(EventType::Poll, &other, otherPcb);
  queue.push(otherPoll);
  queue.push(new Event(EventType::Sent, &client, pcb));

  lwipError(queue, client, pcb);

  assert(client.pcb == nullptr);
  assert(client.slotState == asynctcp_lifecycle::kInvalidatedClosedSlot);
  assert(Pbuf::destroyed == 1);

  handleEvent(queue.pop());
  assert(client.errorCallbacks == 1);
  assert(client.disconnectCallbacks == 1);
  assert(client.tcpRecvedCalls == 0);
  assert(Pcb::touches == touchesBefore);

  assert(queue.pop() == otherPoll);
  releaseEvent(otherPoll);
  assert(queue.pop() == nullptr);
  delete otherPcb;
}

void testRxAlreadyOwnedByAsyncTaskCannotAckFreedPcb() {
  Queue queue;
  Pcb* pcb = new Pcb;
  Client client(pcb);
  const int touchesBefore = Pcb::touches;

  queue.push(new Event(EventType::Rx, &client, pcb, new Pbuf(245)));
  Event* inFlightRx = queue.pop();
  lwipError(queue, client, pcb);

  handleEvent(inFlightRx);
  assert(client.dataCallbacks == 1);
  assert(client.tcpRecvedCalls == 0);
  assert(Pcb::touches == touchesBefore);

  handleEvent(queue.pop());
  assert(client.errorCallbacks == 1);
  assert(client.disconnectCallbacks == 1);
  assert(queue.pop() == nullptr);
}

void testNormalRxAndFin() {
  Queue queue;
  Pcb* pcb = new Pcb;
  Client client(pcb);
  const int touchesBefore = Pcb::touches;

  queue.push(new Event(EventType::Rx, &client, pcb, new Pbuf(245)));
  handleEvent(queue.pop());
  queue.push(new Event(EventType::Rx, &client, pcb, new Pbuf(27)));
  handleEvent(queue.pop());

  assert(client.dataCallbacks == 2);
  assert(client.tcpRecvedCalls == 2);
  assert(client.tcpRecvedBytes == 272);
  assert(Pcb::touches == touchesBefore + 2);

  lwipFin(queue, client, pcb);
  handleEvent(queue.pop());
  assert(client.errorCallbacks == 0);
  assert(client.disconnectCallbacks == 1);
  assert(client.slotState == asynctcp_lifecycle::kInvalidatedClosedSlot);
  assert(queue.pop() == nullptr);
}

void testClosedSlotReservation() {
  uint32_t active = asynctcp_lifecycle::kActiveClosedSlot;
  asynctcp_lifecycle::invalidateClosedSlot(active);
  assert(!asynctcp_lifecycle::closedSlotIsActive(active));
  assert(!asynctcp_lifecycle::closedSlotIsReusable(active));

  const uint32_t releasedGeneration = 17;
  assert(asynctcp_lifecycle::closedSlotIsReusable(releasedGeneration));
}

void testProductionIntegrationContract() {
  const std::string source = readFile("src/AsyncWebServer/AsyncTCP.cpp");
  const size_t errorBegin = source.find("static void _tcp_error");
  const size_t invalidate = source.find("AsyncClient::_s_lwip_error(arg);",
                                        errorBegin);
  const size_t purge = source.find("_remove_events_with_arg(arg);", errorBegin);
  const size_t allocation = source.find("malloc(sizeof(lwip_event_packet_t))",
                                         errorBegin);
  assert(errorBegin != std::string::npos);
  assert(invalidate < purge && purge < allocation);
  assert(source.find("_rx_ack_len(0)") != std::string::npos);
  assert(source.find("e->recv.pb = NULL;") != std::string::npos);

  const size_t asyncErrorBegin = source.find("void AsyncClient::_error");
  const size_t asyncErrorEnd = source.find("//In LwIP Thread", asyncErrorBegin);
  const std::string asyncError =
      source.substr(asyncErrorBegin, asyncErrorEnd - asyncErrorBegin);
  assert(asyncError.find("tcp_arg(") == std::string::npos);
  assert(asyncError.find("_pcb->") == std::string::npos);
}

}  // namespace

int main() {
  testQueuedRxIsPurgedBeforeErrorCallback();
  testRxAlreadyOwnedByAsyncTaskCannotAckFreedPcb();
  testNormalRxAndFin();
  testClosedSlotReservation();
  testProductionIntegrationContract();

  assert(Pbuf::created == Pbuf::destroyed);
  assert(Pcb::created == Pcb::destroyed);
  assert(Event::created == Event::destroyed);
  std::puts("PASS asynctcp_lifecycle_native");
  return 0;
}
