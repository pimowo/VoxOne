#ifndef VOXONE_PLAYER_PENDING_REFRESH_H
#define VOXONE_PLAYER_PENDING_REFRESH_H

#include <atomic>
#include <stdint.h>

// A playlist commit publishes that the current station may need a runtime
// reload. The Player task reads the current selection under PlaylistGuard, so
// coalescing never retains a stale station number.
class PlayerPendingRefresh {
public:
  void reset() { pending_.store(0, std::memory_order_relaxed); }

  void publish() {
    pending_.store(1, std::memory_order_release);
  }

  bool take() {
    return pending_.exchange(0, std::memory_order_acq_rel) != 0;
  }

  bool pending() const {
    return pending_.load(std::memory_order_acquire) != 0;
  }

private:
  static_assert(ATOMIC_INT_LOCK_FREE == 2,
                "pending station refresh must be lock-free");
  std::atomic<uint32_t> pending_{0};
};

#endif
