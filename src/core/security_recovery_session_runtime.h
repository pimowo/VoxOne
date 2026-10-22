#ifndef VOXONE_SECURITY_RECOVERY_SESSION_RUNTIME_H
#define VOXONE_SECURITY_RECOVERY_SESSION_RUNTIME_H
#include "security_recovery_session.h"
namespace voxone { namespace security_recovery {
struct RecoverySessionSnapshot { RecoveryState state; RecoveryDiagnostic diagnostic; bool requested; bool live; bool activated; };
RecoverySessionSnapshot beginRecoverySession(KeyState keyState);
RecoverySessionSnapshot recoverySessionSnapshot();
RecoveryTransition activateRecoverySession();
RecoveryTransition beginRecoveryCommit();
RecoveryTransition failRecoverySession();
RecoveryTransition completeRecoverySession(bool success);
} }
#endif
