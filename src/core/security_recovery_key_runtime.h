#ifndef VOXONE_SECURITY_RECOVERY_KEY_RUNTIME_H
#define VOXONE_SECURITY_RECOVERY_KEY_RUNTIME_H

#include "security_recovery_key.h"

namespace voxone {
namespace security_recovery {

// Samples the active-low KEY once at early boot with only contact debounce.
// The accepted decision is latched for this boot; release is never required.
KeyState detectBootKeyAtStartup();

// Used by the one-shot post-factory-reset FIRST BOOT bypass so a still-held KEY
// cannot become a synthetic runtime click. Encoder A/B remain operational.
void suppressRuntimeKeyUntilRelease();
bool runtimeKeyInputReady();

}  // namespace security_recovery
}  // namespace voxone

#endif
