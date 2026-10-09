#ifndef VOXONE_UPDATE_PROGRESS_H
#define VOXONE_UPDATE_PROGRESS_H

#include <stdint.h>
#include <string.h>

enum class UpdateTarget : uint8_t {
  None, VoxOneFirmware, Filesystem, VoxOneBtFirmware
};

enum class UpdatePhase : uint8_t {
  Idle, Receiving, Validating, Preparing, Writing, Sending, Verifying,
  Restarting, HealthCheck, Success, Error, Aborted
};

struct UpdateProgressSnapshot {
  UpdateTarget target = UpdateTarget::None;
  UpdatePhase phase = UpdatePhase::Idle;
  bool active = false;
  bool locked = false;
  bool progressKnown = false;
  uint32_t doneBytes = 0;
  uint32_t totalBytes = 0;
  uint8_t percent = 0;
  uint16_t errorCode = 0;
  uint32_t revision = 0;
  uint32_t acquisition = 0;
  char status[80]{};
};

// Pure state machine; firmware serializes access in update_progress.cpp.
class UpdateProgressState {
public:
  bool begin(UpdateTarget target, UpdatePhase phase, uint32_t total = 0) {
    if (state_.locked || target == UpdateTarget::None) return false;
    const uint32_t revision = state_.revision + 1;
    const uint32_t acquisition = state_.acquisition + 1;
    state_ = UpdateProgressSnapshot{};
    state_.revision = revision;
    state_.acquisition = acquisition;
    state_.target = target;
    state_.phase = phase;
    state_.active = state_.locked = true;
    state_.totalBytes = total;
    if (phase == UpdatePhase::Receiving || phase == UpdatePhase::Writing ||
        phase == UpdatePhase::Sending) progress(0, total);
    return true;
  }

  void phase(UpdatePhase phase) {
    if (!state_.locked || !state_.active) return;
    if (state_.phase == phase) return;
    state_.phase = phase;
    if (phase != UpdatePhase::Receiving && phase != UpdatePhase::Writing &&
        phase != UpdatePhase::Sending) {
      state_.progressKnown = false;
      state_.percent = 0;
    }
    ++state_.revision;
  }

  void progress(uint32_t done, uint32_t total) {
    if (!state_.locked || !state_.active) return;
    if (state_.phase != UpdatePhase::Receiving &&
        state_.phase != UpdatePhase::Writing &&
        state_.phase != UpdatePhase::Sending) return;
    if (state_.doneBytes == done && state_.totalBytes == total &&
        state_.progressKnown == (total != 0 && done <= total)) return;
    state_.doneBytes = done;
    state_.totalBytes = total;
    state_.progressKnown = total != 0 && done <= total;
    state_.percent = state_.progressKnown
        ? static_cast<uint8_t>((static_cast<uint64_t>(done) * 100u) / total) : 0;
    ++state_.revision;
  }

  void terminal(UpdatePhase phase, uint16_t errorCode = 0,
                const char* status = nullptr) {
    if (!state_.locked || !state_.active) return;
    if (phase != UpdatePhase::Success && phase != UpdatePhase::Error &&
        phase != UpdatePhase::Aborted) return;
    state_.phase = phase;
    state_.active = false;
    // Every successful target remains exclusive until the MAIN boots again.
    state_.locked = phase == UpdatePhase::Success;
    state_.progressKnown = false;
    state_.percent = 0;
    state_.errorCode = errorCode;
    if (status) {
      strncpy(state_.status, status, sizeof(state_.status) - 1);
      state_.status[sizeof(state_.status) - 1] = '\0';
    }
    ++state_.revision;
  }

  // MAIN/FS already schedule reboot; BT will do so in a later stage.
  void restarting() {
    if (!state_.locked) return;
    state_.phase = UpdatePhase::Restarting;
    state_.progressKnown = false;
    state_.percent = 0;
    ++state_.revision;
  }

  UpdateProgressSnapshot snapshot() const { return state_; }
  bool locked() const { return state_.locked; }

private:
  UpdateProgressSnapshot state_{};
};

// Thread-safe global runtime API for AsyncTCP and the MAIN loop.
bool beginUpdate(UpdateTarget target, UpdatePhase phase, uint32_t total = 0);
void setUpdatePhase(UpdateTarget target, UpdatePhase phase);
void setUpdateProgress(UpdateTarget target, uint32_t done, uint32_t total);
void finishUpdate(UpdateTarget target, UpdatePhase phase,
                  uint16_t errorCode = 0, const char* status = nullptr);
void restartingUpdate(UpdateTarget target);
UpdateProgressSnapshot updateProgress();
bool updateLockActive();

#endif
