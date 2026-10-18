#ifndef VOXONE_WIFI_STARTUP_RETRY_H
#define VOXONE_WIFI_STARTUP_RETRY_H

#include <stdint.h>

namespace voxone::wifi {

enum class StartupFailureDecision : uint8_t {
  Retry,
  Recovery
};

struct StartupAttempt {
  bool valid;
  uint8_t ordinal;
  uint8_t total;
  uint8_t profileIndex;
  uint32_t generation;
};

// A boot-time policy only. Runtime reassociation keeps its separate,
// unbounded-with-backoff policy after this boot has accepted a real GOT_IP.
class StartupRetryPolicy {
 public:
  static constexpr uint8_t kMinimumAttempts = 3;
  static constexpr uint32_t kAttemptTimeoutMs = 9000;
  static constexpr uint32_t kPollIntervalMs = 100;

  void begin(uint8_t profileCount, uint8_t preferredProfileOneBased,
             uint32_t nowMs) {
    profileCount_ = profileCount;
    firstProfile_ = preferredProfileOneBased > 0 &&
                            preferredProfileOneBased <= profileCount
                        ? preferredProfileOneBased - 1
                        : 0;
    const uint8_t oneCompleteRoundAndRetry =
        profileCount == 0 ? 0 : static_cast<uint8_t>(profileCount + 1);
    totalAttempts_ = profileCount == 0
                         ? 0
                         : (oneCompleteRoundAndRetry < kMinimumAttempts
                                ? kMinimumAttempts
                                : oneCompleteRoundAndRetry);
    startedAttempts_ = 0;
    generation_ = 0;
    dueAtMs_ = nowMs;
    startedAtMs_ = nowMs;
    active_ = false;
    currentProfile_ = 0;
  }

  bool hasRemaining() const {
    return profileCount_ != 0 && startedAttempts_ < totalAttempts_;
  }

  bool due(uint32_t nowMs) const {
    return hasRemaining() && !active_ &&
        static_cast<int32_t>(nowMs - dueAtMs_) >= 0;
  }

  StartupAttempt start(uint32_t nowMs) {
    if (!due(nowMs)) return {false, 0, totalAttempts_, 0, generation_};
    const uint8_t profile = static_cast<uint8_t>(
        (firstProfile_ + startedAttempts_) % profileCount_);
    ++startedAttempts_;
    ++generation_;
    currentProfile_ = profile;
    startedAtMs_ = nowMs;
    active_ = true;
    return {true, startedAttempts_, totalAttempts_, profile, generation_};
  }

  bool timedOut(uint32_t nowMs) const {
    return active_ &&
        static_cast<uint32_t>(nowMs - startedAtMs_) >= kAttemptTimeoutMs;
  }

  bool accepts(uint32_t generation, uint8_t profileIndex) const {
    return active_ && generation == generation_ &&
        profileIndex == currentProfile_;
  }

  bool succeed(uint32_t generation, uint8_t profileIndex) {
    if (!accepts(generation, profileIndex)) return false;
    active_ = false;
    return true;
  }

  StartupFailureDecision fail(uint32_t generation, uint8_t profileIndex,
                              uint32_t nowMs) {
    if (!accepts(generation, profileIndex))
      return hasRemaining() ? StartupFailureDecision::Retry
                            : StartupFailureDecision::Recovery;
    active_ = false;
    if (!hasRemaining()) return StartupFailureDecision::Recovery;
    dueAtMs_ = nowMs + backoffBeforeAttempt(startedAttempts_ + 1);
    return StartupFailureDecision::Retry;
  }

  uint32_t dueAtMs() const { return dueAtMs_; }
  uint8_t totalAttempts() const { return totalAttempts_; }
  uint8_t startedAttempts() const { return startedAttempts_; }

  static constexpr uint32_t backoffBeforeAttempt(uint8_t ordinal) {
    return ordinal <= 1 ? 0 : ordinal == 2 ? 500 : ordinal == 3 ? 1500 : 2000;
  }

 private:
  uint32_t dueAtMs_ = 0;
  uint32_t startedAtMs_ = 0;
  uint32_t generation_ = 0;
  uint8_t profileCount_ = 0;
  uint8_t firstProfile_ = 0;
  uint8_t totalAttempts_ = 0;
  uint8_t startedAttempts_ = 0;
  uint8_t currentProfile_ = 0;
  bool active_ = false;
};

}  // namespace voxone::wifi

#endif
