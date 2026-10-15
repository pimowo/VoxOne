#include "../src/core/security_http_adapter.h"
#include "../src/AsyncWebServer/RequestConnectionType.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

using namespace voxone::security;

namespace {

SecurityAccessRequest request(SecurityState state, SecurityAccessMethod method,
                              const char* path) {
  return {state, method, path, false, true,
          securityAccessRequiresOrigin(method), true, false, false};
}

struct ParsedHttpRequest {
  SecurityAccessMethod method = SecurityAccessMethod::Unknown;
  std::string path;
  SecurityWebSocketHandshake handshake{};
  std::string connection;
  std::string upgrade;
  std::string version;
  std::string key;
  std::string protocol;
};

ParsedHttpRequest parseAsyncWebRequestSignals(const std::string& raw) {
  ParsedHttpRequest parsed;
  const size_t methodEnd = raw.find(' ');
  const size_t pathEnd = methodEnd == std::string::npos
      ? std::string::npos : raw.find(' ', methodEnd + 1);
  assert(methodEnd != std::string::npos && pathEnd != std::string::npos);
  const std::string method = raw.substr(0, methodEnd);
  parsed.method = method == "GET" ? SecurityAccessMethod::Get
      : method == "POST" ? SecurityAccessMethod::Post
      : method == "PUT" ? SecurityAccessMethod::Put
      : method == "DELETE" ? SecurityAccessMethod::Delete
      : method == "PATCH" ? SecurityAccessMethod::Patch
      : method == "HEAD" ? SecurityAccessMethod::Head
      : method == "OPTIONS" ? SecurityAccessMethod::Options
      : SecurityAccessMethod::Unknown;
  parsed.path = raw.substr(methodEnd + 1, pathEnd - methodEnd - 1);

  size_t line = raw.find("\r\n", pathEnd);
  while (line != std::string::npos) {
    line += 2;
    const size_t end = raw.find("\r\n", line);
    if (end == std::string::npos || end == line) break;
    const size_t colon = raw.find(':', line);
    assert(colon != std::string::npos && colon < end);
    const size_t value = colon + 1 < end && raw[colon + 1] == ' '
        ? colon + 2 : colon + 1;
    const std::string name = raw.substr(line, colon - line);
    const std::string headerValue = raw.substr(value, end - value);
    if (asyncWebHeaderRequestsWebSocket(name.c_str(), headerValue.c_str()))
      parsed.handshake.requestedWebsocket = true;
    if (name == "Connection") {
      ++parsed.handshake.connectionHeaders;
      if (parsed.handshake.connectionHeaders == 1) parsed.connection = headerValue;
    } else if (name == "Upgrade") {
      ++parsed.handshake.upgradeHeaders;
      if (parsed.handshake.upgradeHeaders == 1) parsed.upgrade = headerValue;
    } else if (name == "Sec-WebSocket-Version") {
      ++parsed.handshake.versionHeaders;
      if (parsed.handshake.versionHeaders == 1) parsed.version = headerValue;
    } else if (name == "Sec-WebSocket-Key") {
      ++parsed.handshake.keyHeaders;
      if (parsed.handshake.keyHeaders == 1) parsed.key = headerValue;
    } else if (name == "Sec-WebSocket-Protocol") {
      ++parsed.handshake.protocolHeaders;
      if (parsed.handshake.protocolHeaders == 1) parsed.protocol = headerValue;
    } else if (name == "Cookie") {
      ++parsed.handshake.cookieHeaders;
    }
    line = end;
  }
  return parsed;
}

SecurityHttpDecision evaluateRawProtectedRequest(const std::string& raw,
                                                 bool sessionValid = false) {
  ParsedHttpRequest parsed = parseAsyncWebRequestSignals(raw);
  parsed.handshake.connection = parsed.connection.c_str();
  parsed.handshake.upgrade = parsed.upgrade.c_str();
  parsed.handshake.version = parsed.version.c_str();
  parsed.handshake.key = parsed.key.c_str();
  parsed.handshake.protocol = parsed.protocol.c_str();
  const SecurityWebSocketHandshakeStatus handshakeStatus =
      securityWebSocketHandshakeStatus(parsed.method, parsed.path.c_str(),
                                       parsed.handshake);
  if (handshakeStatus == SecurityWebSocketHandshakeStatus::Invalid)
    return {SecurityAccessStatus::InvalidHandshake, 400, false, true};
  const bool websocket =
      handshakeStatus == SecurityWebSocketHandshakeStatus::Valid;
  SecurityAccessRequest access = request(SecurityState::Protected,
      websocket ? SecurityAccessMethod::WebSocketHandshake : parsed.method,
      parsed.path.c_str());
  access.sessionValid = sessionValid;
  return evaluateSecurityHttpRequest(access, true, websocket);
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void testDisabledAndWebSocketEnforced() {
  SecurityAccessRequest protectedRead = request(
      SecurityState::Protected, SecurityAccessMethod::Get, "/api/stations");
  auto decision = evaluateSecurityHttpRequest(protectedRead, false, false);
  assert(decision.allowed && decision.httpStatus == 200 &&
         !decision.websocketHandshake);

  SecurityAccessRequest protectedWebSocket = request(
      SecurityState::Protected, SecurityAccessMethod::WebSocketHandshake,
      "/ws");
  decision = evaluateSecurityHttpRequest(protectedWebSocket, true, true);
  assert(!decision.allowed && decision.httpStatus == 401 &&
         decision.websocketHandshake);
  protectedWebSocket.sessionValid = true;
  decision = evaluateSecurityHttpRequest(protectedWebSocket, true, true);
  assert(decision.allowed && decision.websocketHandshake);
}

void testStrictHandshakeAndFakeUpgradeCannotBypassHttp() {
  const char* protectedPosts[] = {
      "/update", "/update/bt", "/api/config/restore",
      "/api/stations/import", "/api/time/sync", "/api/hardware",
      "/api/mqtt", "/api/stations/add"};
  unsigned writes = 0;
  for (const char* path : protectedPosts) {
    const std::string raw = std::string("POST ") + path +
        " HTTP/1.1\r\nHost: 192.168.1.44\r\n"
        "Origin: http://192.168.1.44\r\nUpgrade: websocket\r\n"
        "Content-Length: 64\r\n\r\n";
    assert(parseAsyncWebRequestSignals(raw).handshake.requestedWebsocket);
    const SecurityHttpDecision decision = evaluateRawProtectedRequest(raw);
    if (decision.allowed) ++writes;
    assert(!decision.allowed && !decision.websocketHandshake &&
           decision.status == SecurityAccessStatus::AuthenticationRequired);
  }
  assert(writes == 0);

  const SecurityHttpDecision protectedGet = evaluateRawProtectedRequest(
      "GET /api/stations HTTP/1.1\r\nHost: 192.168.1.44\r\n"
      "Upgrade: WebSocket\r\n\r\n");
  assert(!protectedGet.allowed && !protectedGet.websocketHandshake &&
         protectedGet.httpStatus == 401);

  const SecurityHttpDecision foreign = evaluateRawProtectedRequest(
      "GET /not-a-route HTTP/1.1\r\nHost: 192.168.1.44\r\n"
      "Upgrade: websocket\r\n\r\n");
  assert(!foreign.allowed && !foreign.websocketHandshake &&
         foreign.httpStatus == 404);

  const SecurityHttpDecision wrongMethod = evaluateRawProtectedRequest(
      "POST /ws HTTP/1.1\r\nHost: 192.168.1.44\r\n"
      "Origin: http://192.168.1.44\r\nUpgrade: websocket\r\n"
      "Content-Length: 1\r\n\r\n");
  assert(!wrongMethod.allowed && !wrongMethod.websocketHandshake &&
         wrongMethod.httpStatus == 405);

  const SecurityHttpDecision websocket = evaluateRawProtectedRequest(
      "GET /ws HTTP/1.1\r\nHost: 192.168.1.44\r\n"
      "Origin: http://192.168.1.44\r\nConnection: keep-alive, Upgrade\r\n"
      "Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n", true);
  assert(websocket.allowed && websocket.websocketHandshake);

  const SecurityHttpDecision noSession = evaluateRawProtectedRequest(
      "GET /ws HTTP/1.1\r\nHost: 192.168.1.44\r\n"
      "Origin: http://192.168.1.44\r\nConnection: Upgrade\r\n"
      "Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n");
  assert(!noSession.allowed && noSession.websocketHandshake &&
         noSession.httpStatus == 401);

  const SecurityHttpDecision malformed = evaluateRawProtectedRequest(
      "GET /ws HTTP/1.1\r\nHost: 192.168.1.44\r\n"
      "Origin: http://192.168.1.44\r\nUpgrade: websocket\r\n\r\n");
  assert(!malformed.allowed && malformed.websocketHandshake &&
         malformed.status == SecurityAccessStatus::InvalidHandshake &&
         malformed.httpStatus == 400);

  const SecurityHttpDecision ordinaryGetWs = evaluateRawProtectedRequest(
      "GET /ws HTTP/1.1\r\nHost: 192.168.1.44\r\n\r\n");
  assert(!ordinaryGetWs.allowed && !ordinaryGetWs.websocketHandshake &&
         ordinaryGetWs.httpStatus == 405);
}

void testSubprotocolAndCommandContract() {
  SecurityWebSocketHandshake handshake{
      true, 1, 1, 1, 1, 0, 0, "Upgrade", "websocket", "13",
      "dGhlIHNhbXBsZSBub25jZQ==", nullptr};
  assert(securityWebSocketHandshakeStatus(
      SecurityAccessMethod::Get, "/ws", handshake) ==
      SecurityWebSocketHandshakeStatus::Valid);
  assert(!securityWebSocketSubprotocolRequested(handshake));
  handshake.protocolHeaders = 1;
  handshake.protocol = kSecurityWebSocketProtocol;
  assert(securityWebSocketHandshakeStatus(
      SecurityAccessMethod::Get, "/ws", handshake) ==
      SecurityWebSocketHandshakeStatus::Valid);
  assert(securityWebSocketSubprotocolRequested(handshake));
  handshake.protocol = "arbitrary-reflection";
  assert(securityWebSocketHandshakeStatus(
      SecurityAccessMethod::Get, "/ws", handshake) ==
      SecurityWebSocketHandshakeStatus::Invalid);
  handshake.protocolHeaders = 0;
  handshake.protocol = nullptr;
  handshake.key = "dGhlIHNhbXBsZSBub25jZf==";
  assert(securityWebSocketHandshakeStatus(
      SecurityAccessMethod::Get, "/ws", handshake) ==
      SecurityWebSocketHandshakeStatus::Invalid);

  assert(securityWebSocketCommandAllowed("getsystem"));
  assert(securityWebSocketCommandAllowed("vol100"));
  assert(securityWebSocketCommandAllowed("scrollsettings"));
  assert(!securityWebSocketCommandAllowed("format"));
  assert(!securityWebSocketCommandAllowed("clearspiffs"));
  assert(!securityWebSocketCommandAllowed("reset"));
  assert(!securityWebSocketCommandAllowed("unknown-command"));

  const std::string websocketSource =
      readFile("src/AsyncWebServer/AsyncWebSocket.cpp");
  const std::string frontend = readFile("web-src/voxone.js");
  assert(websocketSource.find("_subprotocolPolicyEnabled && selectedSubprotocol") !=
         std::string::npos);
  assert(websocketSource.find("protocol->value() != _subprotocol") !=
         std::string::npos);
  assert(frontend.find("new WebSocket(scheme + location.host + \"/ws\")") !=
         std::string::npos);
}

void testProtectedOpenAndLegacy() {
  SecurityAccessRequest protectedRead = request(
      SecurityState::Protected, SecurityAccessMethod::Get, "/api/stations");
  auto decision = evaluateSecurityHttpRequest(protectedRead, true, false);
  assert(!decision.allowed && decision.httpStatus == 401 &&
         decision.status == SecurityAccessStatus::AuthenticationRequired);
  protectedRead.sessionValid = true;
  decision = evaluateSecurityHttpRequest(protectedRead, true, false);
  assert(decision.allowed);

  SecurityAccessRequest openMutation = request(
      SecurityState::Open, SecurityAccessMethod::Post, "/api/time/sync");
  decision = evaluateSecurityHttpRequest(openMutation, true, false);
  assert(decision.allowed);
  openMutation.originPresent = false;
  openMutation.originValid = false;
  decision = evaluateSecurityHttpRequest(openMutation, true, false);
  assert(!decision.allowed && decision.httpStatus == 403 &&
         decision.status == SecurityAccessStatus::InvalidOrigin);

  SecurityAccessRequest legacyUpdate = request(
      SecurityState::LegacyUnclaimed, SecurityAccessMethod::Post, "/update");
  decision = evaluateSecurityHttpRequest(legacyUpdate, true, false);
  assert(decision.allowed);
  SecurityAccessRequest legacyData = request(
      SecurityState::LegacyUnclaimed, SecurityAccessMethod::Get,
      "/api/stations");
  decision = evaluateSecurityHttpRequest(legacyData, true, false);
  assert(!decision.allowed && decision.httpStatus == 403);

  SecurityAccessRequest openWebSocket = request(
      SecurityState::Open, SecurityAccessMethod::WebSocketHandshake, "/ws");
  decision = evaluateSecurityHttpRequest(openWebSocket, true, true);
  assert(decision.allowed && decision.websocketHandshake);
  openWebSocket.hostValid = false;
  decision = evaluateSecurityHttpRequest(openWebSocket, true, true);
  assert(!decision.allowed && decision.status == SecurityAccessStatus::InvalidHost);
  openWebSocket.hostValid = true;
  openWebSocket.originPresent = false;
  openWebSocket.originValid = false;
  decision = evaluateSecurityHttpRequest(openWebSocket, true, true);
  assert(!decision.allowed &&
         decision.status == SecurityAccessStatus::InvalidOrigin);
  for (SecurityState deniedState : {SecurityState::LegacyUnclaimed,
                                    SecurityState::Unconfigured,
                                    SecurityState::Locked}) {
    SecurityAccessRequest deniedWebSocket = request(
        deniedState, SecurityAccessMethod::WebSocketHandshake, "/ws");
    decision = evaluateSecurityHttpRequest(deniedWebSocket, true, true);
    assert(!decision.allowed);
  }
}

void testPublicReadinessAndFailClosedPaths() {
  for (SecurityState state : {
           SecurityState::Unconfigured, SecurityState::LegacyUnclaimed,
           SecurityState::Protected, SecurityState::Open,
           SecurityState::Locked}) {
    SecurityAccessRequest ready =
        request(state, SecurityAccessMethod::Get, "/api/ready");
    const auto decision =
        evaluateSecurityHttpRequest(ready, true, false);
    assert(decision.allowed);
  }

  SecurityAccessRequest badHost = request(
      SecurityState::Open, SecurityAccessMethod::Get, "/api/ready");
  badHost.hostValid = false;
  auto decision = evaluateSecurityHttpRequest(badHost, true, false);
  assert(!decision.allowed && decision.httpStatus == 403);
  badHost.hostValid = true;
  badHost.originPresent = true;
  badHost.originValid = false;
  decision = evaluateSecurityHttpRequest(badHost, true, false);
  assert(!decision.allowed && decision.httpStatus == 403);

  SecurityAccessRequest directGzip = request(
      SecurityState::Open, SecurityAccessMethod::Get, "/voxone.js.gz");
  decision = evaluateSecurityHttpRequest(directGzip, true, false);
  assert(!decision.allowed && decision.httpStatus == 403 &&
         decision.status ==
             SecurityAccessStatus::DirectCompressedAssetDenied);
  directGzip.method = SecurityAccessMethod::Post;
  directGzip.originPresent = true;
  directGzip.originValid = true;
  decision = evaluateSecurityHttpRequest(directGzip, true, false);
  assert(!decision.allowed && decision.httpStatus == 405);

  SecurityAccessRequest unknown = request(
      SecurityState::Open, SecurityAccessMethod::Get, "/unknown.gz");
  decision = evaluateSecurityHttpRequest(unknown, true, false);
  assert(!decision.allowed && decision.httpStatus == 404);

  assert(securityHttpPathRepresentationAllowed("/voxone.js", 10));
  assert(securityHttpPathRepresentationAllowed("/voxone.js.gz", 13));
  const char unterminated[] = {'/', 'o', 'k'};
  assert(securityHttpPathRepresentationAllowed(unterminated, 3));
  assert(!securityHttpPathRepresentationAllowed("voxone.js", 9));
  const char embeddedNul[] = {'/', 'u', 'p', 'd', 'a', 't', 'e', '\0', 'x', '\0'};
  assert(!securityHttpPathRepresentationAllowed(embeddedNul, 9));
  assert(!securityHttpPathRepresentationAllowed("/api\\ready", 10));
  const char control[] = {'/', 'a', 'p', 'i', '\r', 'x', '\0'};
  assert(!securityHttpPathRepresentationAllowed(control, 6));
}

void testUploadAuthorizationPrecedesEveryWrite() {
  const std::string server = readFile("src/core/netserver.cpp");
  const std::string asyncRequest =
      readFile("src/AsyncWebServer/WebRequest.cpp");
  const size_t gate =
      server.find("webserver.addHandler(new SecurityHttpAccessGate())");
  const size_t updateGuard =
      server.find("webserver.addHandler(new WebUpdateRequestGuard())");
  const size_t routes = server.find("webserver.on(\"/\", HTTP_ANY");
  assert(gate != std::string::npos && updateGuard != std::string::npos &&
         routes != std::string::npos && gate < updateGuard &&
         updateGuard < routes);
  assert(asyncRequest.find("_server->_attachHandler(this);") !=
         std::string::npos);
  assert(asyncRequest.find("_handler->handleUpload(this") !=
         std::string::npos);
  assert(server.find("if (decision.allowed) {") !=
         std::string::npos);
  assert(server.find("request->_tempObject = denied;") !=
         std::string::npos);
  assert(server.find("originHeaders == 1") != std::string::npos);
  assert(server.find("hostHeaders == 1") != std::string::npos);
  assert(server.find("securityHttpPathRepresentationAllowed") !=
         std::string::npos);
  assert(server.find("requestSessionToken(request, token)") !=
         std::string::npos);
  assert(server.find("deviceSecuritySessionService().validate(token, millis())") !=
         std::string::npos);
  assert(server.find("validateBinding(") != std::string::npos);
  assert(server.find("frame->opcode != WS_TEXT") != std::string::npos);
  assert(server.find("securityWebSocketTextAll(wsBuf)") != std::string::npos);
  assert(server.find("clearSensitive(token, sizeof(token))") !=
         std::string::npos);
  assert(server.find("securityWebSocketHandshakeStatus(") !=
         std::string::npos);
  assert(asyncRequest.find("asyncWebHeaderRequestsWebSocket(") !=
         std::string::npos);

  const size_t networkSnapshot = server.find("static bool formatNetworkInfo(");
  const size_t webStatusSnapshot = server.find("static void formatWebStatus(");
  assert(networkSnapshot != std::string::npos &&
         webStatusSnapshot > networkSnapshot);
  const std::string networkFormatter = server.substr(
      networkSnapshot, webStatusSnapshot - networkSnapshot);
  assert(networkFormatter.find("passwordSet") != std::string::npos);
  assert(networkFormatter.find("config.ssids[index].password[0]") !=
         std::string::npos);
  assert(networkFormatter.find("config.ssids[index].password,") ==
         std::string::npos);

  unsigned flashWrites = 0;
  SecurityAccessRequest deniedUpload = request(
      SecurityState::Protected, SecurityAccessMethod::Post, "/update");
  const SecurityHttpDecision pinned =
      evaluateSecurityHttpRequest(deniedUpload, true, false);
  if (pinned.allowed) ++flashWrites;
  assert(!pinned.allowed && flashWrites == 0);
}

void testProtectedRecoveryLoginSelection() {
  assert(!securityHttpRecoveryLoginRequired(
      false, SecurityState::Protected, false, false));
  assert(!securityHttpRecoveryLoginRequired(
      true, SecurityState::Protected, true, false));
  assert(securityHttpRecoveryLoginRequired(
      true, SecurityState::Protected, false, false));
  assert(securityHttpRecoveryLoginRequired(
      true, SecurityState::Protected, true, true));
  for (SecurityState state : {
           SecurityState::Unconfigured, SecurityState::LegacyUnclaimed,
           SecurityState::Open, SecurityState::Locked}) {
    assert(!securityHttpRecoveryLoginRequired(true, state, false, true));
  }

  assert(securityHttpFilesystemRecoveryPassthrough(
      SecurityAccessMethod::Get, "/"));
  assert(securityHttpFilesystemRecoveryPassthrough(
      SecurityAccessMethod::Get, "/api/security/state"));
  assert(securityHttpFilesystemRecoveryPassthrough(
      SecurityAccessMethod::Get, "/api/ready"));
  assert(securityHttpFilesystemRecoveryPassthrough(
      SecurityAccessMethod::Post, "/api/security/login"));
  assert(securityHttpFilesystemRecoveryPassthrough(
      SecurityAccessMethod::Post, "/api/security/logout"));
  assert(!securityHttpFilesystemRecoveryPassthrough(
      SecurityAccessMethod::Get, "/api/security/login"));
  assert(!securityHttpFilesystemRecoveryPassthrough(
      SecurityAccessMethod::Post, "/api/config/restore"));
  assert(!securityHttpFilesystemRecoveryPassthrough(
      SecurityAccessMethod::Post, "/api/stations/import"));
  assert(!securityHttpFilesystemRecoveryPassthrough(
      SecurityAccessMethod::Post, "/update/bt"));
  assert(!securityHttpFilesystemRecoveryPassthrough(
      SecurityAccessMethod::Get, "/emergency"));

  SecurityAccessRequest emergency = request(
      SecurityState::Protected, SecurityAccessMethod::Get, "/emergency");
  auto decision = evaluateSecurityHttpRequest(emergency, true, false);
  assert(!decision.allowed && decision.httpStatus == 401);
  emergency.sessionValid = true;
  decision = evaluateSecurityHttpRequest(emergency, true, false);
  assert(decision.allowed);

  SecurityAccessRequest update = request(
      SecurityState::Protected, SecurityAccessMethod::Post, "/update");
  decision = evaluateSecurityHttpRequest(update, true, false);
  assert(!decision.allowed && decision.httpStatus == 401);
  update.sessionValid = true;
  decision = evaluateSecurityHttpRequest(update, true, false);
  assert(decision.allowed);

  const std::string server = readFile("src/core/netserver.cpp");
  const std::string header = readFile("src/core/netserver.h");
  assert(server.find("securityRecoveryLoginRequired()") != std::string::npos);
  assert(server.find("sendSecurityRecoveryLogin(request)") !=
         std::string::npos);
  assert(server.find("systemOperationState.filesystemUnavailable() &&") !=
         std::string::npos);
  assert(server.find("securityHttpFilesystemRecoveryPassthrough(") !=
         std::string::npos);
  assert(header.find("security_recovery_login") != std::string::npos);
  assert(header.find("/api/security/login") != std::string::npos);
  assert(header.find("window.location.replace('/emergency')") !=
         std::string::npos);
}

void testBuildSwitchIsMonotonic() {
#if VOXONE_SECURITY_HTTP_ENFORCEMENT || \
    VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST
  static_assert(securityHttpEnforcementEnabled(),
                "enabled build must enforce HTTP policy");
#else
  static_assert(!securityHttpEnforcementEnabled(),
                "ordinary build must remain disabled in 1B");
#endif
  const std::string platform = readFile("platformio.ini");
  assert(platform.find("[env:m0-security-test]") != std::string::npos);
  assert(platform.find("-DVOXONE_SECURITY_HTTP_ENFORCEMENT_TEST=1") !=
         std::string::npos);
}

}  // namespace

int main() {
  testDisabledAndWebSocketEnforced();
  testStrictHandshakeAndFakeUpgradeCannotBypassHttp();
  testSubprotocolAndCommandContract();
  testProtectedOpenAndLegacy();
  testPublicReadinessAndFailClosedPaths();
  testUploadAuthorizationPrecedesEveryWrite();
  testProtectedRecoveryLoginSelection();
  testBuildSwitchIsMonotonic();
  return 0;
}
