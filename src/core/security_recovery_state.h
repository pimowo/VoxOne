#ifndef VOXONE_SECURITY_RECOVERY_STATE_H
#define VOXONE_SECURITY_RECOVERY_STATE_H

#include <stdint.h>

#include "security_recovery_key.h"

namespace voxone {
namespace security_recovery {

enum class RecoveryState : uint8_t {
  Idle,
  Requested,
  OtaCheck,
  Ready,
  Active,
  AwaitConfirmation,
  Committing,
  Completed,
  Cancelled,
  Timeout,
  Failed,
};

enum class RecoveryEvent : uint8_t {
  BeginOtaCheck,
  Activate,
  RequireConfirmation,
  Confirm,
  CommitSucceeded,
  CommitFailed,
  Cancel,
  InternalFailure,
  Tick,
};

enum class RecoveryImageState : uint8_t {
  Valid,
  PendingVerify,
  Invalid,
  Aborted,
  Unknown,
};

enum class RecoveryDiagnostic : uint8_t {
  None,
  KeyNotAccepted,
  DuplicateRequest,
  Requested,
  OtaCheckStarted,
  Ready,
  Activated,
  ConfirmationRequired,
  ConfirmationAccepted,
  Committing,
  Completed,
  Cancelled,
  SessionTimeout,
  ConfirmationTimeout,
  OtaStatusReadError,
  OtaPendingVerify,
  OtaImageInvalid,
  OtaImageAborted,
  OtaImageUnknown,
  OtaUploadActive,
  ConfigStatusReadError,
  ConfigRestoreActive,
  CommitFailed,
  InternalFailure,
  InvalidEvent,
};

struct RecoveryTimings {
  uint32_t sessionMs;
  uint32_t confirmationMs;
};

constexpr RecoveryTimings kDefaultRecoveryTimings = {600000U, 30000U};

struct GateConditions {
  bool otaStatusReadable;
  RecoveryImageState imageState;
  bool pending;
  bool otaUploadActive;
  bool configStatusReadable;
  bool configRestoreActive;
};

struct RecoveryTransition {
  RecoveryState from;
  RecoveryState to;
  RecoveryDiagnostic diagnostic;
  bool changed;
};

class RecoveryStateMachine {
 public:
  explicit RecoveryStateMachine(
      RecoveryTimings timings = kDefaultRecoveryTimings);

  RecoveryTransition observeBootKey(KeyState keyState, uint32_t nowMs);
  RecoveryTransition dispatch(RecoveryEvent event, uint32_t nowMs);
  RecoveryTransition evaluateGate(const GateConditions& conditions,
                                  uint32_t nowMs);

  RecoveryState state() const { return state_; }
  RecoveryDiagnostic diagnostic() const { return diagnostic_; }
  bool requestedThisBoot() const { return requestConsumed_; }

 private:
  RecoveryTransition transition(RecoveryState state,
                                RecoveryDiagnostic diagnostic);
  RecoveryTransition report(RecoveryDiagnostic diagnostic);
  bool sessionExpired(uint32_t nowMs) const;
  bool confirmationExpired(uint32_t nowMs) const;
  bool sessionActive() const;

  RecoveryTimings timings_;
  RecoveryState state_ = RecoveryState::Idle;
  RecoveryDiagnostic diagnostic_ = RecoveryDiagnostic::None;
  uint32_t sessionStartedMs_ = 0;
  uint32_t confirmationStartedMs_ = 0;
  bool bootKeyObserved_ = false;
  bool requestConsumed_ = false;
};

const char* recoveryStateName(RecoveryState state);
const char* recoveryDiagnosticName(RecoveryDiagnostic diagnostic);

}  // namespace security_recovery
}  // namespace voxone

#endif
