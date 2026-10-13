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

// Completes the static part of the health gate. Current VoxOne MAIN OTA is
// network-originated for every profile, so networkRequired must remain true.
void reportBootSetup(bool configurationReady, bool dataReady,
                     bool runtimeReady, bool networkRequired,
                     bool networkReady, bool httpApiReady);

// Refreshes volatile connectivity and proves that the main loop remains live
// while the five-second stability window is open.
void serviceBootHealth(bool networkReady);

// Called only after /api/ready has reached its handler. This proves the remote
// control plane end-to-end; merely starting a listening socket is insufficient.
void reportHttpApiProbe();

// /api/ready uses this to avoid reporting a PENDING_VERIFY image as ready.
bool bootHealthReady();

// A bounded, non-secret JSON object for read-only HTTP/UART diagnostics.
// Returns false if the supplied buffer is too small.
bool formatBootHealthJson(char* output, size_t capacity);

}  // namespace ota
}  // namespace voxone

#endif
