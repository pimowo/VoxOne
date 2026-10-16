#ifndef VOXONE_SECURITY_RECOVERY_SESSION_RUNTIME_H
#define VOXONE_SECURITY_RECOVERY_SESSION_RUNTIME_H

#include "security_recovery_session.h"

namespace voxone {
namespace security_recovery {

struct RecoverySessionSnapshot {
  RecoveryState state;
  RecoveryDiagnostic diagnostic;
  bool requested;
  bool live;
  bool activated;
};

// Starts one RAM-only diagnostic session for the boot KEY result. It never
// activates Recovery networking or changes SECURITY/storage.
RecoverySessionSnapshot beginRecoverySession(KeyState keyState,
                                             uint32_t nowMs);
void serviceRecoverySession(uint32_t nowMs);
RecoverySessionSnapshot recoverySessionSnapshot();
RecoveryTransition activateRecoverySession(uint32_t nowMs);
RecoveryTransition awaitRecoveryConfirmation(uint32_t nowMs);
RecoveryTransition confirmRecoveryKey(uint32_t nowMs);
// Diagnostic-only transition. It rechecks the production gate and never
// writes SECURITY/NVS or completes a credential transaction.
RecoveryTransition beginRecoveryCommit(uint32_t nowMs);
RecoveryTransition cancelRecoverySession(uint32_t nowMs);
RecoveryTransition failRecoverySession(uint32_t nowMs);
RecoveryTransition completeRecoverySession(bool success, uint32_t nowMs);

}  // namespace security_recovery
}  // namespace voxone

#endif
