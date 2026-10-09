#include "update_progress.h"

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

namespace {
portMUX_TYPE updateMux = portMUX_INITIALIZER_UNLOCKED;
UpdateProgressState updateState;

bool matches(UpdateTarget target) {
  const auto snapshot = updateState.snapshot();
  return snapshot.locked && snapshot.target == target;
}
}

bool beginUpdate(UpdateTarget target, UpdatePhase phase, uint32_t total) {
  portENTER_CRITICAL(&updateMux);
  const bool acquired = updateState.begin(target, phase, total);
  portEXIT_CRITICAL(&updateMux);
  return acquired;
}

void setUpdatePhase(UpdateTarget target, UpdatePhase phase) {
  portENTER_CRITICAL(&updateMux);
  if (matches(target)) updateState.phase(phase);
  portEXIT_CRITICAL(&updateMux);
}

void setUpdateActivity(UpdateTarget target, UpdateActivity activity) {
  portENTER_CRITICAL(&updateMux);
  if (matches(target)) updateState.activity(activity);
  portEXIT_CRITICAL(&updateMux);
}

void setUpdateProgress(UpdateTarget target, uint32_t done, uint32_t total) {
  portENTER_CRITICAL(&updateMux);
  if (matches(target)) updateState.progress(done, total);
  portEXIT_CRITICAL(&updateMux);
}

void finishUpdate(UpdateTarget target, UpdatePhase phase, uint16_t errorCode,
                  const char* status) {
  portENTER_CRITICAL(&updateMux);
  if (matches(target)) updateState.terminal(phase, errorCode, status);
  portEXIT_CRITICAL(&updateMux);
}

void restartingUpdate(UpdateTarget target) {
  portENTER_CRITICAL(&updateMux);
  if (matches(target)) updateState.restarting();
  portEXIT_CRITICAL(&updateMux);
}

UpdateProgressSnapshot updateProgress() {
  portENTER_CRITICAL(&updateMux);
  const auto snapshot = updateState.snapshot();
  portEXIT_CRITICAL(&updateMux);
  return snapshot;
}

bool updateLockActive() {
  portENTER_CRITICAL(&updateMux);
  const bool locked = updateState.locked();
  portEXIT_CRITICAL(&updateMux);
  return locked;
}
