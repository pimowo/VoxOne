#include "security_recovery_state.h"

namespace voxone {
namespace security_recovery {

RecoveryTransition RecoveryStateMachine::transition(RecoveryState state,
                                                     RecoveryDiagnostic diagnostic) {
  const RecoveryState from = state_;
  state_ = state;
  diagnostic_ = diagnostic;
  return {from, state_, diagnostic_, from != state_};
}

RecoveryTransition RecoveryStateMachine::report(RecoveryDiagnostic diagnostic) {
  diagnostic_ = diagnostic;
  return {state_, state_, diagnostic_, false};
}

RecoveryTransition RecoveryStateMachine::observeBootKey(KeyState keyState) {
  if (bootKeyObserved_) return report(RecoveryDiagnostic::DuplicateRequest);
  bootKeyObserved_ = true;
  if (keyState != KeyState::Accepted) return report(RecoveryDiagnostic::KeyNotAccepted);
  requestConsumed_ = true;
  return transition(RecoveryState::Requested, RecoveryDiagnostic::Requested);
}

RecoveryTransition RecoveryStateMachine::dispatch(RecoveryEvent event) {
  if (event == RecoveryEvent::InternalFailure && state_ != RecoveryState::Idle &&
      state_ != RecoveryState::Completed)
    return transition(RecoveryState::Failed, RecoveryDiagnostic::InternalFailure);
  switch (state_) {
    case RecoveryState::Requested:
      if (event == RecoveryEvent::Activate)
        return transition(RecoveryState::Active, RecoveryDiagnostic::Activated);
      break;
    case RecoveryState::Active:
      if (event == RecoveryEvent::BeginCommit)
        return transition(RecoveryState::Committing, RecoveryDiagnostic::Committing);
      break;
    case RecoveryState::Committing:
      if (event == RecoveryEvent::CommitSucceeded)
        return transition(RecoveryState::Completed, RecoveryDiagnostic::Completed);
      if (event == RecoveryEvent::CommitFailed)
        return transition(RecoveryState::Failed, RecoveryDiagnostic::CommitFailed);
      break;
    case RecoveryState::Idle:
    case RecoveryState::Completed:
    case RecoveryState::Failed:
      break;
  }
  return report(RecoveryDiagnostic::InvalidEvent);
}

const char* recoveryStateName(RecoveryState state) {
  switch (state) {
    case RecoveryState::Idle: return "idle";
    case RecoveryState::Requested: return "requested";
    case RecoveryState::Active: return "active";
    case RecoveryState::Committing: return "committing";
    case RecoveryState::Completed: return "completed";
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
    case RecoveryDiagnostic::Activated: return "activated";
    case RecoveryDiagnostic::Committing: return "committing";
    case RecoveryDiagnostic::Completed: return "completed";
    case RecoveryDiagnostic::CommitFailed: return "commit_failed";
    case RecoveryDiagnostic::InternalFailure: return "internal_failure";
    case RecoveryDiagnostic::InvalidEvent: return "invalid_event";
  }
  return "unknown";
}

}  // namespace security_recovery
}  // namespace voxone
