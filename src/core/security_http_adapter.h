#ifndef VOXONE_SECURITY_HTTP_ADAPTER_H
#define VOXONE_SECURITY_HTTP_ADAPTER_H

#include "security_access_policy.h"

#include <stddef.h>
#include <stdint.h>

#ifndef VOXONE_SECURITY_HTTP_ENFORCEMENT
#define VOXONE_SECURITY_HTTP_ENFORCEMENT 0
#endif

#ifndef VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST
#define VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST 0
#endif

#if (VOXONE_SECURITY_HTTP_ENFORCEMENT != 0 && \
     VOXONE_SECURITY_HTTP_ENFORCEMENT != 1) || \
    (VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST != 0 && \
     VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST != 1)
#error "Security HTTP enforcement flags must be 0 or 1"
#endif

namespace voxone {
namespace security {

// Production enforcement is monotonic: once the production flag is enabled,
// disabling the test flag cannot weaken it. Ordinary profiles currently keep
// both flags off; the dedicated M0 test profile enables only the test flag.
constexpr bool securityHttpEnforcementEnabled() {
  return VOXONE_SECURITY_HTTP_ENFORCEMENT != 0 ||
         VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST != 0;
}

// requestedWebsocket is the connection type reported by AsyncWebServer. It is
// only a candidate signal: the library also reports RCT_WS for an arbitrary
// HTTP request carrying "Upgrade: websocket".
bool securityHttpWebSocketDeferred(SecurityAccessMethod method,
                                   const char* path,
                                   bool requestedWebsocket);

bool securityHttpRecoveryLoginRequired(bool enforcementEnabled,
                                       SecurityState state, bool wwwReady,
                                       bool filesystemUnavailable);

// While SPIFFS is unavailable the generic update guard blocks normal traffic.
// These firmware-only routes must still reach their registered handlers so a
// protected device can establish/inspect/revoke a session and report bootId.
bool securityHttpFilesystemRecoveryPassthrough(SecurityAccessMethod method,
                                               const char* path);

struct SecurityHttpDecision {
  SecurityAccessStatus status;
  uint16_t httpStatus;
  bool allowed;
  bool websocketDeferred;
};

SecurityHttpDecision evaluateSecurityHttpRequest(
    const SecurityAccessRequest& request, bool enforcementEnabled,
    bool websocketRequest);

uint16_t securityAccessHttpStatus(SecurityAccessStatus status);

// AsyncWebServer URL-decodes the path before the gate sees it. Reject control
// bytes, embedded NUL and backslashes so an encoded representation cannot be
// classified as one route and dispatched as another.
bool securityHttpPathRepresentationAllowed(const char* path, size_t length);

}  // namespace security
}  // namespace voxone

#endif
