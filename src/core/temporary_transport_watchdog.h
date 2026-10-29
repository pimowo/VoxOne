#ifndef VOXONE_TEMPORARY_TRANSPORT_WATCHDOG_H
#define VOXONE_TEMPORARY_TRANSPORT_WATCHDOG_H

#include <stdint.h>

#include "radio_reconnect_policy.h"
#include "temporary_audio_state.h"

namespace voxone {
namespace temporary_audio {

// Temporary URLs use the established transport inactivity contract. This is
// not an absolute announcement duration: every received byte starts a fresh
// window, and complete locally buffered frames suppress expiry until drained.
constexpr uint32_t kInactivityTimeoutMs =
    voxone::radio::kStreamInactivityTimeoutMs;

class ProgressWatchdog {
 public:
  void begin(uint32_t token, uint32_t nowMs, uint64_t progressBytes) {
    token_ = token;
    lastProgressMs_ = nowMs;
    progressBytes_ = progressBytes;
  }

  void finish(uint32_t token) {
    if (token == token_) token_ = 0;
  }

  bool expired(uint32_t token, uint32_t nowMs, uint64_t progressBytes,
               bool connected, bool bufferedFrameAvailable) {
    if (!token || token != token_) return false;
    if (progressBytes != progressBytes_) {
      progressBytes_ = progressBytes;
      lastProgressMs_ = nowMs;
      return false;
    }
    if (bufferedFrameAvailable) return false;
    return !connected ||
           static_cast<uint32_t>(nowMs - lastProgressMs_) >=
               kInactivityTimeoutMs;
  }

  uint32_t token() const { return token_; }
  uint32_t lastProgressMs() const { return lastProgressMs_; }
  uint64_t progressBytes() const { return progressBytes_; }

 private:
  uint32_t token_ = 0;
  uint32_t lastProgressMs_ = 0;
  uint64_t progressBytes_ = 0;
};

// The production timeout-to-cleanup dispatcher used from Player::loop().
// An already observed backend EOF/error wins; otherwise timeout signals the
// current generation before requesting the same one-shot cleanup path.
template <typename TimeoutCallback, typename FinishCallback>
bool serviceTemporaryAudioTransport(TemporaryAudioState& temporary,
                                    ProgressWatchdog& watchdog,
                                    uint32_t nowMs,
                                    uint64_t progressBytes,
                                    bool backendRunning,
                                    bool connected,
                                    bool bufferedFrameAvailable,
                                    TimeoutCallback onTimeout,
                                    FinishCallback finish) {
  const uint32_t token = temporary.token();
  if (!token) return false;
  if (backendRunning && !temporary.terminal() &&
      watchdog.expired(token, nowMs, progressBytes, connected,
                       bufferedFrameAvailable) &&
      temporary.signal(token)) {
    onTimeout(token);
  }
  if (backendRunning && !temporary.terminal()) return false;
  finish();
  return true;
}

}  // namespace temporary_audio
}  // namespace voxone

#endif
