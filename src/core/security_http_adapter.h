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

constexpr char kSecurityWebSocketProtocol[] = "voxone.v1";

// AsyncWebServer reports RCT_WS from Upgrade alone. This stricter description
// proves that the candidate is a complete /ws handshake before the HTTP gate
// changes its access method to WebSocketHandshake.
struct SecurityWebSocketHandshake {
  bool requestedWebsocket;
  size_t connectionHeaders;
  size_t upgradeHeaders;
  size_t versionHeaders;
  size_t keyHeaders;
  size_t protocolHeaders;
  size_t cookieHeaders;
  const char* connection;
  const char* upgrade;
  const char* version;
  const char* key;
  const char* protocol;
};

enum class SecurityWebSocketHandshakeStatus : uint8_t {
  NotWebSocket,
  Valid,
  Invalid
};

SecurityWebSocketHandshakeStatus securityWebSocketHandshakeStatus(
    SecurityAccessMethod method, const char* path,
    const SecurityWebSocketHandshake& handshake);
bool securityWebSocketSubprotocolRequested(
    const SecurityWebSocketHandshake& handshake);
bool securityWebSocketCommandAllowed(const char* command);

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
  bool websocketHandshake;
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
