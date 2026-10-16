#ifndef VOXONE_SECURITY_RECOVERY_KEY_RUNTIME_H
#define VOXONE_SECURITY_RECOVERY_KEY_RUNTIME_H

#include "security_recovery_key.h"

namespace voxone {
namespace security_recovery {

// Early-boot diagnostic only. It does not change SECURITY, NVS, networking or
// OTA state, and normal boot always continues after it returns.
KeyState detectBootKeyGesture();

// A stuck-low boot attempt must not later turn into a synthetic click/long
// event. Only the KEY path is suppressed; encoder A/B remain operational.
bool runtimeKeyInputReady();

}  // namespace security_recovery
}  // namespace voxone

#endif
