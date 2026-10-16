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

}  // namespace security_recovery
}  // namespace voxone

#endif
