#include "security_access_policy.h"

namespace voxone {
namespace security {
namespace {

bool pathEquals(const char* path, const char* expected) {
  if (!path || !expected) return false;
  size_t index = 0;
  while (expected[index] && path[index] == expected[index]) ++index;
  return expected[index] == '\0' &&
         (path[index] == '\0' || path[index] == '?' || path[index] == '#');
}

const SecurityRouteRule kRouteRules[] = {
    {SecurityAccessMethod::Get, "/", SecurityEndpoint::Root,
     SecurityRouteClass::PublicBootstrap, false, false},
    {SecurityAccessMethod::Post, "/", SecurityEndpoint::ProvisionWifi,
     SecurityRouteClass::Provisioning, true, false},
    {SecurityAccessMethod::Get, "/voxone.html", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::PublicBootstrap, false, false},
    {SecurityAccessMethod::Get, "/voxone.css", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::PublicBootstrap, false, false},
    {SecurityAccessMethod::Get, "/voxone.js", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::PublicBootstrap, false, false},
    {SecurityAccessMethod::Get, "/advanced-audio.js",
     SecurityEndpoint::LoginAsset, SecurityRouteClass::PublicBootstrap, false,
     false},
    {SecurityAccessMethod::Get, "/dsp-client.js",
     SecurityEndpoint::LoginAsset, SecurityRouteClass::PublicBootstrap, false,
     false},
    {SecurityAccessMethod::Get, "/voxone-logo.svg",
     SecurityEndpoint::LoginAsset, SecurityRouteClass::PublicBootstrap, false,
     false},
    {SecurityAccessMethod::Get, "/favicon.ico", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::PublicBootstrap, false, false},
    // SPIFFS stores these assets compressed, but clients must use their
    // canonical URLs. AsyncStaticWebHandler may internally resolve a canonical
    // request to path + ".gz"; a direct HTTP request for the storage filename
    // is a separate route and is deliberately denied.
    {SecurityAccessMethod::Get, "/advanced-audio.js.gz",
     SecurityEndpoint::LoginAsset,
     SecurityRouteClass::RejectedDirectCompressedAsset, false, false},
    {SecurityAccessMethod::Get, "/dsp-client.js.gz",
     SecurityEndpoint::LoginAsset,
     SecurityRouteClass::RejectedDirectCompressedAsset, false, false},
    {SecurityAccessMethod::Get, "/voxone-logo.svg.gz",
     SecurityEndpoint::LoginAsset,
     SecurityRouteClass::RejectedDirectCompressedAsset, false, false},
    {SecurityAccessMethod::Get, "/voxone.css.gz",
     SecurityEndpoint::LoginAsset,
     SecurityRouteClass::RejectedDirectCompressedAsset, false, false},
    {SecurityAccessMethod::Get, "/voxone.html.gz",
     SecurityEndpoint::LoginAsset,
     SecurityRouteClass::RejectedDirectCompressedAsset, false, false},
    {SecurityAccessMethod::Get, "/voxone.js.gz",
     SecurityEndpoint::LoginAsset,
     SecurityRouteClass::RejectedDirectCompressedAsset, false, false},
    {SecurityAccessMethod::Get, "/api/security/state",
     SecurityEndpoint::SecurityState, SecurityRouteClass::SecurityApi, false,
     false},
    {SecurityAccessMethod::Post, "/api/security/setup",
     SecurityEndpoint::SecuritySetup, SecurityRouteClass::SecurityApi, true,
     false},
    {SecurityAccessMethod::Post, "/api/security/login",
     SecurityEndpoint::SecurityLogin, SecurityRouteClass::SecurityApi, true,
     false},
    {SecurityAccessMethod::Post, "/api/security/logout",
     SecurityEndpoint::SecurityLogout, SecurityRouteClass::SecurityApi, true,
     false},
    {SecurityAccessMethod::Get, "/variables.js", SecurityEndpoint::Variables,
     SecurityRouteClass::ProtectedWeb, false, false},
    {SecurityAccessMethod::Get, "/api/stations",
     SecurityEndpoint::StationsSnapshot, SecurityRouteClass::ProtectedRest,
     false, false},
    {SecurityAccessMethod::Get, "/api/directory/search",
     SecurityEndpoint::DirectorySearch, SecurityRouteClass::ProtectedRest,
     false, false},
    {SecurityAccessMethod::Get, "/api/stations/export",
     SecurityEndpoint::StationsExport, SecurityRouteClass::ProtectedRest,
     false, false},
    {SecurityAccessMethod::Post, "/api/stations/import",
     SecurityEndpoint::StationsImport, SecurityRouteClass::ProtectedRest, true,
     true},
    {SecurityAccessMethod::Post, "/api/stations/add",
     SecurityEndpoint::StationsAdd, SecurityRouteClass::ProtectedRest, true,
     false},
    {SecurityAccessMethod::Post, "/api/stations/edit",
     SecurityEndpoint::StationsEdit, SecurityRouteClass::ProtectedRest, true,
     false},
    {SecurityAccessMethod::Post, "/api/stations/delete",
     SecurityEndpoint::StationsDelete, SecurityRouteClass::ProtectedRest, true,
     false},
    {SecurityAccessMethod::Post, "/api/stations/reorder",
     SecurityEndpoint::StationsReorder, SecurityRouteClass::ProtectedRest,
     true, false},
    {SecurityAccessMethod::Post, "/api/stations/metadata",
     SecurityEndpoint::StationsMetadata, SecurityRouteClass::ProtectedRest,
     true, false},
    {SecurityAccessMethod::Get, "/api/mqtt", SecurityEndpoint::MqttRead,
     SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::Post, "/api/mqtt", SecurityEndpoint::MqttWrite,
     SecurityRouteClass::ProtectedRest, true, false},
    {SecurityAccessMethod::Get, "/api/hardware", SecurityEndpoint::HardwareRead,
     SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::Post, "/api/hardware",
     SecurityEndpoint::HardwareWrite, SecurityRouteClass::ProtectedRest, true,
     false},
    {SecurityAccessMethod::Get, "/api/config/backup",
     SecurityEndpoint::ConfigBackup, SecurityRouteClass::ProtectedRest, false,
     false},
    {SecurityAccessMethod::Post, "/api/config/restore",
     SecurityEndpoint::ConfigRestore, SecurityRouteClass::ProtectedRest, true,
     true},
    {SecurityAccessMethod::Get, "/api/time", SecurityEndpoint::TimeRead,
     SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::Post, "/api/time/sync", SecurityEndpoint::TimeSync,
     SecurityRouteClass::ProtectedRest, true, false},
    {SecurityAccessMethod::Get, "/api/ready", SecurityEndpoint::Ready,
     SecurityRouteClass::PublicBootstrap, false, false},
    {SecurityAccessMethod::Get, "/api/bt/update",
     SecurityEndpoint::BtUpdateStatus, SecurityRouteClass::ProtectedRest,
     false, false},
    {SecurityAccessMethod::Get, "/api/dsp/state", SecurityEndpoint::DspState,
     SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::Get, "/data/stations.tsv",
     SecurityEndpoint::PlaylistData, SecurityRouteClass::ProtectedRest, false,
     false},
    {SecurityAccessMethod::WebSocketHandshake, "/ws",
     SecurityEndpoint::WebSocket, SecurityRouteClass::ProtectedWebSocket,
     false, false},
    {SecurityAccessMethod::Post, "/update",
     SecurityEndpoint::MainOrSpiffsUpdate,
     SecurityRouteClass::ProtectedUpdate, true, true},
    {SecurityAccessMethod::Post, "/update/bt",
     SecurityEndpoint::BtFirmwareUpdate, SecurityRouteClass::ProtectedUpdate,
     true, true},
    {SecurityAccessMethod::Get, "/emergency",
     SecurityEndpoint::EmergencyPage, SecurityRouteClass::Recovery, false,
     false},
    {SecurityAccessMethod::Get, "/update.html", SecurityEndpoint::UpdatePage,
     SecurityRouteClass::Recovery, false, false},
    {SecurityAccessMethod::Get, "/logout",
     SecurityEndpoint::LegacyBasicLogout, SecurityRouteClass::ProtectedWeb,
     false, false},
};

bool knownPath(const char* path) {
  for (const SecurityRouteRule& rule : kRouteRules)
    if (pathEquals(path, rule.path)) return true;
  return false;
}

SecurityAccessStatus protectedAccess(const SecurityAccessRequest& request) {
  switch (request.state) {
    case SecurityState::Open:
      return SecurityAccessStatus::Allowed;
    case SecurityState::Protected:
      return request.sessionValid ? SecurityAccessStatus::Allowed
                                  : SecurityAccessStatus::AuthenticationRequired;
    case SecurityState::Locked:
      return SecurityAccessStatus::Locked;
    case SecurityState::Unconfigured:
    case SecurityState::LegacyUnclaimed:
      return SecurityAccessStatus::InvalidState;
  }
  return SecurityAccessStatus::InvalidState;
}

SecurityAccessStatus updateAccess(const SecurityAccessRequest& request) {
  switch (request.state) {
    case SecurityState::Open:
    case SecurityState::LegacyUnclaimed:
      return SecurityAccessStatus::Allowed;
    case SecurityState::Protected:
      return request.sessionValid ? SecurityAccessStatus::Allowed
                                  : SecurityAccessStatus::AuthenticationRequired;
    case SecurityState::Unconfigured:
      return request.recoveryMode ? SecurityAccessStatus::Allowed
                                  : SecurityAccessStatus::InvalidState;
    case SecurityState::Locked:
      return SecurityAccessStatus::Locked;
  }
  return SecurityAccessStatus::InvalidState;
}

SecurityAccessStatus securityApiAccess(SecurityEndpoint endpoint,
                                       const SecurityAccessRequest& request) {
  switch (endpoint) {
    case SecurityEndpoint::SecurityState:
      return SecurityAccessStatus::Allowed;
    case SecurityEndpoint::SecuritySetup:
      return request.state == SecurityState::LegacyUnclaimed
                 ? SecurityAccessStatus::Allowed
                 : (request.state == SecurityState::Locked
                        ? SecurityAccessStatus::Locked
                        : SecurityAccessStatus::InvalidState);
    case SecurityEndpoint::SecurityLogin:
      return request.state == SecurityState::Protected
                 ? SecurityAccessStatus::Allowed
                 : (request.state == SecurityState::Locked
                        ? SecurityAccessStatus::Locked
                        : SecurityAccessStatus::InvalidState);
    case SecurityEndpoint::SecurityLogout:
      return request.state == SecurityState::Locked
                 ? SecurityAccessStatus::Locked
                 : SecurityAccessStatus::Allowed;
    default:
      return SecurityAccessStatus::UnknownRoute;
  }
}

}  // namespace

bool securityAccessMethodMutates(SecurityAccessMethod method) {
  return method == SecurityAccessMethod::Post ||
         method == SecurityAccessMethod::Put ||
         method == SecurityAccessMethod::Delete ||
         method == SecurityAccessMethod::Patch;
}

bool securityAccessRequiresCsrfProtection(SecurityAccessMethod method) {
  return securityAccessMethodMutates(method);
}

bool securityAccessRequiresOrigin(SecurityAccessMethod method) {
  return securityAccessRequiresCsrfProtection(method) ||
         method == SecurityAccessMethod::WebSocketHandshake;
}

SecurityRouteMatch classifySecurityRoute(SecurityAccessMethod method,
                                         const char* path) {
  for (const SecurityRouteRule& rule : kRouteRules) {
    if (rule.method == method && pathEquals(path, rule.path))
      return {rule.endpoint, rule.routeClass, rule.mutating, rule.multipart,
              true};
  }

  return {SecurityEndpoint::Unknown, SecurityRouteClass::Unknown,
          securityAccessMethodMutates(method), false, knownPath(path)};
}

const SecurityRouteRule* securityAccessRouteRules(size_t& count) {
  count = sizeof(kRouteRules) / sizeof(kRouteRules[0]);
  return kRouteRules;
}

SecurityAccessStatus evaluateSecurityAccess(
    const SecurityAccessRequest& request) {
  const SecurityRouteMatch route =
      classifySecurityRoute(request.method, request.path);
  if (!route.pathKnown)
    return SecurityAccessStatus::UnknownRoute;
  if (route.endpoint == SecurityEndpoint::Unknown)
    return SecurityAccessStatus::MethodNotAllowed;
  if (!request.hostValid) return SecurityAccessStatus::InvalidHost;
  if (securityAccessRequiresOrigin(request.method)) {
    if (!request.originPresent || !request.originValid)
      return SecurityAccessStatus::InvalidOrigin;
  } else if (request.originPresent && !request.originValid) {
    return SecurityAccessStatus::InvalidOrigin;
  }

  switch (route.routeClass) {
    case SecurityRouteClass::PublicBootstrap:
      // The first AP is Wi-Fi provisioning only. Its embedded root page does
      // not need the normal WWW assets; those become public only once Wi-Fi
      // has moved the device out of Unconfigured.
      if (route.endpoint == SecurityEndpoint::LoginAsset &&
          request.state == SecurityState::Unconfigured)
        return SecurityAccessStatus::InvalidState;
      return SecurityAccessStatus::Allowed;
    case SecurityRouteClass::Provisioning:
      return request.state == SecurityState::Unconfigured &&
                     request.provisioningMode
                 ? SecurityAccessStatus::Allowed
                 : (request.state == SecurityState::Locked
                        ? SecurityAccessStatus::Locked
                        : SecurityAccessStatus::InvalidState);
    case SecurityRouteClass::SecurityApi:
      return securityApiAccess(route.endpoint, request);
    case SecurityRouteClass::ProtectedWeb:
    case SecurityRouteClass::ProtectedRest:
    case SecurityRouteClass::ProtectedWebSocket:
      return protectedAccess(request);
    case SecurityRouteClass::ProtectedUpdate:
      return updateAccess(request);
    case SecurityRouteClass::Recovery:
      if (request.state == SecurityState::Unconfigured &&
          !request.recoveryMode)
        return SecurityAccessStatus::InvalidState;
      return updateAccess(request);
    case SecurityRouteClass::RejectedDirectCompressedAsset:
      return SecurityAccessStatus::DirectCompressedAssetDenied;
    case SecurityRouteClass::Unknown:
      return SecurityAccessStatus::UnknownRoute;
  }
  return SecurityAccessStatus::UnknownRoute;
}

const char* securityEndpointName(SecurityEndpoint endpoint) {
  switch (endpoint) {
    case SecurityEndpoint::Root: return "root";
    case SecurityEndpoint::LoginAsset: return "login-asset";
    case SecurityEndpoint::ProvisionWifi: return "provision-wifi";
    case SecurityEndpoint::SecurityState: return "security-state";
    case SecurityEndpoint::SecuritySetup: return "security-setup";
    case SecurityEndpoint::SecurityLogin: return "security-login";
    case SecurityEndpoint::SecurityLogout: return "security-logout";
    case SecurityEndpoint::Variables: return "variables";
    case SecurityEndpoint::StationsSnapshot: return "stations-snapshot";
    case SecurityEndpoint::DirectorySearch: return "directory-search";
    case SecurityEndpoint::StationsExport: return "stations-export";
    case SecurityEndpoint::StationsImport: return "stations-import";
    case SecurityEndpoint::StationsAdd: return "stations-add";
    case SecurityEndpoint::StationsEdit: return "stations-edit";
    case SecurityEndpoint::StationsDelete: return "stations-delete";
    case SecurityEndpoint::StationsReorder: return "stations-reorder";
    case SecurityEndpoint::StationsMetadata: return "stations-metadata";
    case SecurityEndpoint::MqttRead: return "mqtt-read";
    case SecurityEndpoint::MqttWrite: return "mqtt-write";
    case SecurityEndpoint::HardwareRead: return "hardware-read";
    case SecurityEndpoint::HardwareWrite: return "hardware-write";
    case SecurityEndpoint::ConfigBackup: return "config-backup";
    case SecurityEndpoint::ConfigRestore: return "config-restore";
    case SecurityEndpoint::TimeRead: return "time-read";
    case SecurityEndpoint::TimeSync: return "time-sync";
    case SecurityEndpoint::Ready: return "ready";
    case SecurityEndpoint::BtUpdateStatus: return "bt-update-status";
    case SecurityEndpoint::DspState: return "dsp-state";
    case SecurityEndpoint::PlaylistData: return "playlist-data";
    case SecurityEndpoint::WebSocket: return "websocket";
    case SecurityEndpoint::MainOrSpiffsUpdate: return "main-spiffs-update";
    case SecurityEndpoint::BtFirmwareUpdate: return "bt-firmware-update";
    case SecurityEndpoint::EmergencyPage: return "emergency-page";
    case SecurityEndpoint::UpdatePage: return "update-page";
    case SecurityEndpoint::LegacyBasicLogout: return "legacy-basic-logout";
    case SecurityEndpoint::Unknown: return "unknown";
  }
  return "unknown";
}

const char* securityRouteClassName(SecurityRouteClass routeClass) {
  switch (routeClass) {
    case SecurityRouteClass::PublicBootstrap: return "public-bootstrap";
    case SecurityRouteClass::Provisioning: return "provisioning";
    case SecurityRouteClass::SecurityApi: return "security-api";
    case SecurityRouteClass::ProtectedWeb: return "protected-web";
    case SecurityRouteClass::ProtectedRest: return "protected-rest";
    case SecurityRouteClass::ProtectedWebSocket: return "protected-websocket";
    case SecurityRouteClass::ProtectedUpdate: return "protected-update";
    case SecurityRouteClass::Recovery: return "recovery";
    case SecurityRouteClass::RejectedDirectCompressedAsset:
      return "rejected-direct-compressed-asset";
    case SecurityRouteClass::Unknown: return "unknown";
  }
  return "unknown";
}

const char* securityAccessStatusName(SecurityAccessStatus status) {
  switch (status) {
    case SecurityAccessStatus::Allowed: return "allowed";
    case SecurityAccessStatus::InvalidHost: return "invalid-host";
    case SecurityAccessStatus::InvalidOrigin: return "invalid-origin";
    case SecurityAccessStatus::AuthenticationRequired:
      return "authentication-required";
    case SecurityAccessStatus::InvalidState: return "invalid-state";
    case SecurityAccessStatus::Locked: return "locked";
    case SecurityAccessStatus::MethodNotAllowed: return "method-not-allowed";
    case SecurityAccessStatus::DirectCompressedAssetDenied:
      return "direct-compressed-asset-denied";
    case SecurityAccessStatus::UnknownRoute: return "unknown-route";
  }
  return "unknown";
}

}  // namespace security
}  // namespace voxone
