#ifndef VOXONE_SECURITY_RECOVERY_STATE_RUNTIME_H
#define VOXONE_SECURITY_RECOVERY_STATE_RUNTIME_H

#include "security_recovery_state.h"

namespace voxone {
namespace security_recovery {

struct BootRecoveryEvaluation {
  RecoveryState state;
  RecoveryDiagnostic diagnostic;
  bool requested;
};

// Read-only early-boot eligibility evaluation. This does not activate a
// recovery mode and does not alter OTA, config, SECURITY, networking or NVS.
BootRecoveryEvaluation evaluateBootRecoveryRequest(KeyState keyState,
                                                    uint32_t nowMs);

}  // namespace security_recovery
}  // namespace voxone

#endif
