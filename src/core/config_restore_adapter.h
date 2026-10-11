#ifndef VOXONE_CONFIG_RESTORE_ADAPTER_H
#define VOXONE_CONFIG_RESTORE_ADAPTER_H

#include <cstddef>
#include <cstdint>

#include "config_backup.h"

namespace voxone {
namespace config_restore {

enum class BootRecoveryStatus : uint8_t { NotPending, Recovered, Failed };

BootRecoveryStatus recoverAtBoot();
void cleanupStaleAfterStorageInit();

// Internal production entry point. A later API layer may call this after it
// has received a complete VXCF artifact; this stage intentionally exposes no
// HTTP or WWW surface.
config_backup::RestoreResult restoreProductionBackup(const uint8_t* input,
                                                     std::size_t size);

}  // namespace config_restore
}  // namespace voxone

#endif
