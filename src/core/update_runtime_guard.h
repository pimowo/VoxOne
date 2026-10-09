#ifndef VOXONE_UPDATE_RUNTIME_GUARD_H
#define VOXONE_UPDATE_RUNTIME_GUARD_H

#include <atomic>
#include "update_progress.h"

// Identifies each successful lock acquisition, regardless of intervening
// phase/progress revisions or a fast failure followed by a new attempt.
class UpdateRuntimeGuard {
public:
  bool needsQuiesce(const UpdateProgressSnapshot& snapshot) {
    if (!snapshot.locked || !snapshot.active || snapshot.acquisition == 0) return false;
    uint32_t previous = handled_.load();
    while (previous < snapshot.acquisition) {
      if (handled_.compare_exchange_weak(previous, snapshot.acquisition)) return true;
    }
    return false;
  }

private:
  std::atomic<uint32_t> handled_{0};
};

#endif
