#include "security_recovery_session.h"

namespace voxone {
namespace security_recovery {

RecoverySession::RecoverySession(RecoveryGateSource& gateSource,
                                 RecoveryTimings timings)
    : gateSource_(gateSource), machine_(timings) {}

RecoveryTransition RecoverySession::current() const {
  return {machine_.state(), machine_.state(), machine_.diagnostic(), false};
}

bool RecoverySession::live() const {
  switch (machine_.state()) {
    case RecoveryState::Requested:
    case RecoveryState::OtaCheck:
    case RecoveryState::Ready:
    case RecoveryState::Active:
    case RecoveryState::AwaitConfirmation:
    case RecoveryState::CommitReady:
    case RecoveryState::Committing:
      return true;
    case RecoveryState::Idle:
    case RecoveryState::Completed:
    case RecoveryState::Cancelled:
    case RecoveryState::Timeout:
    case RecoveryState::Failed:
      return false;
  }
  return false;
}

bool RecoverySession::activated() const {
  return machine_.state() == RecoveryState::Active ||
         machine_.state() == RecoveryState::AwaitConfirmation ||
         machine_.state() == RecoveryState::CommitReady ||
         machine_.state() == RecoveryState::Committing;
}

RecoveryTransition RecoverySession::begin(
    KeyState keyState, uint32_t nowMs) {
  RecoveryTransition result = machine_.observeBootKey(keyState, nowMs);
  if (machine_.state() != RecoveryState::Requested) return result;
  result = machine_.dispatch(RecoveryEvent::BeginOtaCheck, nowMs);
  if (machine_.state() != RecoveryState::OtaCheck) return result;
  return machine_.evaluateGate(gateSource_.read(), nowMs);
}

RecoveryTransition RecoverySession::activate(uint32_t nowMs) {
  if (machine_.state() != RecoveryState::Ready)
    return machine_.recheckGate({}, nowMs);
  const RecoveryTransition checked =
      machine_.recheckGate(gateSource_.read(), nowMs);
  if (machine_.state() != RecoveryState::Ready) return checked;
  return machine_.dispatch(RecoveryEvent::Activate, nowMs);
}

RecoveryTransition RecoverySession::awaitConfirmation(uint32_t nowMs) {
  return machine_.dispatch(RecoveryEvent::RequireConfirmation, nowMs);
}

RecoveryTransition RecoverySession::confirm(uint32_t nowMs) {
  if (machine_.state() != RecoveryState::AwaitConfirmation)
    return machine_.recheckGate({}, nowMs);
  const RecoveryTransition checked =
      machine_.recheckGate(gateSource_.read(), nowMs);
  if (machine_.state() != RecoveryState::AwaitConfirmation) return checked;
  return machine_.dispatch(RecoveryEvent::Confirm, nowMs);
}

RecoveryTransition RecoverySession::beginCommit(uint32_t nowMs) {
  if (machine_.state() != RecoveryState::CommitReady)
    return machine_.dispatch(RecoveryEvent::BeginCommit, nowMs);
  const RecoveryTransition checked =
      machine_.recheckGate(gateSource_.read(), nowMs);
  if (machine_.state() != RecoveryState::CommitReady) return checked;
  return machine_.dispatch(RecoveryEvent::BeginCommit, nowMs);
}

RecoveryTransition RecoverySession::complete(bool success, uint32_t nowMs) {
  return machine_.dispatch(success ? RecoveryEvent::CommitSucceeded
                                   : RecoveryEvent::CommitFailed,
                           nowMs);
}

RecoveryTransition RecoverySession::cancel(uint32_t nowMs) {
  return machine_.dispatch(RecoveryEvent::Cancel, nowMs);
}

RecoveryTransition RecoverySession::fail(uint32_t nowMs) {
  return machine_.dispatch(RecoveryEvent::InternalFailure, nowMs);
}

RecoveryTransition RecoverySession::service(uint32_t nowMs) {
  return live() ? machine_.dispatch(RecoveryEvent::Tick, nowMs) : current();
}

}  // namespace security_recovery
}  // namespace voxone
