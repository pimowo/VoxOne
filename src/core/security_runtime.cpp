#include "security_runtime.h"

namespace voxone {
namespace security {

SecurityRuntimeDiagnostics initializeSecurityRuntime(
    SecurityService& service, bool wifiConfigured) {
  const SecurityLoadStatus loadStatus = service.begin(wifiConfigured);
  return {service.state(), loadStatus, wifiConfigured};
}

}  // namespace security
}  // namespace voxone
