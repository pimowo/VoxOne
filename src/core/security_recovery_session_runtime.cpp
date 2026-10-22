#include "security_recovery_session_runtime.h"
namespace voxone { namespace security_recovery { namespace {
RecoverySession session;
RecoverySessionSnapshot snapshot() { return {session.state(), session.diagnostic(), session.requestedThisBoot(), session.live(), session.activated()}; }
}
RecoverySessionSnapshot beginRecoverySession(KeyState keyState) { session.begin(keyState); return snapshot(); }
RecoverySessionSnapshot recoverySessionSnapshot() { return snapshot(); }
RecoveryTransition activateRecoverySession() { return session.activate(); }
RecoveryTransition beginRecoveryCommit() { return session.beginCommit(); }
RecoveryTransition failRecoverySession() { return session.fail(); }
RecoveryTransition completeRecoverySession(bool success) { return session.complete(success); }
} }
