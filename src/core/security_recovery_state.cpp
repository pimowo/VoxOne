#include "security_recovery_state.h"

namespace voxone {
namespace security_recovery {

RecoveryStateMachine::RecoveryStateMachine(RecoveryTimings timings)
    : timings_(timings) {}

RecoveryTransition RecoveryStateMachine::transition(
    RecoveryState state, RecoveryDiagnostic diagnostic) {
  const RecoveryState from = state_;
  state_ = state;
  diagnostic_ = diagnostic;
  return {from, state_, diagnostic_, from != state_};
}

RecoveryTransition RecoveryStateMachine::report(
    RecoveryDiagnostic diagnostic) {
  diagnostic_ = diagnostic;
  return {state_, state_, diagnostic_, false};
}

bool RecoveryStateMachine::sessionExpired(uint32_t nowMs) const {
  return static_cast<uint32_t>(nowMs - sessionStartedMs_) >=
         timings_.sessionMs;
}

bool RecoveryStateMachine::confirmationExpired(uint32_t nowMs) const {
  return static_cast<uint32_t>(nowMs - confirmationStartedMs_) >=
         timings_.confirmationMs;
}

bool RecoveryStateMachine::sessionActive() const {
  return state_ == RecoveryState::Requested ||
         state_ == RecoveryState::OtaCheck ||
         state_ == RecoveryState::Ready || state_ == RecoveryState::Active ||
         state_ == RecoveryState::AwaitConfirmation ||
         state_ == RecoveryState::Committing;
}

RecoveryTransition RecoveryStateMachine::observeBootKey(KeyState keyState,
                                                        uint32_t nowMs) {
  if (bootKeyObserved_) return report(RecoveryDiagnostic::DuplicateRequest);
  bootKeyObserved_ = true;
  if (keyState != KeyState::Accepted)
    return report(RecoveryDiagnostic::KeyNotAccepted);
  if (state_ != RecoveryState::Idle)
    return report(RecoveryDiagnostic::InvalidEvent);
  requestConsumed_ = true;
  sessionStartedMs_ = nowMs;
  return transition(RecoveryState::Requested,
                    RecoveryDiagnostic::Requested);
}

RecoveryTransition RecoveryStateMachine::dispatch(RecoveryEvent event,
                                                   uint32_t nowMs) {
  if (sessionActive() && sessionExpired(nowMs))
    return transition(RecoveryState::Timeout,
                      RecoveryDiagnostic::SessionTimeout);
  if (state_ == RecoveryState::AwaitConfirmation &&
      confirmationExpired(nowMs))
    return transition(RecoveryState::Timeout,
                      RecoveryDiagnostic::ConfirmationTimeout);

  if (event == RecoveryEvent::Tick) {
    if (!sessionActive()) return report(RecoveryDiagnostic::InvalidEvent);
    return report(diagnostic_);
  }

  if (event == RecoveryEvent::InternalFailure && sessionActive())
    return transition(RecoveryState::Failed,
                      RecoveryDiagnostic::InternalFailure);

  if (event == RecoveryEvent::Cancel && sessionActive() &&
      state_ != RecoveryState::Committing)
    return transition(RecoveryState::Cancelled,
                      RecoveryDiagnostic::Cancelled);

  switch (state_) {
    case RecoveryState::Requested:
      if (event == RecoveryEvent::BeginOtaCheck)
        return transition(RecoveryState::OtaCheck,
                          RecoveryDiagnostic::OtaCheckStarted);
      break;
    case RecoveryState::Ready:
      if (event == RecoveryEvent::Activate)
        return transition(RecoveryState::Active,
                          RecoveryDiagnostic::Activated);
      break;
    case RecoveryState::Active:
      if (event == RecoveryEvent::RequireConfirmation) {
        confirmationStartedMs_ = nowMs;
        return transition(RecoveryState::AwaitConfirmation,
                          RecoveryDiagnostic::ConfirmationRequired);
      }
      break;
    case RecoveryState::AwaitConfirmation:
      if (event == RecoveryEvent::Confirm) {
        return transition(RecoveryState::Committing,
                          RecoveryDiagnostic::ConfirmationAccepted);
      }
      break;
    case RecoveryState::Committing:
      if (event == RecoveryEvent::CommitSucceeded)
        return transition(RecoveryState::Completed,
                          RecoveryDiagnostic::Completed);
      if (event == RecoveryEvent::CommitFailed ||
          event == RecoveryEvent::InternalFailure)
        return transition(RecoveryState::Failed,
                          event == RecoveryEvent::CommitFailed
                              ? RecoveryDiagnostic::CommitFailed
                              : RecoveryDiagnostic::InternalFailure);
      break;
    case RecoveryState::Idle:
    case RecoveryState::OtaCheck:
    case RecoveryState::Completed:
    case RecoveryState::Cancelled:
    case RecoveryState::Timeout:
    case RecoveryState::Failed:
      break;
  }
  return report(RecoveryDiagnostic::InvalidEvent);
}

RecoveryTransition RecoveryStateMachine::evaluateGate(
    const GateConditions& conditions, uint32_t nowMs) {
  if (state_ != RecoveryState::OtaCheck)
    return report(RecoveryDiagnostic::InvalidEvent);
  if (sessionExpired(nowMs))
    return transition(RecoveryState::Timeout,
                      RecoveryDiagnostic::SessionTimeout);
  const RecoveryDiagnostic failure = gateFailure(conditions);
  if (failure != RecoveryDiagnostic::None)
    return transition(RecoveryState::Failed, failure);
  return transition(RecoveryState::Ready, RecoveryDiagnostic::Ready);
}

RecoveryTransition RecoveryStateMachine::recheckGate(
    const GateConditions& conditions, uint32_t nowMs) {
  if (state_ != RecoveryState::Ready &&
      state_ != RecoveryState::AwaitConfirmation)
    return report(RecoveryDiagnostic::InvalidEvent);
  if (sessionExpired(nowMs))
    return transition(RecoveryState::Timeout,
                      RecoveryDiagnostic::SessionTimeout);
  if (state_ == RecoveryState::AwaitConfirmation &&
      confirmationExpired(nowMs))
    return transition(RecoveryState::Timeout,
                      RecoveryDiagnostic::ConfirmationTimeout);
  const RecoveryDiagnostic failure = gateFailure(conditions);
  if (failure != RecoveryDiagnostic::None)
    return transition(RecoveryState::Failed, failure);
  return report(RecoveryDiagnostic::GateRechecked);
}

RecoveryDiagnostic RecoveryStateMachine::gateFailure(
    const GateConditions& conditions) const {
  if (!conditions.otaStatusReadable)
    return RecoveryDiagnostic::OtaStatusReadError;
  if (conditions.pending ||
      conditions.imageState == RecoveryImageState::PendingVerify)
    return RecoveryDiagnostic::OtaPendingVerify;
  if (conditions.imageState == RecoveryImageState::Invalid)
    return RecoveryDiagnostic::OtaImageInvalid;
  if (conditions.imageState == RecoveryImageState::Aborted)
    return RecoveryDiagnostic::OtaImageAborted;
  if (conditions.imageState != RecoveryImageState::Valid)
    return RecoveryDiagnostic::OtaImageUnknown;
  if (conditions.otaUploadActive)
    return RecoveryDiagnostic::OtaUploadActive;
  if (!conditions.configStatusReadable)
    return RecoveryDiagnostic::ConfigStatusReadError;
  if (conditions.configRestoreActive)
    return RecoveryDiagnostic::ConfigRestoreActive;
  return RecoveryDiagnostic::None;
}

const char* recoveryStateName(RecoveryState state) {
  switch (state) {
    case RecoveryState::Idle: return "idle";
    case RecoveryState::Requested: return "requested";
    case RecoveryState::OtaCheck: return "ota_check";
    case RecoveryState::Ready: return "ready";
    case RecoveryState::Active: return "active";
    case RecoveryState::AwaitConfirmation: return "await_confirmation";
    case RecoveryState::Committing: return "committing";
    case RecoveryState::Completed: return "completed";
    case RecoveryState::Cancelled: return "cancelled";
    case RecoveryState::Timeout: return "timeout";
    case RecoveryState::Failed: return "failed";
  }
  return "unknown";
}

const char* recoveryDiagnosticName(RecoveryDiagnostic diagnostic) {
  switch (diagnostic) {
    case RecoveryDiagnostic::None: return "none";
    case RecoveryDiagnostic::KeyNotAccepted: return "key_not_accepted";
    case RecoveryDiagnostic::DuplicateRequest: return "duplicate_request";
    case RecoveryDiagnostic::Requested: return "requested";
    case RecoveryDiagnostic::OtaCheckStarted: return "ota_check_started";
    case RecoveryDiagnostic::Ready: return "ready";
    case RecoveryDiagnostic::Activated: return "activated";
    case RecoveryDiagnostic::ConfirmationRequired: return "confirmation_required";
    case RecoveryDiagnostic::ConfirmationAccepted: return "confirmation_accepted";
    case RecoveryDiagnostic::GateRechecked: return "gate_rechecked";
    case RecoveryDiagnostic::Committing: return "committing";
    case RecoveryDiagnostic::Completed: return "completed";
    case RecoveryDiagnostic::Cancelled: return "cancelled";
    case RecoveryDiagnostic::SessionTimeout: return "session_timeout";
    case RecoveryDiagnostic::ConfirmationTimeout: return "confirmation_timeout";
    case RecoveryDiagnostic::OtaStatusReadError: return "ota_status_read_error";
    case RecoveryDiagnostic::OtaPendingVerify: return "ota_pending_verify";
    case RecoveryDiagnostic::OtaImageInvalid: return "ota_image_invalid";
    case RecoveryDiagnostic::OtaImageAborted: return "ota_image_aborted";
    case RecoveryDiagnostic::OtaImageUnknown: return "ota_image_unknown";
    case RecoveryDiagnostic::OtaUploadActive: return "ota_upload_active";
    case RecoveryDiagnostic::ConfigStatusReadError: return "config_status_read_error";
    case RecoveryDiagnostic::ConfigRestoreActive: return "config_restore_active";
    case RecoveryDiagnostic::CommitFailed: return "commit_failed";
    case RecoveryDiagnostic::InternalFailure: return "internal_failure";
    case RecoveryDiagnostic::InvalidEvent: return "invalid_event";
  }
  return "unknown";
}

}  // namespace security_recovery
}  // namespace voxone
