#ifndef VOXONE_SECURITY_RECOVERY_AP_RUNTIME_H
#define VOXONE_SECURITY_RECOVERY_AP_RUNTIME_H

#include "security_recovery_ap.h"

namespace voxone {
namespace security_recovery {

RecoveryApStartResult beginSecurityRecoveryAp(uint32_t nowMs);
bool serviceSecurityRecoveryAp(uint32_t nowMs);
bool securityRecoveryApOwnsRuntime();
bool securityRecoveryApRunning();

}  // namespace security_recovery
}  // namespace voxone

#endif
