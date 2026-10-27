#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>

// Compile the production AsyncWebLock definition with a bounded host-side
// FreeRTOS semaphore facade.  Skipping ESPAsyncWebServer.h keeps this test
// independent of Arduino while still exercising the actual lock and guard.
#define _ESPAsyncWebServer_H_
#define ESP32 1
#define ESP_ARDUINO_VERSION_VAL(major, minor, patch) \
  ((major) * 10000 + (minor) * 100 + (patch))
#define ESP_ARDUINO_VERSION ESP_ARDUINO_VERSION_VAL(2, 0, 17)

using BaseType_t = int;
using TickType_t = uint32_t;
constexpr BaseType_t pdTRUE = 1;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;

struct FakeSemaphore {
  explicit FakeSemaphore(bool recursiveValue) : recursive(recursiveValue) {}

  bool recursive;
  std::mutex stateMutex;
  std::condition_variable stateChanged;
  unsigned count = 0;
  std::recursive_mutex recursiveMutex;
};

using SemaphoreHandle_t = FakeSemaphore*;

SemaphoreHandle_t xSemaphoreCreateBinary() {
  return new FakeSemaphore(false);
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
  return new FakeSemaphore(true);
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
  assert(semaphore != nullptr && !semaphore->recursive);
  {
    std::lock_guard<std::mutex> lock(semaphore->stateMutex);
    semaphore->count = 1;
  }
  semaphore->stateChanged.notify_one();
  return pdTRUE;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t) {
  assert(semaphore != nullptr && !semaphore->recursive);
  std::unique_lock<std::mutex> lock(semaphore->stateMutex);
  semaphore->stateChanged.wait(lock, [&] { return semaphore->count != 0; });
  --semaphore->count;
  return pdTRUE;
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t semaphore, TickType_t) {
  assert(semaphore != nullptr && semaphore->recursive);
  semaphore->recursiveMutex.lock();
  return pdTRUE;
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t semaphore) {
  assert(semaphore != nullptr && semaphore->recursive);
  semaphore->recursiveMutex.unlock();
  return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
  delete semaphore;
}

#define configASSERT(condition) assert(condition)

// The legacy implementation declared the SMP kernel array as this scalar.
// Keeping the symbol in the fixture makes the test compile against the old
// header as well; the mutual-exclusion assertion then fails deterministically.
void* pxCurrentTCB = reinterpret_cast<void*>(0x1);

#include "../src/AsyncWebServer/AsyncWebSynchronization.h"

namespace {

void testRecursiveEntryBySameTask() {
  AsyncWebLock lock;
  AsyncWebLockGuard outer(lock);
  AsyncWebLockGuard inner(lock);
}

void testDifferentTasksCannotEnterTogether() {
  AsyncWebLock lock;
  std::mutex stateMutex;
  std::condition_variable stateChanged;
  bool firstInside = false;
  bool releaseFirst = false;
  bool secondInside = false;

  std::thread first([&] {
    AsyncWebLockGuard guard(lock);
    std::unique_lock<std::mutex> stateLock(stateMutex);
    firstInside = true;
    stateChanged.notify_all();
    stateChanged.wait(stateLock, [&] { return releaseFirst; });
  });

  {
    std::unique_lock<std::mutex> stateLock(stateMutex);
    stateChanged.wait(stateLock, [&] { return firstInside; });
  }

  std::thread second([&] {
    AsyncWebLockGuard guard(lock);
    {
      std::lock_guard<std::mutex> stateLock(stateMutex);
      secondInside = true;
    }
    stateChanged.notify_all();
  });

  bool overlapped = false;
  {
    std::unique_lock<std::mutex> stateLock(stateMutex);
    overlapped = stateChanged.wait_for(
        stateLock, std::chrono::milliseconds(100),
        [&] { return secondInside; });
    releaseFirst = true;
  }
  stateChanged.notify_all();

  first.join();
  second.join();
  assert(!overlapped);
  assert(secondInside);
}

}  // namespace

int main() {
  testRecursiveEntryBySameTask();
  testDifferentTasksCannotEnterTogether();
  std::puts("PASS asyncweb_lock_native");
  return 0;
}
