#ifndef VOXONE_SECURITY_RECOVERY_STATE_H
#define VOXONE_SECURITY_RECOVERY_STATE_H

#include <stdint.h>
#include "security_recovery_key.h"

namespace voxone {
namespace security_recovery {

enum class RecoveryState : uint8_t { Idle, Requested, Active, Committing, Completed, Failed };
enum class RecoveryEvent : uint8_t { Activate, BeginCommit, CommitSucceeded, CommitFailed, InternalFailure };
enum class RecoveryDiagnostic : uint8_t {
  None, KeyNotAccepted, DuplicateRequest, Requested, Activated, Committing,
  Completed, CommitFailed, InternalFailure, InvalidEvent,
};

struct RecoveryTransition {
  RecoveryState from;
  RecoveryState to;
  RecoveryDiagnostic diagnostic;
  bool changed;
};

class RecoveryStateMachine {
 public:
  RecoveryTransition observeBootKey(KeyState keyState);
  RecoveryTransition dispatch(RecoveryEvent event);
  RecoveryState state() const { return state_; }
  RecoveryDiagnostic diagnostic() const { return diagnostic_; }
  bool requestedThisBoot() const { return requestConsumed_; }

 private:
  RecoveryTransition transition(RecoveryState state, RecoveryDiagnostic diagnostic);
  RecoveryTransition report(RecoveryDiagnostic diagnostic);
  RecoveryState state_ = RecoveryState::Idle;
  RecoveryDiagnostic diagnostic_ = RecoveryDiagnostic::None;
  bool bootKeyObserved_ = false;
  bool requestConsumed_ = false;
};

const char* recoveryStateName(RecoveryState state);
const char* recoveryDiagnosticName(RecoveryDiagnostic diagnostic);

}  // namespace security_recovery
}  // namespace voxone
#endif
