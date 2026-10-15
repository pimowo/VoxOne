#ifndef VOXONE_OTA_BOOT_RUNTIME_H
#define VOXONE_OTA_BOOT_RUNTIME_H

#include "ota_boot_health.h"

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace ota {

// Starts read-only inspection of otadata and, only for PENDING_VERIFY, a
// health monitor. Call at the beginning of setup(), before fallible recovery.
void beginBootHealthMonitor();

// Fatal startup failures are reported as soon as they are known. The monitor
// owns the eventual IDF rollback call so callers never reboot directly.
void reportBootFatal(Failure failure);

// Completes the static part of the health gate. controlPlaneReady means that
// the embedded HTTP server and either validated STA or Recovery AP started;
// it does not require Internet access or an external browser request.
void reportBootSetup(bool configurationReady, bool dataReady,
                     bool runtimeReady, bool controlPlaneReady);

// Proves that the main loop and local control plane remain live while the
// five-second stability window is open. Internet availability is not tested.
// Once the pending image is VALID the monitor exits, so later connectivity
// changes cannot invalidate a successfully booted image.
void serviceBootHealth(bool controlPlaneReady);

// Called after /api/ready reaches its handler for diagnostics and the isolated
// rollback HIL profile. Standard images never depend on this external probe.
void reportHttpApiProbe();

// /api/ready uses this to avoid reporting a PENDING_VERIFY image as ready.
bool bootHealthReady();

// A bounded, non-secret JSON object for read-only HTTP/UART diagnostics.
// Returns false if the supplied buffer is too small.
bool formatBootHealthJson(char* output, size_t capacity);

}  // namespace ota
}  // namespace voxone

#endif
