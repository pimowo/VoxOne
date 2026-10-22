#include "../src/core/security_recovery_state.h"
#include <cassert>
using namespace voxone::security_recovery;
int main() {
  RecoveryStateMachine normal; assert(normal.observeBootKey(KeyState::Cancelled).to==RecoveryState::Idle); assert(!normal.requestedThisBoot());
  RecoveryStateMachine recovery; assert(recovery.observeBootKey(KeyState::Accepted).to==RecoveryState::Requested); assert(recovery.requestedThisBoot());
  assert(recovery.dispatch(RecoveryEvent::Activate).to==RecoveryState::Active);
  assert(recovery.dispatch(RecoveryEvent::BeginCommit).to==RecoveryState::Committing);
  assert(recovery.dispatch(RecoveryEvent::CommitSucceeded).to==RecoveryState::Completed);
  RecoveryStateMachine failed; failed.observeBootKey(KeyState::Accepted); failed.dispatch(RecoveryEvent::Activate); failed.dispatch(RecoveryEvent::BeginCommit); assert(failed.dispatch(RecoveryEvent::CommitFailed).to==RecoveryState::Failed);
  assert(failed.observeBootKey(KeyState::Accepted).diagnostic==RecoveryDiagnostic::DuplicateRequest);
}
