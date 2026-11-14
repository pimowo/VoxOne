#ifndef VOXONE_SOURCE_MEDIA_WATCHDOG_H
#define VOXONE_SOURCE_MEDIA_WATCHDOG_H

#include <stdint.h>

#include "radio_reconnect_policy.h"
#include "source_media_state.h"

namespace voxone {
namespace source_media {

// Source media uses the established transport inactivity contract. This is
// not an absolute playback duration: every received byte starts a fresh
// window, and complete locally buffered frames suppress expiry until drained.
constexpr uint32_t kInactivityTimeoutMs =
    voxone::radio::kStreamInactivityTimeoutMs;

enum class ProgressWatchdogResult : uint8_t {
  None = 0,
  Timeout,
  Disconnected,
};

class ProgressWatchdog {
 public:
  void begin(uint32_t token, uint32_t nowMs, uint64_t progressBytes) {
    token_ = token;
    lastProgressMs_ = nowMs;
    progressBytes_ = progressBytes;
    bufferedBytes_ = 0;
    bodyCompleteObserved_ = false;
    disconnectedObserved_ = false;
  }

  void finish(uint32_t token) {
    if (token == token_) {
      token_ = 0;
      bufferedBytes_ = 0;
      bodyCompleteObserved_ = false;
      disconnectedObserved_ = false;
    }
  }

  ProgressWatchdogResult evaluate(uint32_t token, uint32_t nowMs,
                                  uint64_t progressBytes, bool connected,
                                  bool bodyComplete, uint32_t bufferedBytes,
                                  bool bufferedFrameAvailable) {
    if (!token || token != token_) return ProgressWatchdogResult::None;
    if (progressBytes != progressBytes_) {
      progressBytes_ = progressBytes;
      bufferedBytes_ = bufferedBytes;
      bodyCompleteObserved_ = bodyComplete;
      lastProgressMs_ = nowMs;
      disconnectedObserved_ = false;
      return ProgressWatchdogResult::None;
    }
    if (bodyComplete) {
      // A complete HTTP body and decoder EOF are distinct states. Once all
      // declared bytes have arrived, TCP close is expected; keep servicing
      // the decoder while its input buffer drains. Actual drain progress
      // renews the same bounded inactivity window, so a stuck decoder cannot
      // leave source playback active forever.
      if (!bodyCompleteObserved_ || bufferedBytes < bufferedBytes_) {
        lastProgressMs_ = nowMs;
      }
      bufferedBytes_ = bufferedBytes;
      bodyCompleteObserved_ = true;
      disconnectedObserved_ = false;
      if (static_cast<uint32_t>(nowMs - lastProgressMs_) >=
          kInactivityTimeoutMs) {
        return ProgressWatchdogResult::Timeout;
      }
      return ProgressWatchdogResult::None;
    }
    bodyCompleteObserved_ = false;
    bufferedBytes_ = bufferedBytes;
    if (bufferedFrameAvailable) return ProgressWatchdogResult::None;
    if (!connected) {
      // Give Audio::loop() one more invocation to turn a fully received body
      // into its synchronous natural-EOF callback. A persistent close with an
      // incomplete body is a transport error, not an elapsed watchdog window.
      if (!disconnectedObserved_) {
        disconnectedObserved_ = true;
        return ProgressWatchdogResult::None;
      }
      return ProgressWatchdogResult::Disconnected;
    }
    disconnectedObserved_ = false;
    if (static_cast<uint32_t>(nowMs - lastProgressMs_) >=
        kInactivityTimeoutMs) {
      return ProgressWatchdogResult::Timeout;
    }
    return ProgressWatchdogResult::None;
  }

  uint32_t token() const { return token_; }
  uint32_t lastProgressMs() const { return lastProgressMs_; }
  uint64_t progressBytes() const { return progressBytes_; }

 private:
  uint32_t token_ = 0;
  uint32_t lastProgressMs_ = 0;
  uint64_t progressBytes_ = 0;
  uint32_t bufferedBytes_ = 0;
  bool bodyCompleteObserved_ = false;
  bool disconnectedObserved_ = false;
};

// The production timeout-to-cleanup dispatcher used from Player::loop().
// An already observed backend EOF/error wins; otherwise timeout signals the
// current generation before requesting the same one-shot cleanup path.
template <typename TimeoutCallback, typename DisconnectCallback,
          typename FinishCallback>
bool serviceSourceMediaTransport(SourceMediaState& media,
                                    ProgressWatchdog& watchdog,
                                    uint32_t nowMs,
                                    uint64_t progressBytes,
                                    bool backendRunning,
                                    bool connected,
                                    bool bodyComplete,
                                    uint32_t bufferedBytes,
                                    bool bufferedFrameAvailable,
                                    TimeoutCallback onTimeout,
                                    DisconnectCallback onDisconnect,
                                    FinishCallback finish) {
  const uint32_t token = media.token();
  if (!token) return false;
  if (backendRunning && !media.terminal()) {
    const ProgressWatchdogResult result = watchdog.evaluate(
        token, nowMs, progressBytes, connected, bodyComplete, bufferedBytes,
        bufferedFrameAvailable);
    if (result != ProgressWatchdogResult::None && media.signal(token)) {
      if (result == ProgressWatchdogResult::Timeout)
        onTimeout(token);
      else
        onDisconnect(token);
    }
  }
  if (backendRunning && !media.terminal()) return false;
  finish();
  return true;
}

}  // namespace source_media
}  // namespace voxone

#endif
