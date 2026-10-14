#ifndef VOXONE_PLAYER_PENDING_REFRESH_H
#define VOXONE_PLAYER_PENDING_REFRESH_H

#include <atomic>
#include <stdint.h>

struct PlayerPendingRefreshRequest {
  constexpr PlayerPendingRefreshRequest(bool isPending = false,
                                        bool resetOvol = false)
      : pending(isPending), resetOvolIfEmpty(resetOvol) {}

  bool pending;
  bool resetOvolIfEmpty;

  explicit operator bool() const { return pending; }
};

// A playlist commit publishes that the current station may need runtime
// reconciliation. The Player task reads the current selection under
// PlaylistGuard, so coalescing never retains a stale station number or URL.
class PlayerPendingRefresh {
public:
  void reset() { pending_.store(0, std::memory_order_relaxed); }

  void publish(bool resetOvolIfEmpty = false) {
    uint32_t flags = kPending;
    if (resetOvolIfEmpty) flags |= kResetOvolIfEmpty;
    pending_.store(flags, std::memory_order_release);
  }

  // A legacy refresh carries no newer playlist transaction semantics, so it
  // must not erase reset-OVOL requested by an already published import.
  void publishRefresh() {
    pending_.fetch_or(kPending, std::memory_order_release);
  }

  void republish(PlayerPendingRefreshRequest request) {
    uint32_t flags = kPending;
    if (request.resetOvolIfEmpty) flags |= kResetOvolIfEmpty;
    pending_.fetch_or(flags, std::memory_order_release);
  }

  PlayerPendingRefreshRequest take() {
    const uint32_t flags = pending_.exchange(0, std::memory_order_acq_rel);
    return {(flags & kPending) != 0,
            (flags & kResetOvolIfEmpty) != 0};
  }

  bool pending() const {
    return pending_.load(std::memory_order_acquire) != 0;
  }

private:
  static constexpr uint32_t kPending = 1U << 0;
  static constexpr uint32_t kResetOvolIfEmpty = 1U << 1;
  static_assert(ATOMIC_INT_LOCK_FREE == 2,
                "pending station refresh must be lock-free");
  std::atomic<uint32_t> pending_{0};
};

#endif
