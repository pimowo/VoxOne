#include "i2s_output_lock.h"

#include <freertos/semphr.h>

namespace voxone::i2s_output {
namespace {

SemaphoreHandle_t outputMutex = nullptr;

}  // namespace

bool begin() {
  if (!outputMutex) outputMutex = xSemaphoreCreateRecursiveMutex();
  return outputMutex != nullptr;
}

bool take(TickType_t waitTicks) {
  return outputMutex &&
         xSemaphoreTakeRecursive(outputMutex, waitTicks) == pdTRUE;
}

void give() {
  if (outputMutex) xSemaphoreGiveRecursive(outputMutex);
}

}  // namespace voxone::i2s_output
