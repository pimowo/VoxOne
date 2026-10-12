#pragma once

#include <atomic>
#include <stdint.h>

namespace voxone {
namespace radio {

enum class ConnectAttemptState : uint8_t {
  Idle = 0,
  Connecting,
  CancelRequested,
  FinalizingSuccess,
  FinalizingFailure,
  FinalizingCanceled,
  Succeeded,
  Failed,
  Canceled,
};

enum class ConnectCompletion : uint8_t {
  Accepted = 0,
  Rejected,
  Invalid,
};

class ConnectAttemptLifetime {
 public:
  bool begin() {
    uint8_t current = state_.load(std::memory_order_acquire);
    while (!isActiveValue(current)) {
      if (state_.compare_exchange_weak(
              current, value(ConnectAttemptState::Connecting),
              std::memory_order_acq_rel, std::memory_order_acquire))
        return true;
    }
    return false;
  }

  bool requestCancel() {
    uint8_t current = state_.load(std::memory_order_acquire);
    for (;;) {
      uint8_t canceled = 0;
      if (current == value(ConnectAttemptState::Connecting))
        canceled = value(ConnectAttemptState::CancelRequested);
      else if (current == value(ConnectAttemptState::FinalizingSuccess) ||
               current == value(ConnectAttemptState::FinalizingFailure))
        canceled = value(ConnectAttemptState::FinalizingCanceled);
      else
        return current == value(ConnectAttemptState::CancelRequested) ||
               current == value(ConnectAttemptState::FinalizingCanceled);
      if (state_.compare_exchange_weak(current, canceled,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire))
        return true;
    }
  }

  template <typename Cleanup, typename Reject>
  ConnectCompletion complete(bool connected, Cleanup cleanup, Reject reject) {
    uint8_t current = state_.load(std::memory_order_acquire);
    uint8_t finalizing = 0;
    for (;;) {
      if (current == value(ConnectAttemptState::Connecting)) {
        finalizing = value(connected
                               ? ConnectAttemptState::FinalizingSuccess
                               : ConnectAttemptState::FinalizingFailure);
      } else if (current == value(ConnectAttemptState::CancelRequested)) {
        finalizing = value(ConnectAttemptState::FinalizingCanceled);
      } else {
        return ConnectCompletion::Invalid;
      }
      if (state_.compare_exchange_weak(current, finalizing,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire))
        break;
    }

    cleanup();

    current = state_.load(std::memory_order_acquire);
    for (;;) {
      if (current == value(ConnectAttemptState::FinalizingCanceled)) {
        reject();
        state_.store(value(ConnectAttemptState::Canceled),
                     std::memory_order_release);
        return ConnectCompletion::Rejected;
      }
      ConnectAttemptState terminal;
      ConnectCompletion published;
      if (current == value(ConnectAttemptState::FinalizingSuccess)) {
        terminal = ConnectAttemptState::Succeeded;
        published = ConnectCompletion::Accepted;
      } else if (current == value(ConnectAttemptState::FinalizingFailure)) {
        terminal = ConnectAttemptState::Failed;
        published = ConnectCompletion::Rejected;
      } else {
        return ConnectCompletion::Invalid;
      }
      if (state_.compare_exchange_weak(current, value(terminal),
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire))
        return published;
    }
  }

  ConnectAttemptState state() const {
    return static_cast<ConnectAttemptState>(
        state_.load(std::memory_order_acquire));
  }

  bool active() const {
    return isActiveValue(state_.load(std::memory_order_acquire));
  }

  bool cancellationRequested() const {
    const auto current = state();
    return current == ConnectAttemptState::CancelRequested ||
           current == ConnectAttemptState::FinalizingCanceled;
  }

  bool finalizing() const {
    const auto current = state();
    return current == ConnectAttemptState::FinalizingSuccess ||
           current == ConnectAttemptState::FinalizingFailure ||
           current == ConnectAttemptState::FinalizingCanceled;
  }

  bool succeeded() const {
    return state() == ConnectAttemptState::Succeeded;
  }

 private:
  static constexpr uint8_t value(ConnectAttemptState state) {
    return static_cast<uint8_t>(state);
  }

  static bool isActiveValue(uint8_t state) {
    return state == value(ConnectAttemptState::Connecting) ||
           state == value(ConnectAttemptState::CancelRequested) ||
           state == value(ConnectAttemptState::FinalizingSuccess) ||
           state == value(ConnectAttemptState::FinalizingFailure) ||
           state == value(ConnectAttemptState::FinalizingCanceled);
  }

  std::atomic<uint8_t> state_{value(ConnectAttemptState::Idle)};
};

inline bool connectWaitExpired(uint32_t startedAt, uint32_t now,
                               uint32_t timeoutMs) {
  return static_cast<uint32_t>(now - startedAt) > timeoutMs;
}

}  // namespace radio
}  // namespace voxone
