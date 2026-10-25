#include "../src/AsyncWebServer/AsyncTCPLifecycle.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

enum class EventType { Rx, Sent, Poll, Clear, Error, Fin };

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
        Pbuf* eventPbuf = nullptr, uint8_t eventMarker = 0)
      : type(eventType), owner(eventOwner), pcb(eventPcb), pbuf(eventPbuf),
        marker(eventMarker) {
    ++created;
  }
  ~Event() { ++destroyed; }

  EventType type;
  Client* owner;
  Pcb* pcb;
  Pbuf* pbuf;
  uint8_t marker;
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

class InterleavingQueue {
 public:
  size_t count() const { return events_.size(); }

  Event* pop() {
    if (events_.empty()) return nullptr;
    Event* event = events_.front();
    events_.pop_front();
    return event;
  }

  void push(Event* event) {
    events_.push_back(event);
    if (!interleaved_) {
      interleaved_ = true;
      deliverOne();
      deliverOne();
    }
  }

  void seed(Event* event) { events_.push_back(event); }

  void drain() {
    while (!events_.empty()) deliverOne();
  }

  const std::vector<uint8_t>& delivered() const { return delivered_; }

 private:
  void deliverOne() {
    Event* event = pop();
    assert(event != nullptr);
    if (event->type == EventType::Rx) {
      assert(event->pbuf != nullptr && event->pbuf->len == 1436);
      delivered_.push_back(event->marker);
    }
    releaseEvent(event);
  }

  std::deque<Event*> events_;
  std::vector<uint8_t> delivered_;
  bool interleaved_ = false;
};

class ProducerInterleavingQueue {
 public:
  ProducerInterleavingQueue(Client* upload, Pcb* uploadPcb)
      : upload_(upload), uploadPcb_(uploadPcb) {}

  size_t count() const { return events_.size(); }

  Event* pop() {
    if (events_.empty()) return nullptr;
    Event* event = events_.front();
    events_.pop_front();
    return event;
  }

  void push(Event* event) {
    events_.push_back(event);
    if (!injected_) {
      injected_ = true;
      events_.push_back(new Event(EventType::Rx, upload_, uploadPcb_,
                                  new Pbuf(1436), 'D'));
    }
  }

  void seed(Event* event) { events_.push_back(event); }

 private:
  Client* upload_;
  Pcb* uploadPcb_;
  std::deque<Event*> events_;
  bool injected_ = false;
};

class MssRotationProducerQueue {
 public:
  MssRotationProducerQueue(Client* upload, Pcb* uploadPcb,
                           Client* closing)
      : upload_(upload), uploadPcb_(uploadPcb), closing_(closing) {}

  size_t count() const { return events_.size(); }

  Event* pop() {
    if (events_.empty()) return nullptr;
    Event* event = events_.front();
    events_.pop_front();
    if (!injected_ && event->owner == closing_) {
      injected_ = true;
      events_.push_back(new Event(EventType::Rx, upload_, uploadPcb_,
                                  new Pbuf(1436), 'B'));
      events_.push_back(new Event(EventType::Rx, upload_, uploadPcb_,
                                  new Pbuf(1436), 'C'));
    }
    return event;
  }

  void push(Event* event) { events_.push_back(event); }
  void seed(Event* event) { events_.push_back(event); }

 private:
  Client* upload_;
  Pcb* uploadPcb_;
  Client* closing_;
  std::deque<Event*> events_;
  bool injected_ = false;
};

void testLegacyConcurrentPurgeReproducesThreeMssBlockRotation() {
  Pcb* uploadPcb = new Pcb;
  Client upload(uploadPcb);
  Pcb* closingPcb = new Pcb;
  Client closing(closingPcb);
  InterleavingQueue queue;
  queue.seed(new Event(EventType::Rx, &upload, uploadPcb, new Pbuf(1436), 'A'));
  queue.seed(new Event(EventType::Rx, &upload, uploadPcb, new Pbuf(1436), 'B'));
  queue.seed(new Event(EventType::Rx, &upload, uploadPcb, new Pbuf(1436), 'C'));
  queue.seed(new Event(EventType::Poll, &closing, closingPcb));

  const size_t removed = asynctcp_lifecycle::removeQueuedEvents<
      InterleavingQueue, Event>(
      queue, &closing,
      [](Event* event, Client* owner) { return event->owner == owner; },
      [](Event* event) { releaseEvent(event); });
  assert(removed == 1);
  queue.drain();

  const std::vector<uint8_t> expected{'B', 'C', 'A'};
  assert(queue.delivered() == expected);
  delete uploadPcb;
  delete closingPcb;
}

void testSerializedPurgePreservesThreeMssBlockOrder() {
  Queue queue;
  Pcb* uploadPcb = new Pcb;
  Client upload(uploadPcb);
  Pcb* closingPcb = new Pcb;
  Client closing(closingPcb);
  queue.push(new Event(EventType::Rx, &upload, uploadPcb, new Pbuf(1436), 'A'));
  queue.push(new Event(EventType::Rx, &upload, uploadPcb, new Pbuf(1436), 'B'));
  queue.push(new Event(EventType::Rx, &upload, uploadPcb, new Pbuf(1436), 'C'));
  queue.push(new Event(EventType::Poll, &closing, closingPcb));

  assert(removeEventsForClient(queue, &closing) == 1);
  const uint8_t expected[] = {'A', 'B', 'C'};
  for (uint8_t marker : expected) {
    Event* event = queue.pop();
    assert(event != nullptr && event->type == EventType::Rx);
    assert(event->pbuf != nullptr && event->pbuf->len == 1436);
    assert(event->marker == marker);
    releaseEvent(event);
  }
  assert(queue.pop() == nullptr);
  delete uploadPcb;
  delete closingPcb;
}

void testLegacyConcurrentProducerCanOvertakeQueuedRx() {
  Pcb* uploadPcb = new Pcb;
  Client upload(uploadPcb);
  Pcb* closingPcb = new Pcb;
  Client closing(closingPcb);
  ProducerInterleavingQueue queue(&upload, uploadPcb);
  queue.seed(new Event(EventType::Rx, &upload, uploadPcb, new Pbuf(1436), 'A'));
  queue.seed(new Event(EventType::Rx, &upload, uploadPcb, new Pbuf(1436), 'B'));
  queue.seed(new Event(EventType::Rx, &upload, uploadPcb, new Pbuf(1436), 'C'));
  queue.seed(new Event(EventType::Poll, &closing, closingPcb));

  assert((asynctcp_lifecycle::removeQueuedEvents<
              ProducerInterleavingQueue, Event>(
              queue, &closing,
              [](Event* event, Client* owner) { return event->owner == owner; },
              [](Event* event) { releaseEvent(event); }) == 1));
  const uint8_t expected[] = {'A', 'D', 'B', 'C'};
  for (uint8_t marker : expected) {
    Event* event = queue.pop();
    assert(event != nullptr && event->type == EventType::Rx);
    assert(event->pbuf != nullptr && event->pbuf->len == 1436);
    assert(event->marker == marker);
    releaseEvent(event);
  }
  assert(queue.pop() == nullptr);
  delete uploadPcb;
  delete closingPcb;
}

void testLegacyProducerPreemptionReproducesThreeMssBlockRotation() {
  Pcb* uploadPcb = new Pcb;
  Client upload(uploadPcb);
  Pcb* closingPcb = new Pcb;
  Client closing(closingPcb);
  MssRotationProducerQueue queue(&upload, uploadPcb, &closing);
  queue.seed(new Event(EventType::Poll, &closing, closingPcb));
  queue.seed(new Event(EventType::Rx, &upload, uploadPcb,
                       new Pbuf(1436), 'A'));

  assert((asynctcp_lifecycle::removeQueuedEvents<
              MssRotationProducerQueue, Event>(
              queue, &closing,
              [](Event* event, Client* owner) { return event->owner == owner; },
              [](Event* event) { releaseEvent(event); }) == 1));
  const uint8_t expected[] = {'B', 'C', 'A'};
  for (uint8_t marker : expected) {
    Event* event = queue.pop();
    assert(event != nullptr && event->type == EventType::Rx);
    assert(event->pbuf != nullptr && event->pbuf->len == 1436);
    assert(event->marker == marker);
    releaseEvent(event);
  }
  assert(queue.pop() == nullptr);
  delete uploadPcb;
  delete closingPcb;
}

void testSerializedProducerRunsAfterPurge() {
  Queue queue;
  Pcb* uploadPcb = new Pcb;
  Client upload(uploadPcb);
  Pcb* closingPcb = new Pcb;
  Client closing(closingPcb);
  queue.push(new Event(EventType::Rx, &upload, uploadPcb, new Pbuf(1436), 'A'));
  queue.push(new Event(EventType::Rx, &upload, uploadPcb, new Pbuf(1436), 'B'));
  queue.push(new Event(EventType::Rx, &upload, uploadPcb, new Pbuf(1436), 'C'));
  queue.push(new Event(EventType::Poll, &closing, closingPcb));

  assert(removeEventsForClient(queue, &closing) == 1);
  queue.push(new Event(EventType::Rx, &upload, uploadPcb, new Pbuf(1436), 'D'));
  const uint8_t expected[] = {'A', 'B', 'C', 'D'};
  for (uint8_t marker : expected) {
    Event* event = queue.pop();
    assert(event != nullptr && event->marker == marker);
    releaseEvent(event);
  }
  assert(queue.pop() == nullptr);
  delete uploadPcb;
  delete closingPcb;
}

class SerializedBoundedQueue {
 public:
  explicit SerializedBoundedQueue(size_t capacity)
      : capacity_(capacity), availableSlots_(capacity) {}

  void send(Event* event, bool toFront = false) {
    {
      std::unique_lock<std::mutex> slotsLock(slotsMutex_);
      ++waitingProducers_;
      producerStateChanged_.notify_all();
      slotsAvailable_.wait(slotsLock,
                           [this] { return availableSlots_ != 0; });
      --waitingProducers_;
      --availableSlots_;
    }

    {
      std::lock_guard<std::mutex> queueLock(queueMutex_);
      assert(events_.size() < capacity_);
      if (toFront) {
        events_.push_front(event);
      } else {
        events_.push_back(event);
      }
    }
    eventsAvailable_.notify_one();
  }

  Event* receive() {
    Event* event = nullptr;
    {
      std::unique_lock<std::mutex> queueLock(queueMutex_);
      eventsAvailable_.wait(queueLock, [this] { return !events_.empty(); });
      event = events_.front();
      events_.pop_front();
    }
    releaseSlots(1);
    return event;
  }

  size_t remove(Client* owner) {
    size_t removed = 0;
    {
      std::lock_guard<std::mutex> queueLock(queueMutex_);
      QueueView view(events_);
      removed = asynctcp_lifecycle::removeQueuedEvents<QueueView, Event>(
          view, owner,
          [](Event* event, Client* selected) {
            return event->owner == selected;
          },
          [](Event* event) { releaseEvent(event); });
    }
    releaseSlots(removed);
    return removed;
  }

  size_t size() const {
    std::lock_guard<std::mutex> queueLock(queueMutex_);
    return events_.size();
  }

  size_t availableSlots() const {
    std::lock_guard<std::mutex> slotsLock(slotsMutex_);
    return availableSlots_;
  }

  void waitForBlockedProducer() {
    std::unique_lock<std::mutex> slotsLock(slotsMutex_);
    const bool observed = producerStateChanged_.wait_for(
        slotsLock, std::chrono::seconds(2),
        [this] { return waitingProducers_ != 0 && availableSlots_ == 0; });
    assert(observed);
  }

 private:
  class QueueView {
   public:
    explicit QueueView(std::deque<Event*>& events) : events_(events) {}

    size_t count() const { return events_.size(); }

    Event* pop() {
      if (events_.empty()) return nullptr;
      Event* event = events_.front();
      events_.pop_front();
      return event;
    }

    void push(Event* event) { events_.push_back(event); }

   private:
    std::deque<Event*>& events_;
  };

  void releaseSlots(size_t count) {
    if (count == 0) return;
    {
      std::lock_guard<std::mutex> slotsLock(slotsMutex_);
      assert(availableSlots_ + count <= capacity_);
      availableSlots_ += count;
    }
    slotsAvailable_.notify_all();
  }

  const size_t capacity_;
  mutable std::mutex queueMutex_;
  std::condition_variable eventsAvailable_;
  std::deque<Event*> events_;
  mutable std::mutex slotsMutex_;
  std::condition_variable slotsAvailable_;
  std::condition_variable producerStateChanged_;
  size_t availableSlots_;
  size_t waitingProducers_ = 0;
};

void assertFullQueueCleanupUnblocksProducer(EventType controlType) {
  constexpr size_t kCapacity = 32;
  SerializedBoundedQueue queue(kCapacity);
  Pcb* uploadPcb = new Pcb;
  Client upload(uploadPcb);
  Pcb* closingPcb = new Pcb;
  Client closing(closingPcb);

  for (uint8_t marker = 1; marker < kCapacity; ++marker) {
    queue.send(new Event(EventType::Rx, &upload, uploadPcb,
                         new Pbuf(1436), marker));
  }
  queue.send(new Event(controlType, &closing, closingPcb));
  assert(queue.size() == kCapacity && queue.availableSlots() == 0);

  std::atomic<bool> producerCompleted{false};
  std::thread producer([&] {
    queue.send(new Event(EventType::Poll, &upload, uploadPcb), true);
    producerCompleted.store(true);
  });
  queue.waitForBlockedProducer();

  assert(queue.remove(&closing) == 1);
  producer.join();
  assert(producerCompleted.load());
  assert(queue.size() == kCapacity && queue.availableSlots() == 0);

  Event* prepended = queue.receive();
  assert(prepended->type == EventType::Poll && prepended->owner == &upload);
  releaseEvent(prepended);
  for (uint8_t marker = 1; marker < kCapacity; ++marker) {
    Event* event = queue.receive();
    assert(event->type == EventType::Rx && event->marker == marker);
    assert(event->pbuf != nullptr && event->pbuf->len == 1436);
    releaseEvent(event);
  }
  assert(queue.size() == 0 && queue.availableSlots() == kCapacity);
  delete uploadPcb;
  delete closingPcb;
}

void testFullQueueCleanupUnblocksPrependForClearErrorAndFin() {
  assertFullQueueCleanupUnblocksProducer(EventType::Clear);
  assertFullQueueCleanupUnblocksProducer(EventType::Error);
  assertFullQueueCleanupUnblocksProducer(EventType::Fin);
}

void assertConsumerCleanupCompletesWithWaitingProducer(EventType controlType) {
  constexpr size_t kCapacity = 32;
  SerializedBoundedQueue queue(kCapacity);
  Pcb* uploadPcb = new Pcb;
  Client upload(uploadPcb);
  Pcb* closingPcb = new Pcb;
  Client closing(closingPcb);

  queue.send(new Event(controlType, &closing, closingPcb));
  for (uint8_t marker = 1; marker < kCapacity; ++marker) {
    queue.send(new Event(EventType::Rx, &upload, uploadPcb,
                         new Pbuf(1436), marker));
  }
  assert(queue.size() == kCapacity && queue.availableSlots() == 0);

  std::atomic<bool> producerCompleted{false};
  std::thread producer([&] {
    queue.send(new Event(EventType::Rx, &upload, uploadPcb,
                         new Pbuf(1436), kCapacity));
    producerCompleted.store(true);
  });
  queue.waitForBlockedProducer();

  Event* control = queue.receive();
  assert(control->type == controlType && control->owner == &closing);
  assert(queue.remove(&closing) == 0);
  releaseEvent(control);
  producer.join();
  assert(producerCompleted.load());
  assert(queue.size() == kCapacity && queue.availableSlots() == 0);

  for (uint8_t marker = 1; marker <= kCapacity; ++marker) {
    Event* event = queue.receive();
    assert(event->type == EventType::Rx && event->marker == marker);
    assert(event->pbuf != nullptr && event->pbuf->len == 1436);
    releaseEvent(event);
  }
  assert(queue.size() == 0 && queue.availableSlots() == kCapacity);
  delete uploadPcb;
  delete closingPcb;
}

void testClearErrorAndFinConsumerCleanupWithWaitingProducer() {
  assertConsumerCleanupCompletesWithWaitingProducer(EventType::Clear);
  assertConsumerCleanupCompletesWithWaitingProducer(EventType::Error);
  assertConsumerCleanupCompletesWithWaitingProducer(EventType::Fin);
}

void testParallelProducerConsumerPreservesAllRxData() {
  constexpr size_t kCapacity = 32;
  constexpr uint8_t kEventCount = 64;
  SerializedBoundedQueue queue(kCapacity);
  Pcb* pcb = new Pcb;
  Client client(pcb);
  for (uint8_t marker = 0; marker < kCapacity; ++marker) {
    queue.send(new Event(EventType::Rx, &client, pcb,
                         new Pbuf(1436), marker));
  }

  std::vector<uint8_t> received;
  received.reserve(kEventCount);
  std::thread producer([&] {
    for (uint8_t marker = kCapacity; marker < kEventCount; ++marker) {
      queue.send(new Event(EventType::Rx, &client, pcb,
                           new Pbuf(1436), marker));
    }
  });
  std::thread consumer([&] {
    for (uint8_t index = 0; index < kEventCount; ++index) {
      Event* event = queue.receive();
      assert(event->type == EventType::Rx);
      assert(event->pbuf != nullptr && event->pbuf->len == 1436);
      received.push_back(event->marker);
      releaseEvent(event);
    }
  });
  producer.join();
  consumer.join();

  assert(received.size() == kEventCount);
  for (uint8_t marker = 0; marker < kEventCount; ++marker) {
    assert(received[marker] == marker);
  }
  assert(queue.size() == 0 && queue.availableSlots() == kCapacity);
  delete pcb;
}

void testDisconnectCleanupAndReconnectUseDistinctOwnership() {
  SerializedBoundedQueue queue(32);
  Pcb* oldPcb = new Pcb;
  Client oldClient(oldPcb);
  Pcb* survivorPcb = new Pcb;
  Client survivor(survivorPcb);
  for (uint8_t marker = 1; marker <= 3; ++marker) {
    queue.send(new Event(EventType::Rx, &oldClient, oldPcb,
                         new Pbuf(1436), marker));
    queue.send(new Event(EventType::Rx, &survivor, survivorPcb,
                         new Pbuf(1436), static_cast<uint8_t>(10 + marker)));
  }

  oldClient.invalidateFromLwipError();
  assert(queue.remove(&oldClient) == 3);
  delete oldPcb;
  oldPcb = nullptr;

  Pcb* reconnectPcb = new Pcb;
  Client reconnect(reconnectPcb);
  for (uint8_t marker = 21; marker <= 23; ++marker) {
    queue.send(new Event(EventType::Rx, &reconnect, reconnectPcb,
                         new Pbuf(1436), marker));
  }

  const uint8_t expected[] = {11, 12, 13, 21, 22, 23};
  for (uint8_t marker : expected) {
    Event* event = queue.receive();
    assert(event->marker == marker);
    handleEvent(event);
  }
  assert(oldClient.dataCallbacks == 0);
  assert(survivor.dataCallbacks == 3);
  assert(reconnect.dataCallbacks == 3);
  assert(queue.size() == 0 && queue.availableSlots() == 32);
  delete survivorPcb;
  delete reconnectPcb;
}

void testProductionIntegrationContract() {
  const std::string source = readFile("src/AsyncWebServer/AsyncTCP.cpp");
  const size_t errorBegin = source.find("static void _tcp_error");
  const size_t invalidate = source.find("AsyncClient::_s_lwip_error(arg);",
                                        errorBegin);
  const size_t allocation = source.find("malloc(sizeof(lwip_event_packet_t))",
                                         errorBegin);
  assert(errorBegin != std::string::npos);
  assert(invalidate < allocation);
  assert(source.find("_rx_ack_len(0)") != std::string::npos);
  assert(source.find("e->recv.pb = NULL;") != std::string::npos);

  const size_t asyncErrorBegin = source.find("void AsyncClient::_error");
  const size_t asyncErrorEnd = source.find("//In LwIP Thread", asyncErrorBegin);
  const std::string asyncError =
      source.substr(asyncErrorBegin, asyncErrorEnd - asyncErrorBegin);
  assert(asyncError.find("tcp_arg(") == std::string::npos);
  assert(asyncError.find("_pcb->") == std::string::npos);

  const size_t lwipErrorEnd = source.find("static void _tcp_dns_found",
                                          errorBegin);
  const std::string lwipError = source.substr(errorBegin,
                                               lwipErrorEnd - errorBegin);
  assert(lwipError.find("_remove_events_with_arg") == std::string::npos);
  assert(source.find("SemaphoreHandle_t _async_queue_lock") !=
         std::string::npos);
  assert(source.find("SemaphoreHandle_t _async_queue_slots") !=
         std::string::npos);
  assert(source.find("xSemaphoreCreateCounting(XQUEUE_SIZE, XQUEUE_SIZE)") !=
         std::string::npos);
  assert(source.find("configASSERT(released == pdPASS)") !=
         std::string::npos);

  const size_t queueHelperBegin = source.find("static inline bool _queue_async_event");
  const size_t sendWrapperBegin = source.find("static inline bool _send_async_event",
                                               queueHelperBegin);
  assert(queueHelperBegin != std::string::npos);
  assert(sendWrapperBegin != std::string::npos);
  const std::string queueHelper = source.substr(
      queueHelperBegin, sendWrapperBegin - queueHelperBegin);
  const size_t reserveSlot = queueHelper.find(
      "xSemaphoreTake(_async_queue_slots, wait_for_slot)");
  const size_t takeMutationLock = queueHelper.find(
      "xSemaphoreTake(_async_queue_lock, portMAX_DELAY)");
  assert(reserveSlot < takeMutationLock);
  assert(queueHelper.find("xQueueSendToFront(_async_queue, e, 0)") !=
         std::string::npos);
  assert(queueHelper.find("xQueueSend(_async_queue, e, 0)") !=
         std::string::npos);
  assert(queueHelper.find("xQueueSendToFront(_async_queue, e, portMAX_DELAY)") ==
         std::string::npos);

  const size_t queueViewBegin = source.find("class AsyncEventQueueView");
  const size_t removeBegin = source.find("static size_t _remove_events_with_arg",
                                         queueViewBegin);
  const std::string queueView = source.substr(
      queueViewBegin, removeBegin - queueViewBegin);
  assert(queueView.find("xQueueSend(_async_queue, &packet, 0)") !=
         std::string::npos);
  assert(queueView.find("portMAX_DELAY") == std::string::npos);

  const size_t getBegin = source.find("static inline bool _get_async_event");
  const size_t freeBegin = source.find("static void _free_async_event", getBegin);
  const std::string getEvent = source.substr(getBegin, freeBegin - getBegin);
  assert(getEvent.find("_release_async_queue_slot()") !=
         std::string::npos);
}

}  // namespace

int main() {
  testQueuedRxIsPurgedBeforeErrorCallback();
  testRxAlreadyOwnedByAsyncTaskCannotAckFreedPcb();
  testNormalRxAndFin();
  testClosedSlotReservation();
  testLegacyConcurrentPurgeReproducesThreeMssBlockRotation();
  testSerializedPurgePreservesThreeMssBlockOrder();
  testLegacyConcurrentProducerCanOvertakeQueuedRx();
  testLegacyProducerPreemptionReproducesThreeMssBlockRotation();
  testSerializedProducerRunsAfterPurge();
  testFullQueueCleanupUnblocksPrependForClearErrorAndFin();
  testClearErrorAndFinConsumerCleanupWithWaitingProducer();
  testParallelProducerConsumerPreservesAllRxData();
  testDisconnectCleanupAndReconnectUseDistinctOwnership();
  testProductionIntegrationContract();

  assert(Pbuf::created == Pbuf::destroyed);
  assert(Pcb::created == Pcb::destroyed);
  assert(Event::created == Event::destroyed);
  std::puts("PASS asynctcp_lifecycle_native");
  return 0;
}
