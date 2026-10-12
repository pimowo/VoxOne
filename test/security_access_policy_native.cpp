#include "../src/core/security_access_policy.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

using namespace voxone::security;

namespace {

struct EndpointCase {
  SecurityAccessMethod method;
  const char* path;
  SecurityEndpoint endpoint;
  SecurityRouteClass routeClass;
  bool mutating;
  bool multipart;
};

const EndpointCase kEndpoints[] = {
    {SecurityAccessMethod::Get, "/", SecurityEndpoint::Root,
     SecurityRouteClass::PublicBootstrap, false, false},
    {SecurityAccessMethod::Get, "/voxone.html", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::PublicBootstrap, false, false},
    {SecurityAccessMethod::Get, "/voxone.css?v=1", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::PublicBootstrap, false, false},
    {SecurityAccessMethod::Get, "/voxone.js", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::PublicBootstrap, false, false},
    {SecurityAccessMethod::Get, "/advanced-audio.js", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::PublicBootstrap, false, false},
    {SecurityAccessMethod::Get, "/dsp-client.js", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::PublicBootstrap, false, false},
    {SecurityAccessMethod::Get, "/voxone-logo.svg", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::PublicBootstrap, false, false},
    {SecurityAccessMethod::Get, "/favicon.ico", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::PublicBootstrap, false, false},
    {SecurityAccessMethod::Get, "/advanced-audio.js.gz",
     SecurityEndpoint::LoginAsset,
     SecurityRouteClass::RejectedDirectCompressedAsset, false, false},
    {SecurityAccessMethod::Get, "/dsp-client.js.gz", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::RejectedDirectCompressedAsset, false, false},
    {SecurityAccessMethod::Get, "/voxone-logo.svg.gz",
     SecurityEndpoint::LoginAsset,
     SecurityRouteClass::RejectedDirectCompressedAsset, false, false},
    {SecurityAccessMethod::Get, "/voxone.css.gz", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::RejectedDirectCompressedAsset, false, false},
    {SecurityAccessMethod::Get, "/voxone.html.gz", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::RejectedDirectCompressedAsset, false, false},
    {SecurityAccessMethod::Get, "/voxone.js.gz", SecurityEndpoint::LoginAsset,
     SecurityRouteClass::RejectedDirectCompressedAsset, false, false},
    {SecurityAccessMethod::Post, "/", SecurityEndpoint::ProvisionWifi,
     SecurityRouteClass::Provisioning, true, false},
    {SecurityAccessMethod::Get, "/api/security/state", SecurityEndpoint::SecurityState,
     SecurityRouteClass::SecurityApi, false, false},
    {SecurityAccessMethod::Post, "/api/security/setup", SecurityEndpoint::SecuritySetup,
     SecurityRouteClass::SecurityApi, true, false},
    {SecurityAccessMethod::Post, "/api/security/login", SecurityEndpoint::SecurityLogin,
     SecurityRouteClass::SecurityApi, true, false},
    {SecurityAccessMethod::Post, "/api/security/logout", SecurityEndpoint::SecurityLogout,
     SecurityRouteClass::SecurityApi, true, false},
    {SecurityAccessMethod::Get, "/variables.js", SecurityEndpoint::Variables,
     SecurityRouteClass::ProtectedWeb, false, false},
    {SecurityAccessMethod::Get, "/api/stations", SecurityEndpoint::StationsSnapshot,
     SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::Get, "/api/directory/search?country=PL&q=radio",
     SecurityEndpoint::DirectorySearch, SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::Get, "/api/stations/export", SecurityEndpoint::StationsExport,
     SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::Post, "/api/stations/import", SecurityEndpoint::StationsImport,
     SecurityRouteClass::ProtectedRest, true, true},
    {SecurityAccessMethod::Post, "/api/stations/add", SecurityEndpoint::StationsAdd,
     SecurityRouteClass::ProtectedRest, true, false},
    {SecurityAccessMethod::Post, "/api/stations/edit", SecurityEndpoint::StationsEdit,
     SecurityRouteClass::ProtectedRest, true, false},
    {SecurityAccessMethod::Post, "/api/stations/delete", SecurityEndpoint::StationsDelete,
     SecurityRouteClass::ProtectedRest, true, false},
    {SecurityAccessMethod::Post, "/api/stations/reorder", SecurityEndpoint::StationsReorder,
     SecurityRouteClass::ProtectedRest, true, false},
    {SecurityAccessMethod::Post, "/api/stations/metadata", SecurityEndpoint::StationsMetadata,
     SecurityRouteClass::ProtectedRest, true, false},
    {SecurityAccessMethod::Get, "/api/mqtt", SecurityEndpoint::MqttRead,
     SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::Post, "/api/mqtt", SecurityEndpoint::MqttWrite,
     SecurityRouteClass::ProtectedRest, true, false},
    {SecurityAccessMethod::Get, "/api/hardware", SecurityEndpoint::HardwareRead,
     SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::Post, "/api/hardware", SecurityEndpoint::HardwareWrite,
     SecurityRouteClass::ProtectedRest, true, false},
    {SecurityAccessMethod::Get, "/api/config/backup", SecurityEndpoint::ConfigBackup,
     SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::Post, "/api/config/restore", SecurityEndpoint::ConfigRestore,
     SecurityRouteClass::ProtectedRest, true, true},
    {SecurityAccessMethod::Get, "/api/time", SecurityEndpoint::TimeRead,
     SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::Post, "/api/time/sync", SecurityEndpoint::TimeSync,
     SecurityRouteClass::ProtectedRest, true, false},
    {SecurityAccessMethod::Get, "/api/ready", SecurityEndpoint::Ready,
     SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::Get, "/api/bt/update", SecurityEndpoint::BtUpdateStatus,
     SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::Get, "/api/dsp/state", SecurityEndpoint::DspState,
     SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::Get, "/data/stations.tsv", SecurityEndpoint::PlaylistData,
     SecurityRouteClass::ProtectedRest, false, false},
    {SecurityAccessMethod::WebSocketHandshake, "/ws", SecurityEndpoint::WebSocket,
     SecurityRouteClass::ProtectedWebSocket, false, false},
    {SecurityAccessMethod::Post, "/update", SecurityEndpoint::MainOrSpiffsUpdate,
     SecurityRouteClass::ProtectedUpdate, true, true},
    {SecurityAccessMethod::Post, "/update/bt", SecurityEndpoint::BtFirmwareUpdate,
     SecurityRouteClass::ProtectedUpdate, true, true},
    {SecurityAccessMethod::Get, "/emergency", SecurityEndpoint::EmergencyPage,
     SecurityRouteClass::Recovery, false, false},
    {SecurityAccessMethod::Get, "/update.html", SecurityEndpoint::UpdatePage,
     SecurityRouteClass::Recovery, false, false},
    {SecurityAccessMethod::Get, "/logout", SecurityEndpoint::LegacyBasicLogout,
     SecurityRouteClass::ProtectedWeb, false, false},
};

SecurityAccessRequest request(SecurityState state, SecurityAccessMethod method,
                              const char* path) {
  return {state, method, path, false, true,
          securityAccessRequiresOrigin(method), true, false, false};
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

SecurityAccessStatus expectedForTrustedRequest(const EndpointCase& endpoint,
                                               SecurityState state,
                                               bool sessionValid) {
  switch (endpoint.routeClass) {
    case SecurityRouteClass::PublicBootstrap:
      if (endpoint.endpoint == SecurityEndpoint::LoginAsset &&
          state == SecurityState::Unconfigured)
        return SecurityAccessStatus::InvalidState;
      return SecurityAccessStatus::Allowed;
    case SecurityRouteClass::Provisioning:
      return state == SecurityState::Unconfigured
                 ? SecurityAccessStatus::Allowed
                 : (state == SecurityState::Locked
                        ? SecurityAccessStatus::Locked
                        : SecurityAccessStatus::InvalidState);
    case SecurityRouteClass::SecurityApi:
      switch (endpoint.endpoint) {
        case SecurityEndpoint::SecurityState:
          return SecurityAccessStatus::Allowed;
        case SecurityEndpoint::SecuritySetup:
          return state == SecurityState::LegacyUnclaimed
                     ? SecurityAccessStatus::Allowed
                     : (state == SecurityState::Locked
                            ? SecurityAccessStatus::Locked
                            : SecurityAccessStatus::InvalidState);
        case SecurityEndpoint::SecurityLogin:
          return state == SecurityState::Protected
                     ? SecurityAccessStatus::Allowed
                     : (state == SecurityState::Locked
                            ? SecurityAccessStatus::Locked
                            : SecurityAccessStatus::InvalidState);
        case SecurityEndpoint::SecurityLogout:
          return state == SecurityState::Locked
                     ? SecurityAccessStatus::Locked
                     : SecurityAccessStatus::Allowed;
        default:
          assert(false);
      }
      break;
    case SecurityRouteClass::ProtectedWeb:
    case SecurityRouteClass::ProtectedRest:
    case SecurityRouteClass::ProtectedWebSocket:
      if (state == SecurityState::Open) return SecurityAccessStatus::Allowed;
      if (state == SecurityState::Protected)
        return sessionValid ? SecurityAccessStatus::Allowed
                            : SecurityAccessStatus::AuthenticationRequired;
      return state == SecurityState::Locked ? SecurityAccessStatus::Locked
                                            : SecurityAccessStatus::InvalidState;
    case SecurityRouteClass::ProtectedUpdate:
    case SecurityRouteClass::Recovery:
      if (state == SecurityState::Open ||
          state == SecurityState::LegacyUnclaimed ||
          state == SecurityState::Unconfigured)
        return SecurityAccessStatus::Allowed;
      if (state == SecurityState::Protected)
        return sessionValid ? SecurityAccessStatus::Allowed
                            : SecurityAccessStatus::AuthenticationRequired;
      return SecurityAccessStatus::Locked;
    case SecurityRouteClass::RejectedDirectCompressedAsset:
      return SecurityAccessStatus::DirectCompressedAssetDenied;
    case SecurityRouteClass::Unknown:
      return SecurityAccessStatus::UnknownRoute;
  }
  return SecurityAccessStatus::UnknownRoute;
}

SecurityAccessStatus expectedForTrustedRequest(const SecurityRouteMatch& route,
                                               SecurityState state,
                                               bool sessionValid) {
  const EndpointCase endpoint = {SecurityAccessMethod::Unknown, "",
                                 route.endpoint, route.routeClass,
                                 route.mutating, route.multipart};
  return expectedForTrustedRequest(endpoint, state, sessionValid);
}

bool sameCanonicalPath(const char* left, const char* right) {
  if (!left || !right) return false;
  size_t index = 0;
  while (left[index] && left[index] != '?' && left[index] != '#' &&
         right[index] && right[index] != '?' && right[index] != '#') {
    if (left[index] != right[index]) return false;
    ++index;
  }
  const bool leftEnded = left[index] == '\0' || left[index] == '?' ||
                         left[index] == '#';
  const bool rightEnded = right[index] == '\0' || right[index] == '?' ||
                          right[index] == '#';
  return leftEnded && rightEnded;
}

void testCompleteEndpointInventory() {
  for (const EndpointCase& expected : kEndpoints) {
    const SecurityRouteMatch actual =
        classifySecurityRoute(expected.method, expected.path);
    assert(actual.pathKnown);
    assert(actual.endpoint == expected.endpoint);
    assert(actual.routeClass == expected.routeClass);
    assert(actual.mutating == expected.mutating);
    assert(actual.multipart == expected.multipart);
    assert(std::strcmp(securityEndpointName(actual.endpoint), "unknown") != 0);
    assert(std::strcmp(securityRouteClassName(actual.routeClass), "unknown") != 0);
  }
  assert(sizeof(kEndpoints) / sizeof(kEndpoints[0]) == 47);

  size_t ruleCount = 0;
  const SecurityRouteRule* rules = securityAccessRouteRules(ruleCount);
  assert(rules != nullptr && ruleCount == 47);
  for (size_t index = 0; index < ruleCount; ++index) {
    const SecurityRouteRule& rule = rules[index];
    assert(rule.path && rule.path[0] == '/');
    assert(rule.endpoint != SecurityEndpoint::Unknown);
    assert(rule.routeClass != SecurityRouteClass::Unknown);
    assert(rule.mutating == securityAccessMethodMutates(rule.method));
    assert(rule.mutating ==
           securityAccessRequiresCsrfProtection(rule.method));
    size_t expectedMatches = 0;
    for (const EndpointCase& expected : kEndpoints) {
      if (expected.method == rule.method &&
          sameCanonicalPath(expected.path, rule.path) &&
          expected.endpoint == rule.endpoint &&
          expected.routeClass == rule.routeClass &&
          expected.mutating == rule.mutating &&
          expected.multipart == rule.multipart)
        ++expectedMatches;
    }
    assert(expectedMatches == 1);
    for (size_t other = index + 1; other < ruleCount; ++other)
      assert(rule.method != rules[other].method ||
             !sameCanonicalPath(rule.path, rules[other].path));
  }
}

void testHostOriginAndCsrfMatrix() {
  assert(securityAccessRequiresOrigin(
      SecurityAccessMethod::WebSocketHandshake));
  assert(!securityAccessRequiresCsrfProtection(
      SecurityAccessMethod::WebSocketHandshake));
  for (const EndpointCase& endpoint : kEndpoints) {
    SecurityAccessRequest candidate =
        request(SecurityState::Open, endpoint.method, endpoint.path);
    candidate.provisioningMode = true;
    candidate.recoveryMode = true;
    if (endpoint.endpoint == SecurityEndpoint::ProvisionWifi)
      candidate.state = SecurityState::Unconfigured;
    if (endpoint.endpoint == SecurityEndpoint::SecuritySetup)
      candidate.state = SecurityState::LegacyUnclaimed;
    if (endpoint.endpoint == SecurityEndpoint::SecurityLogin)
      candidate.state = SecurityState::Protected;

    candidate.hostValid = false;
    assert(evaluateSecurityAccess(candidate) == SecurityAccessStatus::InvalidHost);
    candidate.hostValid = true;
    candidate.originPresent = true;
    candidate.originValid = false;
    assert(evaluateSecurityAccess(candidate) == SecurityAccessStatus::InvalidOrigin);
    candidate.originPresent = false;
    candidate.originValid = false;
    if (securityAccessRequiresOrigin(endpoint.method)) {
      assert(evaluateSecurityAccess(candidate) == SecurityAccessStatus::InvalidOrigin);
    } else {
      assert(evaluateSecurityAccess(candidate) ==
             expectedForTrustedRequest(endpoint, candidate.state,
                                       candidate.sessionValid));
    }
  }
}

void testDirectCompressedAssetsAndCanonicalFallback() {
  const char* canonicalAssets[] = {
      "/advanced-audio.js", "/dsp-client.js", "/voxone-logo.svg",
      "/voxone.css", "/voxone.html", "/voxone.js"};
  const char* directCompressedAssets[] = {
      "/advanced-audio.js.gz", "/dsp-client.js.gz",
      "/voxone-logo.svg.gz", "/voxone.css.gz", "/voxone.html.gz",
      "/voxone.js.gz"};

  for (size_t index = 0;
       index < sizeof(canonicalAssets) / sizeof(canonicalAssets[0]); ++index) {
    SecurityAccessRequest canonical = request(
        SecurityState::Open, SecurityAccessMethod::Get, canonicalAssets[index]);
    assert(evaluateSecurityAccess(canonical) == SecurityAccessStatus::Allowed);

    SecurityAccessRequest direct = request(
        SecurityState::Open, SecurityAccessMethod::Get,
        directCompressedAssets[index]);
    assert(evaluateSecurityAccess(direct) ==
           SecurityAccessStatus::DirectCompressedAssetDenied);
    direct.path = index == 0 ? "/advanced-audio.js.gz?cache=1"
                             : directCompressedAssets[index];
    assert(evaluateSecurityAccess(direct) ==
           SecurityAccessStatus::DirectCompressedAssetDenied);

    for (SecurityAccessMethod method : {
             SecurityAccessMethod::Post, SecurityAccessMethod::Put,
             SecurityAccessMethod::Delete, SecurityAccessMethod::Patch,
             SecurityAccessMethod::Head, SecurityAccessMethod::Options}) {
      direct.method = method;
      direct.path = directCompressedAssets[index];
      direct.originPresent = securityAccessRequiresOrigin(method);
      direct.originValid = true;
      assert(evaluateSecurityAccess(direct) ==
             SecurityAccessStatus::MethodNotAllowed);
    }
  }

  const char* bypassAttempts[] = {
      "/voxone.js%2egz", "/voxone.js%2Egz", "/voxone.js%252egz",
      "/voxone.js.GZ", "/voxone.js.gz/", "/./voxone.js.gz",
      "//voxone.js.gz", "/assets/../voxone.js.gz", "/unknown.gz"};
  for (const char* path : bypassAttempts) {
    SecurityAccessRequest candidate =
        request(SecurityState::Open, SecurityAccessMethod::Get, path);
    assert(evaluateSecurityAccess(candidate) ==
           SecurityAccessStatus::UnknownRoute);
  }
  SecurityAccessRequest unknownCompressedMutation = request(
      SecurityState::Open, SecurityAccessMethod::Post, "/unknown.gz");
  assert(evaluateSecurityAccess(unknownCompressedMutation) ==
         SecurityAccessStatus::UnknownRoute);

  // The policy sees the client URL, not the SPIFFS filename selected by the
  // static handler. Preserve the library's canonical path -> path + ".gz"
  // fallback and its Content-Encoding response behavior.
  const std::string staticHandler =
      readFile("src/AsyncWebServer/WebHandlers.cpp");
  const std::string responses =
      readFile("src/AsyncWebServer/WebResponses.cpp");
  assert(staticHandler.find("String gzip = path + \".gz\";") !=
         std::string::npos);
  assert(staticHandler.find("request->_tempFile = _fs.open(gzip, \"r\")") !=
         std::string::npos);
  assert(responses.find("addHeader(\"Content-Encoding\", \"gzip\")") !=
         std::string::npos);
}

void testCompleteMethodStateSessionHostOriginMatrix() {
  const SecurityState states[] = {
      SecurityState::Unconfigured, SecurityState::LegacyUnclaimed,
      SecurityState::Protected, SecurityState::Open, SecurityState::Locked};
  const SecurityAccessMethod methods[] = {
      SecurityAccessMethod::Get,
      SecurityAccessMethod::Post,
      SecurityAccessMethod::Put,
      SecurityAccessMethod::Delete,
      SecurityAccessMethod::Patch,
      SecurityAccessMethod::Head,
      SecurityAccessMethod::Options,
      SecurityAccessMethod::WebSocketHandshake,
      SecurityAccessMethod::Unknown,
  };
  for (const EndpointCase& endpoint : kEndpoints) {
    for (SecurityAccessMethod method : methods) {
      const SecurityRouteMatch route =
          classifySecurityRoute(method, endpoint.path);
      assert(route.pathKnown);
      for (SecurityState state : states) {
        for (bool sessionValid : {false, true}) {
          for (bool hostValid : {false, true}) {
            for (uint8_t originCase = 0; originCase < 3; ++originCase) {
              SecurityAccessRequest candidate =
                  request(state, method, endpoint.path);
              candidate.sessionValid = sessionValid;
              candidate.hostValid = hostValid;
              candidate.originPresent = originCase != 0;
              candidate.originValid = originCase == 1;
              candidate.provisioningMode = true;
              candidate.recoveryMode = true;

              SecurityAccessStatus expected;
              if (route.endpoint == SecurityEndpoint::Unknown) {
                expected = SecurityAccessStatus::MethodNotAllowed;
              } else if (!hostValid) {
                expected = SecurityAccessStatus::InvalidHost;
              } else if ((securityAccessRequiresOrigin(method) &&
                          originCase != 1) ||
                         (!securityAccessRequiresOrigin(method) &&
                          originCase == 2)) {
                expected = SecurityAccessStatus::InvalidOrigin;
              } else {
                expected = expectedForTrustedRequest(route, state,
                                                     sessionValid);
              }
              assert(evaluateSecurityAccess(candidate) == expected);
            }
          }
        }
      }
    }
  }
}

void testProtectedOpenAndLockedPolicy() {
  const EndpointCase protectedCases[] = {
      {SecurityAccessMethod::Get, "/variables.js", SecurityEndpoint::Variables,
       SecurityRouteClass::ProtectedWeb, false, false},
      {SecurityAccessMethod::Get, "/api/stations", SecurityEndpoint::StationsSnapshot,
       SecurityRouteClass::ProtectedRest, false, false},
      {SecurityAccessMethod::Post, "/api/mqtt", SecurityEndpoint::MqttWrite,
       SecurityRouteClass::ProtectedRest, true, false},
      {SecurityAccessMethod::WebSocketHandshake, "/ws", SecurityEndpoint::WebSocket,
       SecurityRouteClass::ProtectedWebSocket, false, false},
  };
  for (const EndpointCase& endpoint : protectedCases) {
    SecurityAccessRequest candidate =
        request(SecurityState::Protected, endpoint.method, endpoint.path);
    assert(evaluateSecurityAccess(candidate) ==
           SecurityAccessStatus::AuthenticationRequired);
    candidate.sessionValid = true;
    assert(evaluateSecurityAccess(candidate) == SecurityAccessStatus::Allowed);
    candidate.state = SecurityState::Open;
    candidate.sessionValid = false;
    assert(evaluateSecurityAccess(candidate) == SecurityAccessStatus::Allowed);
    candidate.state = SecurityState::LegacyUnclaimed;
    assert(evaluateSecurityAccess(candidate) == SecurityAccessStatus::InvalidState);
    candidate.state = SecurityState::Unconfigured;
    assert(evaluateSecurityAccess(candidate) == SecurityAccessStatus::InvalidState);
    candidate.state = SecurityState::Locked;
    assert(evaluateSecurityAccess(candidate) == SecurityAccessStatus::Locked);
  }

  SecurityAccessRequest bootstrap = request(
      SecurityState::Locked, SecurityAccessMethod::Get, "/voxone.html");
  assert(evaluateSecurityAccess(bootstrap) == SecurityAccessStatus::Allowed);
  SecurityAccessRequest state = request(
      SecurityState::Locked, SecurityAccessMethod::Get, "/api/security/state");
  assert(evaluateSecurityAccess(state) == SecurityAccessStatus::Allowed);
}

void testProvisioningMigrationRecoveryAndOtaRestart() {
  SecurityAccessRequest provisioning = request(
      SecurityState::Unconfigured, SecurityAccessMethod::Post, "/");
  assert(evaluateSecurityAccess(provisioning) == SecurityAccessStatus::InvalidState);
  provisioning.provisioningMode = true;
  assert(evaluateSecurityAccess(provisioning) == SecurityAccessStatus::Allowed);
  SecurityAccessRequest firstApRoot = request(
      SecurityState::Unconfigured, SecurityAccessMethod::Get, "/");
  firstApRoot.provisioningMode = true;
  assert(evaluateSecurityAccess(firstApRoot) == SecurityAccessStatus::Allowed);
  SecurityAccessRequest firstApPanel = request(
      SecurityState::Unconfigured, SecurityAccessMethod::Get, "/voxone.html");
  firstApPanel.provisioningMode = true;
  assert(evaluateSecurityAccess(firstApPanel) ==
         SecurityAccessStatus::InvalidState);

  SecurityAccessRequest setup = request(
      SecurityState::LegacyUnclaimed, SecurityAccessMethod::Post,
      "/api/security/setup");
  assert(evaluateSecurityAccess(setup) == SecurityAccessStatus::Allowed);
  setup.state = SecurityState::Open;
  assert(evaluateSecurityAccess(setup) == SecurityAccessStatus::InvalidState);
  setup.state = SecurityState::Protected;
  assert(evaluateSecurityAccess(setup) == SecurityAccessStatus::InvalidState);

  SecurityAccessRequest legacyUpdate = request(
      SecurityState::LegacyUnclaimed, SecurityAccessMethod::Post, "/update");
  assert(evaluateSecurityAccess(legacyUpdate) == SecurityAccessStatus::Allowed);
  SecurityAccessRequest legacyData = request(
      SecurityState::LegacyUnclaimed, SecurityAccessMethod::Get, "/api/stations");
  assert(evaluateSecurityAccess(legacyData) == SecurityAccessStatus::InvalidState);

  SecurityAccessRequest recovery = request(
      SecurityState::Unconfigured, SecurityAccessMethod::Get, "/emergency");
  assert(evaluateSecurityAccess(recovery) == SecurityAccessStatus::InvalidState);
  recovery.recoveryMode = true;
  assert(evaluateSecurityAccess(recovery) == SecurityAccessStatus::Allowed);

  SecurityAccessRequest update = request(
      SecurityState::Protected, SecurityAccessMethod::Post, "/update");
  update.sessionValid = true;
  assert(evaluateSecurityAccess(update) == SecurityAccessStatus::Allowed);
  SecurityAccessRequest afterRestart = request(
      SecurityState::Protected, SecurityAccessMethod::Get, "/api/ready");
  assert(evaluateSecurityAccess(afterRestart) ==
         SecurityAccessStatus::AuthenticationRequired);
  SecurityAccessRequest login = request(
      SecurityState::Protected, SecurityAccessMethod::Post,
      "/api/security/login");
  assert(evaluateSecurityAccess(login) == SecurityAccessStatus::Allowed);
  afterRestart.sessionValid = true;
  assert(evaluateSecurityAccess(afterRestart) == SecurityAccessStatus::Allowed);
}

void testFailClosedUnknownAndMethodMismatch() {
  const SecurityAccessMethod mutations[] = {
      SecurityAccessMethod::Post, SecurityAccessMethod::Put,
      SecurityAccessMethod::Delete, SecurityAccessMethod::Patch};
  for (SecurityAccessMethod method : mutations) {
    SecurityAccessRequest candidate =
        request(SecurityState::Open, method, "/api/new-dangerous-operation");
    assert(evaluateSecurityAccess(candidate) == SecurityAccessStatus::UnknownRoute);
  }
  SecurityAccessRequest unknownRead = request(
      SecurityState::Open, SecurityAccessMethod::Get, "/api/new-read");
  assert(evaluateSecurityAccess(unknownRead) == SecurityAccessStatus::UnknownRoute);
  SecurityAccessRequest methodMismatch = request(
      SecurityState::Open, SecurityAccessMethod::Get, "/api/mqtt");
  assert(evaluateSecurityAccess(methodMismatch) == SecurityAccessStatus::Allowed);
  methodMismatch.method = SecurityAccessMethod::Delete;
  methodMismatch.originPresent = true;
  methodMismatch.originValid = true;
  assert(evaluateSecurityAccess(methodMismatch) ==
         SecurityAccessStatus::MethodNotAllowed);
  assert(classifySecurityRoute(SecurityAccessMethod::Get, "/api/stations/add")
             .pathKnown);
  assert(classifySecurityRoute(SecurityAccessMethod::Head, "/voxone.html")
             .endpoint == SecurityEndpoint::Unknown);
  assert(classifySecurityRoute(SecurityAccessMethod::Head, "/")
             .endpoint == SecurityEndpoint::Unknown);

  size_t ruleCount = 0;
  const SecurityRouteRule* rules = securityAccessRouteRules(ruleCount);
  for (size_t index = 0; index < ruleCount; ++index) {
    if (!rules[index].multipart) continue;
    assert(rules[index].mutating);
    assert(securityAccessRequiresCsrfProtection(rules[index].method));
    assert(securityAccessRequiresOrigin(rules[index].method));
    SecurityAccessRequest missingOrigin = request(
        SecurityState::Open, rules[index].method, rules[index].path);
    missingOrigin.originPresent = false;
    missingOrigin.originValid = false;
    missingOrigin.recoveryMode = true;
    assert(evaluateSecurityAccess(missingOrigin) ==
           SecurityAccessStatus::InvalidOrigin);
  }
}

void testProductionInventoryAndObserveOnlyIsolation() {
  const std::string server = readFile("src/core/netserver.cpp");
  const std::string api = readFile("src/core/security_api.cpp");
  const std::string policy = readFile("src/core/security_access_policy.cpp");
  const std::string main = readFile("src/main.cpp");
  const std::string options = readFile("src/core/options.h");

  const char* registrations[] = {
      "webserver.on(\"/\", HTTP_ANY", "/api/stations/export",
      "/api/stations/import", "kStationsSnapshotRoute", "kDirectorySearchRoute",
      "/api/stations/add", "/api/stations/edit", "/api/stations/delete",
      "/api/stations/reorder", "/api/stations/metadata", "/api/mqtt",
      "/api/hardware", "kBackupRoute", "kRestoreRoute", "/api/time",
      "/api/time/sync", "kSecurityStateRoute", "kSecuritySetupRoute",
      "kSecurityLoginRoute", "kSecurityLogoutRoute", "/api/ready",
      "/api/bt/update", "/api/dsp/state", "AsyncWebSocket websocket(\"/ws\")",
      "request->url()==\"/update\"", "request->url() == \"/update/bt\"",
      "request->url()==\"/emergency\"", "request->url() == \"/update.html\"",
      "request->url() == \"/variables.js\"", "PLAYLIST_PATH"};
  for (const char* registration : registrations)
    assert(server.find(registration) != std::string::npos);

  // SECURITY-ENFORCE-1A deliberately prepares policy without changing runtime.
  assert(server.find("security_access_policy.h") == std::string::npos);
  assert(server.find("evaluateSecurityAccess(") == std::string::npos);
  assert(api.find("protectionEnforced, true") == std::string::npos);
  // ArduinoOTA is a separate, compile-time service rather than an HTTP
  // route. It must remain disabled until it can follow the same access
  // contract instead of relying on an unrelated OTA_PASS.
  assert(main.find("ArduinoOTA.setPassword(OTA_PASS)") != std::string::npos);
  assert(options.find("#define USE_OTA    false") != std::string::npos);
  assert(policy.find("password") == std::string::npos);
  assert(policy.find("cookie") == std::string::npos);
  assert(policy.find("token") == std::string::npos);
}

}  // namespace

int main() {
  testCompleteEndpointInventory();
  testHostOriginAndCsrfMatrix();
  testCompleteMethodStateSessionHostOriginMatrix();
  testDirectCompressedAssetsAndCanonicalFallback();
  testProtectedOpenAndLockedPolicy();
  testProvisioningMigrationRecoveryAndOtaRestart();
  testFailClosedUnknownAndMethodMismatch();
  testProductionInventoryAndObserveOnlyIsolation();
  return 0;
}
