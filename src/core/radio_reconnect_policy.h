#ifndef VOXONE_RADIO_RECONNECT_POLICY_H
#define VOXONE_RADIO_RECONNECT_POLICY_H

#include <stdint.h>

namespace voxone {
namespace radio {

constexpr uint32_t kStreamInactivityTimeoutMs = 10000;

class ReconnectPolicy {
 public:
  void reset() {
    failures_ = 0;
    pending_ = false;
    dueAtMs_ = 0;
  }

  void cancel() { reset(); }

  bool schedule(uint32_t nowMs) {
    if (pending_) return false;
    dueAtMs_ = nowMs + delayForFailure(failures_);
    pending_ = true;
    if (failures_ < kDelayCount) ++failures_;
    return true;
  }

  bool takeDue(uint32_t nowMs, bool allowed, bool networkReady) {
    if (!allowed) {
      cancel();
      return false;
    }
    if (!pending_ || !networkReady ||
        static_cast<int32_t>(nowMs - dueAtMs_) < 0) return false;
    pending_ = false;
    return true;
  }

  bool pending() const { return pending_; }
  uint8_t failures() const { return failures_; }
  uint32_t dueAtMs() const { return dueAtMs_; }

  static uint32_t delayForFailure(uint8_t failures) {
    static constexpr uint32_t delays[] = {1000, 2000, 5000, 10000, 15000, 30000};
    const uint8_t index = failures < kDelayCount ? failures : kDelayCount - 1;
    return delays[index];
  }

 private:
  static constexpr uint8_t kDelayCount = 6;
  uint32_t dueAtMs_ = 0;
  uint8_t failures_ = 0;
  bool pending_ = false;
};

inline bool streamIsDead(bool socketConnected, bool bufferedFrameAvailable,
                         uint32_t nowMs, uint32_t lastDataMs) {
  if (bufferedFrameAvailable) return false;
  return !socketConnected ||
         static_cast<uint32_t>(nowMs - lastDataMs) >= kStreamInactivityTimeoutMs;
}

enum class SegmentTransportResult : uint8_t {
  Waiting = 0,
  SegmentComplete,
  RecoverStation,
};

// HLS segment completion is a normal transport transition, not a station
// failure. Only a disconnected/drained or time-stalled incomplete segment is
// returned to Player for the common station reconnect policy.
class SegmentTransportWatchdog {
 public:
  void reset(uint32_t nowMs) {
    begin(false, 0, false, nowMs);
  }

  void begin(bool contentLengthKnown, uint32_t contentLength, bool chunked,
             uint32_t nowMs) {
    contentLengthKnown_ = contentLengthKnown;
    contentLength_ = contentLength;
    bodyBytes_ = 0;
    chunked_ = chunked;
    chunkComplete_ = false;
    bodyInvalid_ = false;
    lastDataMs_ = nowMs;
  }

  uint32_t readLimit(uint32_t availableBytes) const {
    if (chunked_ || !contentLengthKnown_) return availableBytes;
    if (bodyBytes_ >= contentLength_) return 0;
    const uint32_t remaining = contentLength_ - bodyBytes_;
    return availableBytes < remaining ? availableBytes : remaining;
  }

  bool received(uint32_t bytes, uint32_t nowMs) {
    if (!bytes) return true;
    if (bodyBytes_ > UINT32_MAX - bytes) {
      bodyBytes_ = UINT32_MAX;
      bodyInvalid_ = true;
      return false;
    }
    bodyBytes_ += bytes;
    if (!chunked_ && contentLengthKnown_ && bodyBytes_ > contentLength_)
      bodyInvalid_ = true;
    lastDataMs_ = nowMs;
    return !bodyInvalid_;
  }

  void chunkComplete() { chunkComplete_ = true; }
  void chunkError() { bodyInvalid_ = true; }

  SegmentTransportResult evaluate(bool transportExpected,
                                  bool socketConnected,
                                  uint32_t clientAvailableBytes,
                                  bool bufferedFrameAvailable,
                                  uint16_t partialUnitBytes,
                                  uint32_t nowMs) const {
    if (!transportExpected) return SegmentTransportResult::Waiting;

    if (bodyInvalid_) return SegmentTransportResult::RecoverStation;

    if (chunked_) {
      if (chunkComplete_)
        return partialUnitBytes == 0
                   ? SegmentTransportResult::SegmentComplete
                   : SegmentTransportResult::RecoverStation;
    } else if (contentLengthKnown_) {
      if (bodyBytes_ == contentLength_)
        return partialUnitBytes == 0
                   ? SegmentTransportResult::SegmentComplete
                   : SegmentTransportResult::RecoverStation;
    } else if (!socketConnected && clientAvailableBytes == 0) {
      // Without framing headers, connection close is the HTTP body delimiter.
      return bodyBytes_ != 0 && partialUnitBytes == 0
                 ? SegmentTransportResult::SegmentComplete
                 : SegmentTransportResult::RecoverStation;
    }

    // WiFiClient may retain unread response bytes after the peer has closed.
    // They must be consumed before disconnect is interpreted as truncation.
    if (clientAvailableBytes != 0) return SegmentTransportResult::Waiting;

    if (!socketConnected ||
        static_cast<uint32_t>(nowMs - lastDataMs_) >=
            kStreamInactivityTimeoutMs)
      return SegmentTransportResult::RecoverStation;

    (void)bufferedFrameAvailable;
    return SegmentTransportResult::Waiting;
  }

  uint32_t lastDataMs() const { return lastDataMs_; }
  uint32_t bodyBytes() const { return bodyBytes_; }
  uint32_t contentLength() const { return contentLength_; }
  bool contentLengthKnown() const { return contentLengthKnown_; }
  bool chunked() const { return chunked_; }

 private:
  uint32_t lastDataMs_ = 0;
  uint32_t contentLength_ = 0;
  uint32_t bodyBytes_ = 0;
  bool contentLengthKnown_ = false;
  bool chunked_ = false;
  bool chunkComplete_ = false;
  bool bodyInvalid_ = false;
};

}  // namespace radio
}  // namespace voxone

#endif
