#ifndef VOXONE_SECURITY_HTTP_ADAPTER_H
#define VOXONE_SECURITY_HTTP_ADAPTER_H

#include "security_access_policy.h"

#include <stddef.h>
#include <stdint.h>

#ifndef VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST
#define VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST 0
#endif

#if defined(VOXONE_PROFILE_W0) || defined(VOXONE_PROFILE_M0) || \
    defined(VOXONE_PROFILE_L0) || defined(VOXONE_PROFILE_S0) || \
    defined(VOXONE_PROFILE_DIN0)
#define VOXONE_SECURITY_STANDARD_PROFILE 1
#else
#define VOXONE_SECURITY_STANDARD_PROFILE 0
#endif

#ifndef VOXONE_SECURITY_HTTP_ENFORCEMENT
// Every standard firmware profile enforces the reviewed HTTP/API/WebSocket
// policy. The dedicated M0 HIL profile selects TEST first, which keeps the
// production bit clear while exercising the same runtime gate.
#if VOXONE_SECURITY_STANDARD_PROFILE && \
    !VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST
#define VOXONE_SECURITY_HTTP_ENFORCEMENT 1
#else
#define VOXONE_SECURITY_HTTP_ENFORCEMENT 0
#endif
#endif

#if (VOXONE_SECURITY_HTTP_ENFORCEMENT != 0 && \
     VOXONE_SECURITY_HTTP_ENFORCEMENT != 1) || \
    (VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST != 0 && \
     VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST != 1)
#error "Security HTTP enforcement flags must be 0 or 1"
#endif

#if VOXONE_SECURITY_HTTP_ENFORCEMENT && \
    VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST
#error "Security HTTP enforcement cannot be both production and test"
#endif

#if VOXONE_SECURITY_STANDARD_PROFILE && \
    !VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST && \
    !VOXONE_SECURITY_HTTP_ENFORCEMENT
#error "Standard VoxOne profiles require production HTTP enforcement"
#endif

namespace voxone {
namespace security {

enum class SecurityHttpEnforcementMode : uint8_t {
  Disabled,
  Test,
  Production
};

constexpr bool securityHttpProductionEnforcementEnabled() {
  return VOXONE_SECURITY_HTTP_ENFORCEMENT != 0;
}

constexpr bool securityHttpTestEnforcementEnabled() {
  return VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST != 0;
}

constexpr SecurityHttpEnforcementMode securityHttpEnforcementMode() {
  return securityHttpProductionEnforcementEnabled()
             ? SecurityHttpEnforcementMode::Production
         : securityHttpTestEnforcementEnabled()
             ? SecurityHttpEnforcementMode::Test
             : SecurityHttpEnforcementMode::Disabled;
}

constexpr const char* securityHttpEnforcementModeName(
    SecurityHttpEnforcementMode mode) {
  return mode == SecurityHttpEnforcementMode::Production
             ? "production"
         : mode == SecurityHttpEnforcementMode::Test ? "test" : "disabled";
}

// This is the actual runtime gate used by HTTP and WebSocket access checks.
// Standard profiles use production mode; the dedicated M0 test profile enables
// only the test flag and must never be reported as production protection.
constexpr bool securityHttpEnforcementEnabled() {
  return securityHttpEnforcementMode() !=
         SecurityHttpEnforcementMode::Disabled;
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
