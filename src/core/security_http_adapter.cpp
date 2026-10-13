#include "security_http_adapter.h"

#include <cstring>

namespace voxone {
namespace security {

bool securityHttpWebSocketDeferred(SecurityAccessMethod method,
                                   const char* path,
                                   bool requestedWebsocket) {
  return requestedWebsocket && method == SecurityAccessMethod::Get && path &&
         std::strcmp(path, "/ws") == 0;
}

bool securityHttpRecoveryLoginRequired(bool enforcementEnabled,
                                       SecurityState state, bool wwwReady,
                                       bool filesystemUnavailable) {
  return enforcementEnabled && state == SecurityState::Protected &&
         (!wwwReady || filesystemUnavailable);
}

bool securityHttpFilesystemRecoveryPassthrough(SecurityAccessMethod method,
                                               const char* path) {
  if (!path) return false;
  if (method == SecurityAccessMethod::Get)
    return std::strcmp(path, "/") == 0 ||
           std::strcmp(path, "/api/security/state") == 0 ||
           std::strcmp(path, "/api/ready") == 0;
  if (method == SecurityAccessMethod::Post)
    return std::strcmp(path, "/api/security/login") == 0 ||
           std::strcmp(path, "/api/security/logout") == 0;
  return false;
}

bool securityHttpPathRepresentationAllowed(const char* path, size_t length) {
  if (!path || !length || path[0] != '/') return false;
  for (size_t index = 0; index < length; ++index) {
    const unsigned char value = static_cast<unsigned char>(path[index]);
    if (value == 0 || value < 0x20 || value == 0x7f || value == '\\')
      return false;
  }
  return true;
}

uint16_t securityAccessHttpStatus(SecurityAccessStatus status) {
  switch (status) {
    case SecurityAccessStatus::Allowed: return 200;
    case SecurityAccessStatus::AuthenticationRequired: return 401;
    case SecurityAccessStatus::MethodNotAllowed: return 405;
    case SecurityAccessStatus::UnknownRoute: return 404;
    case SecurityAccessStatus::InvalidHost:
    case SecurityAccessStatus::InvalidOrigin:
    case SecurityAccessStatus::InvalidState:
    case SecurityAccessStatus::Locked:
    case SecurityAccessStatus::DirectCompressedAssetDenied:
      return 403;
  }
  return 403;
}

SecurityHttpDecision evaluateSecurityHttpRequest(
    const SecurityAccessRequest& request, bool enforcementEnabled,
    bool websocketRequest) {
  // SECURITY-ENFORCE-1B deliberately leaves the WebSocket handshake and
  // frames untouched. A later stage must adapt them before production can
  // claim complete protection.
  if (websocketRequest)
    return {SecurityAccessStatus::Allowed, 200, true, true};
  if (!enforcementEnabled)
    return {SecurityAccessStatus::Allowed, 200, true, false};

  const SecurityAccessStatus status = evaluateSecurityAccess(request);
  return {status, securityAccessHttpStatus(status),
          status == SecurityAccessStatus::Allowed, false};
}

}  // namespace security
}  // namespace voxone
