#include "../src/core/security_recovery_session.h"
#include <cassert>
using namespace voxone::security_recovery;
int main() {
  RecoverySession session; assert(session.begin(KeyState::Accepted).to==RecoveryState::Requested); assert(session.live());
  assert(session.activate().to==RecoveryState::Active); assert(session.activated());
  assert(session.beginCommit().to==RecoveryState::Committing); assert(session.complete(true).to==RecoveryState::Completed); assert(!session.live());
  RecoverySession idle; assert(idle.begin(KeyState::Cancelled).to==RecoveryState::Idle); assert(!idle.live());
}
