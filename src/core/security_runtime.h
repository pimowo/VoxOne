#ifndef VOXONE_SECURITY_RUNTIME_H
#define VOXONE_SECURITY_RUNTIME_H

#include "security_settings.h"

namespace voxone {
namespace security {

struct SecurityRuntimeDiagnostics {
  SecurityState state;
  SecurityLoadStatus loadStatus;
  bool wifiConfigured;
};

SecurityRuntimeDiagnostics initializeSecurityRuntime(
    SecurityService& service, bool wifiConfigured);

// Device adapter. Call once after NVS-backed configuration services and the
// Wi-Fi profile inventory are ready, before starting network services.
SecurityRuntimeDiagnostics initializeDeviceSecurity(bool wifiConfigured);

}  // namespace security
}  // namespace voxone

#endif
