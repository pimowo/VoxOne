#include "http_heap_trace.h"

#if defined(VOXONE_HEAP_TRACE)

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" int ets_printf(const char* format, ...);

namespace voxone {
namespace http_heap_trace {
namespace {

constexpr size_t kSlotCount = 16;
constexpr size_t kMethodCapacity = 8;
constexpr size_t kRouteCapacity = 56;
constexpr size_t kStageCapacity = 40;
constexpr uint32_t kHeapCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

struct Slot {
  const void* request = nullptr;
  const void* client = nullptr;
  uint32_t requestId = 0;
  uint32_t connectionId = 0;
  uint32_t requestReuseFrom = 0;
  uint32_t clientReuseFrom = 0;
  uint32_t createdMs = 0;
  uint32_t lastSequence = 0;
  uint16_t peerPort = 0;
  bool active = false;
  bool requestAlive = false;
  uint8_t peer[4]{};
  char method[kMethodCapacity]{};
  char route[kRouteCapacity]{};
  char stage[kStageCapacity]{};
};

struct Snapshot {
  const void* request = nullptr;
  const void* client = nullptr;
  uint32_t requestId = 0;
  uint32_t connectionId = 0;
  uint32_t requestReuseFrom = 0;
  uint32_t clientReuseFrom = 0;
  uint32_t createdMs = 0;
  uint32_t sequence = 0;
  uint16_t peerPort = 0;
  bool found = false;
  uint8_t peer[4]{};
  char method[kMethodCapacity]{};
  char route[kRouteCapacity]{};
  char stage[kStageCapacity]{};
};

portMUX_TYPE traceMux = portMUX_INITIALIZER_UNLOCKED;
Slot slots[kSlotCount]{};
uint32_t nextRequestId = 1;
uint32_t nextConnectionId = 1;
uint32_t nextSequence = 1;
bool firstFailureLatched = false;
bool emitterBusy = false;

uint32_t monotonicMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

uint32_t takeCounter(uint32_t& counter) {
  const uint32_t result = counter++;
  if (counter == 0) counter = 1;
  return result;
}

void copyBounded(char* output, size_t capacity, const char* input,
                 const char* fallback) {
  if (!output || capacity == 0) return;
  const char* source = input && input[0] ? input : fallback;
  size_t index = 0;
  while (source && source[index] && index + 1 < capacity) {
    const unsigned char value = static_cast<unsigned char>(source[index]);
    output[index] = value >= 0x20 && value <= 0x7e
                        ? static_cast<char>(value)
                        : '?';
    ++index;
  }
  output[index] = '\0';
}

Slot* findRequestLocked(const void* request) {
  for (Slot& slot : slots)
    if (slot.active && slot.request == request) return &slot;
  return nullptr;
}

Slot* findClientLocked(const void* client) {
  for (Slot& slot : slots)
    if (slot.active && slot.client == client) return &slot;
  return nullptr;
}

uint32_t previousRequestIdLocked(const void* request) {
  uint32_t previous = 0;
  for (const Slot& slot : slots)
    if (!slot.active && slot.request == request && slot.requestId > previous)
      previous = slot.requestId;
  return previous;
}

uint32_t previousClientIdLocked(const void* client) {
  uint32_t previous = 0;
  for (const Slot& slot : slots)
    if (!slot.active && slot.client == client && slot.requestId > previous)
      previous = slot.requestId;
  return previous;
}

Slot* replacementSlotLocked() {
  Slot* oldest = nullptr;
  for (Slot& slot : slots) {
    if (slot.active) continue;
    if (!oldest || slot.lastSequence < oldest->lastSequence) oldest = &slot;
  }
  return oldest;
}

Snapshot snapshotLocked(Slot* slot, const char* stage) {
  Snapshot result{};
  result.sequence = takeCounter(nextSequence);
  if (!slot) {
    copyBounded(result.method, sizeof(result.method), "?", "?");
    copyBounded(result.route, sizeof(result.route), "<untracked>",
                "<untracked>");
    copyBounded(result.stage, sizeof(result.stage), stage, "unknown");
    return result;
  }
  copyBounded(slot->stage, sizeof(slot->stage), stage, "unknown");
  slot->lastSequence = result.sequence;
  result.request = slot->request;
  result.client = slot->client;
  result.requestId = slot->requestId;
  result.connectionId = slot->connectionId;
  result.requestReuseFrom = slot->requestReuseFrom;
  result.clientReuseFrom = slot->clientReuseFrom;
  result.createdMs = slot->createdMs;
  result.peerPort = slot->peerPort;
  std::memcpy(result.peer, slot->peer, sizeof(result.peer));
  result.found = true;
  copyBounded(result.method, sizeof(result.method), slot->method, "?");
  copyBounded(result.route, sizeof(result.route), slot->route, "<pending>");
  copyBounded(result.stage, sizeof(result.stage), slot->stage, "unknown");
  return result;
}

bool beginEmit() {
  bool acquired = false;
  portENTER_CRITICAL(&traceMux);
  if (!emitterBusy) {
    emitterBusy = true;
    acquired = true;
  }
  portEXIT_CRITICAL(&traceMux);
  return acquired;
}

void endEmit() {
  portENTER_CRITICAL(&traceMux);
  emitterBusy = false;
  portEXIT_CRITICAL(&traceMux);
}

void emit(const Snapshot& snapshot, const char* integrity, size_t value1,
          size_t value2, size_t value3) {
  if (!beginEmit()) return;
  const uint32_t now = monotonicMs();
  const uint32_t age = snapshot.found ? now - snapshot.createdMs : 0;
  ets_printf(
      "##[HEAP-TRACE]# seq=%lu ms=%lu req=%lu conn=%lu reqp=%p client=%p "
      "peer=%u.%u.%u.%u:%u reuseReq=%lu reuseClient=%lu method=%s route=%s "
      "stage=%s ageMs=%lu v1=%u v2=%u v3=%u heap=%s free=%u largest=%u\n",
      static_cast<unsigned long>(snapshot.sequence),
      static_cast<unsigned long>(now),
      static_cast<unsigned long>(snapshot.requestId),
      static_cast<unsigned long>(snapshot.connectionId), snapshot.request,
      snapshot.client, static_cast<unsigned>(snapshot.peer[0]),
      static_cast<unsigned>(snapshot.peer[1]),
      static_cast<unsigned>(snapshot.peer[2]),
      static_cast<unsigned>(snapshot.peer[3]),
      static_cast<unsigned>(snapshot.peerPort),
      static_cast<unsigned long>(snapshot.requestReuseFrom),
      static_cast<unsigned long>(snapshot.clientReuseFrom), snapshot.method,
      snapshot.route, snapshot.stage, static_cast<unsigned long>(age),
      static_cast<unsigned>(value1), static_cast<unsigned>(value2),
      static_cast<unsigned>(value3), integrity,
      static_cast<unsigned>(heap_caps_get_free_size(kHeapCaps)),
      static_cast<unsigned>(heap_caps_get_largest_free_block(kHeapCaps)));
  endEmit();
}

void emitFirstFailure(const Snapshot& snapshot) {
  // Do not route the first-failure marker through the best-effort emitter
  // guard: a concurrent normal trace line must never make this evidence
  // disappear. ets_printf is allocation-free; at worst two bounded lines can
  // interleave, while the unique marker and request context are still emitted.
  const uint32_t now = monotonicMs();
  ets_printf(
      "##[HEAP-TRACE-FIRST]# seq=%lu ms=%lu req=%lu conn=%lu reqp=%p "
      "client=%p peer=%u.%u.%u.%u:%u method=%s route=%s stage=%s heap=BAD\n",
      static_cast<unsigned long>(snapshot.sequence),
      static_cast<unsigned long>(now),
      static_cast<unsigned long>(snapshot.requestId),
      static_cast<unsigned long>(snapshot.connectionId), snapshot.request,
      snapshot.client, static_cast<unsigned>(snapshot.peer[0]),
      static_cast<unsigned>(snapshot.peer[1]),
      static_cast<unsigned>(snapshot.peer[2]),
      static_cast<unsigned>(snapshot.peer[3]),
      static_cast<unsigned>(snapshot.peerPort), snapshot.method,
      snapshot.route, snapshot.stage);
}

void record(Snapshot snapshot, bool checkIntegrity, size_t value1,
            size_t value2, size_t value3) {
  bool alreadyFailed = false;
  portENTER_CRITICAL(&traceMux);
  alreadyFailed = firstFailureLatched;
  portEXIT_CRITICAL(&traceMux);

  bool intact = true;
  if (checkIntegrity && !alreadyFailed) {
    intact = heap_caps_check_integrity(kHeapCaps, false);
    if (!intact) {
      bool first = false;
      portENTER_CRITICAL(&traceMux);
      if (!firstFailureLatched) {
        firstFailureLatched = true;
        first = true;
      }
      portEXIT_CRITICAL(&traceMux);
      if (first) {
        // Preserve correlation before asking the allocator for its detailed
        // diagnostic; the latter may immediately expose a fatal condition.
        emitFirstFailure(snapshot);
        heap_caps_check_integrity(kHeapCaps, true);
      } else {
        alreadyFailed = true;
      }
    }
  }
  emit(snapshot,
       alreadyFailed ? "LATCHED" : (checkIntegrity ? (intact ? "OK" : "BAD")
                                                    : "SKIP"),
       value1, value2, value3);
}

Snapshot requestSnapshot(const void* request, const char* stage) {
  portENTER_CRITICAL(&traceMux);
  Snapshot result = snapshotLocked(findRequestLocked(request), stage);
  portEXIT_CRITICAL(&traceMux);
  return result;
}

Snapshot clientSnapshot(const void* client, const char* stage) {
  portENTER_CRITICAL(&traceMux);
  Snapshot result = snapshotLocked(findClientLocked(client), stage);
  portEXIT_CRITICAL(&traceMux);
  return result;
}

}  // namespace

void requestCreated(const void* request, const void* client, uint8_t peer0,
                    uint8_t peer1, uint8_t peer2, uint8_t peer3,
                    uint16_t peerPort) {
  Snapshot snapshot{};
  const uint32_t createdMs = monotonicMs();
  portENTER_CRITICAL(&traceMux);
  Slot* slot = replacementSlotLocked();
  if (slot) {
    const uint32_t requestReuse = previousRequestIdLocked(request);
    const uint32_t clientReuse = previousClientIdLocked(client);
    *slot = Slot{};
    slot->request = request;
    slot->client = client;
    slot->requestId = takeCounter(nextRequestId);
    slot->connectionId = takeCounter(nextConnectionId);
    slot->requestReuseFrom = requestReuse;
    slot->clientReuseFrom = clientReuse;
    slot->createdMs = createdMs;
    slot->peerPort = peerPort;
    slot->peer[0] = peer0;
    slot->peer[1] = peer1;
    slot->peer[2] = peer2;
    slot->peer[3] = peer3;
    slot->active = true;
    slot->requestAlive = true;
    copyBounded(slot->method, sizeof(slot->method), "?", "?");
    copyBounded(slot->route, sizeof(slot->route), "<pending>", "<pending>");
    snapshot = snapshotLocked(slot, "connection_open");
  } else {
    snapshot.sequence = takeCounter(nextSequence);
  }
  portEXIT_CRITICAL(&traceMux);
  record(snapshot, true, 0, 0, 0);
}

void requestRouteKnown(const void* request, const char* method,
                       const char* route) {
  Snapshot snapshot{};
  portENTER_CRITICAL(&traceMux);
  Slot* slot = findRequestLocked(request);
  if (slot) {
    copyBounded(slot->method, sizeof(slot->method), method, "?");
    copyBounded(slot->route, sizeof(slot->route), route, "<unknown>");
  }
  snapshot = snapshotLocked(slot, "route_known");
  portEXIT_CRITICAL(&traceMux);
  record(snapshot, true, 0, 0, 0);
}

void requestPoint(const void* request, const char* stage, bool checkIntegrity,
                  size_t value1, size_t value2, size_t value3) {
  record(requestSnapshot(request, stage), checkIntegrity, value1, value2,
         value3);
}

void clientPoint(const void* client, const char* stage, bool checkIntegrity,
                 size_t value1, size_t value2, size_t value3) {
  record(clientSnapshot(client, stage), checkIntegrity, value1, value2,
         value3);
}

void requestReleased(const void* request) {
  Snapshot snapshot{};
  portENTER_CRITICAL(&traceMux);
  Slot* slot = findRequestLocked(request);
  snapshot = snapshotLocked(slot, "request_destructor_body_end");
  if (slot) slot->requestAlive = false;
  portEXIT_CRITICAL(&traceMux);
  record(snapshot, true, 0, 0, 0);
}

void clientReleased(const void* client) {
  Snapshot snapshot{};
  portENTER_CRITICAL(&traceMux);
  Slot* slot = findClientLocked(client);
  snapshot = snapshotLocked(slot, "client_delete_returned");
  if (slot) slot->active = false;
  portEXIT_CRITICAL(&traceMux);
  record(snapshot, true, 0, 0, 0);
}

}  // namespace http_heap_trace
}  // namespace voxone

#endif  // VOXONE_HEAP_TRACE
