#ifndef VOXONE_SECURITY_ACCESS_POLICY_H
#define VOXONE_SECURITY_ACCESS_POLICY_H

#include "security_settings.h"

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace security {

// Transport-neutral request description. The HTTP/WebSocket adapter is
// responsible only for parsing the request and validating Host/Origin; this
// policy owns the authorization decision.
enum class SecurityAccessMethod : uint8_t {
  Get,
  Post,
  Put,
  Delete,
  Patch,
  Head,
  Options,
  WebSocketHandshake,
  Unknown
};

enum class SecurityEndpoint : uint8_t {
  Root,
  LoginAsset,
  ProvisionWifi,
  SecurityState,
  SecuritySetup,
  SecurityLogin,
  SecurityLogout,
  Variables,
  StationsSnapshot,
  DirectorySearch,
  StationsExport,
  StationsImport,
  StationsAdd,
  StationsEdit,
  StationsDelete,
  StationsReorder,
  StationsMetadata,
  MqttRead,
  MqttWrite,
  HardwareRead,
  HardwareWrite,
  ConfigBackup,
  ConfigRestore,
  TimeRead,
  TimeSync,
  Ready,
  BtUpdateStatus,
  DspState,
  PlaylistData,
  WebSocket,
  MainOrSpiffsUpdate,
  BtFirmwareUpdate,
  EmergencyPage,
  UpdatePage,
  LegacyBasicLogout,
  Unknown
};

enum class SecurityRouteClass : uint8_t {
  PublicBootstrap,
  Provisioning,
  SecurityApi,
  ProtectedWeb,
  ProtectedRest,
  ProtectedWebSocket,
  ProtectedUpdate,
  Recovery,
  RejectedDirectCompressedAsset,
  Unknown
};

enum class SecurityAccessStatus : uint8_t {
  Allowed,
  InvalidHost,
  InvalidOrigin,
  AuthenticationRequired,
  InvalidState,
  Locked,
  InvalidHandshake,
  MethodNotAllowed,
  DirectCompressedAssetDenied,
  UnknownRoute
};

struct SecurityRouteMatch {
  SecurityEndpoint endpoint;
  SecurityRouteClass routeClass;
  bool mutating;
  bool multipart;
  bool pathKnown;
};

// Canonical inventory of routes known to the policy. Keeping the method and
// path table here (rather than in a concrete HTTP handler) lets adapters share
// one fail-closed classification and lets native tests compare it with the
// routes registered by the firmware.
struct SecurityRouteRule {
  SecurityAccessMethod method;
  const char* path;
  SecurityEndpoint endpoint;
  SecurityRouteClass routeClass;
  bool mutating;
  bool multipart;
};

struct SecurityAccessRequest {
  SecurityState state;
  SecurityAccessMethod method;
  const char* path;
  bool sessionValid;
  bool hostValid;
  bool originPresent;
  bool originValid;
  bool provisioningMode;
  bool recoveryMode;
};

SecurityRouteMatch classifySecurityRoute(SecurityAccessMethod method,
                                         const char* path);
const SecurityRouteRule* securityAccessRouteRules(size_t& count);
SecurityAccessStatus evaluateSecurityAccess(
    const SecurityAccessRequest& request);

bool securityAccessMethodMutates(SecurityAccessMethod method);
// Mutating HTTP requests use a mandatory trusted same-origin proof as the
// CSRF control. WebSocket handshakes are not mutations themselves, but also
// require a trusted Origin before a session may be accepted.
bool securityAccessRequiresCsrfProtection(SecurityAccessMethod method);
bool securityAccessRequiresOrigin(SecurityAccessMethod method);
const char* securityEndpointName(SecurityEndpoint endpoint);
const char* securityRouteClassName(SecurityRouteClass routeClass);
const char* securityAccessStatusName(SecurityAccessStatus status);

}  // namespace security
}  // namespace voxone

#endif
