#ifndef VOXONE_UPDATE_RESTART_COORDINATOR_H
#define VOXONE_UPDATE_RESTART_COORDINATOR_H

#include "update_progress.h"

// Driven only by the MAIN loop; the upload callback merely publishes Success.
class UpdateRestartCoordinator {
public:
  static constexpr uint32_t CompletedHoldMs = 1200;
  static constexpr uint32_t PreparingHoldMs = 400;
  static constexpr uint32_t NetworkQuietMs = 100;
  static constexpr uint32_t ShutdownTimeoutMs = 5000;

  enum class Action : uint8_t {
    None, BeginNetworkQuiesce, Restart, RestartAfterTimeout
  };

  Action tick(const UpdateProgressSnapshot& snapshot, uint32_t now,
              bool networkReady = false) {
    if (stage_ != Stage::Idle &&
        (!snapshot.locked || snapshot.acquisition != acquisition_)) {
      stage_ = Stage::Idle;
      readyObserved_ = false;
    }
    if (stage_ == Stage::Idle) {
      if (snapshot.locked && snapshot.phase == UpdatePhase::Success) {
        acquisition_ = snapshot.acquisition;
        started_ = now;
        stage_ = Stage::Completed;
      }
      return Action::None;
    }
    if (stage_ == Stage::Completed &&
        static_cast<uint32_t>(now - started_) >= CompletedHoldMs) {
      started_ = now;
      stage_ = Stage::Preparing;
      readyObserved_ = false;
      return Action::BeginNetworkQuiesce;
    }
    if (stage_ == Stage::Preparing) {
      if (networkReady && !readyObserved_) {
        readyObserved_ = true;
        readySince_ = now;
      } else if (!networkReady) {
        readyObserved_ = false;
      }
      const uint32_t elapsed = static_cast<uint32_t>(now - started_);
      if (elapsed >= PreparingHoldMs && readyObserved_ &&
          static_cast<uint32_t>(now - readySince_) >= NetworkQuietMs) {
        stage_ = Stage::Done;
        return Action::Restart;
      }
      if (elapsed >= ShutdownTimeoutMs) {
        stage_ = Stage::Done;
        return Action::RestartAfterTimeout;
      }
    }
    return Action::None;
  }

private:
  enum class Stage : uint8_t { Idle, Completed, Preparing, Done };
  Stage stage_ = Stage::Idle;
  uint32_t acquisition_ = 0;
  uint32_t started_ = 0;
  uint32_t readySince_ = 0;
  bool readyObserved_ = false;
};

#endif
