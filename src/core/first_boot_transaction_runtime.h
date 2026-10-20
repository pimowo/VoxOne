#ifndef VOXONE_FIRST_BOOT_TRANSACTION_RUNTIME_H
#define VOXONE_FIRST_BOOT_TRANSACTION_RUNTIME_H

#include "first_boot_transaction.h"

namespace voxone {
namespace first_boot_transaction {

Result commitDeviceFirstBootTransaction(
    const hardware::RuntimeHardwareConfig& requestedHardware,
    const char* ssid, const char* password, bool securityLockHeld);

RecoveryStatus recoverDeviceFirstBootTransactionAtBoot();

// A failed rollback is fail-closed even while production HTTP enforcement is
// disabled. The current boot must not expose a partially OPEN configuration.
bool deviceFirstBootTransactionBlocked();

}  // namespace first_boot_transaction
}  // namespace voxone

#endif
