#include "security_http_adapter.h"

#include <cstring>

namespace voxone {
namespace security {
namespace {

char asciiLower(char value) {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value + 32) : value;
}

bool equalsIgnoreCase(const char* left, const char* right) {
  if (!left || !right) return false;
  while (*left && *right) {
    if (asciiLower(*left++) != asciiLower(*right++)) return false;
  }
  return *left == '\0' && *right == '\0';
}

bool connectionHasUpgradeToken(const char* value) {
  if (!value) return false;
  const char* cursor = value;
  while (*cursor) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') ++cursor;
    const char* start = cursor;
    while (*cursor && *cursor != ',') ++cursor;
    const char* end = cursor;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;
    static const char expected[] = "upgrade";
    if (static_cast<size_t>(end - start) == sizeof(expected) - 1) {
      bool match = true;
      for (size_t index = 0; index < sizeof(expected) - 1; ++index)
        match = match && asciiLower(start[index]) == expected[index];
      if (match) return true;
    }
    if (*cursor == ',') ++cursor;
  }
  return false;
}

bool validWebSocketKey(const char* key) {
  if (!key) return false;
  size_t length = 0;
  while (key[length]) ++length;
  if (length != 24 || key[22] != '=' || key[23] != '=') return false;
  for (size_t index = 0; index < 22; ++index) {
    const char value = key[index];
    if (!((value >= 'A' && value <= 'Z') ||
          (value >= 'a' && value <= 'z') ||
          (value >= '0' && value <= '9') || value == '+' || value == '/'))
      return false;
  }
  // A WebSocket key is exactly 16 bytes. With two padding characters the
  // final base64 sextet may contain only the top two data bits.
  return key[21] == 'A' || key[21] == 'Q' || key[21] == 'g' ||
         key[21] == 'w';
}

}  // namespace

SecurityWebSocketHandshakeStatus securityWebSocketHandshakeStatus(
    SecurityAccessMethod method, const char* path,
    const SecurityWebSocketHandshake& handshake) {
  if (!handshake.requestedWebsocket || method != SecurityAccessMethod::Get ||
      !path || std::strcmp(path, "/ws") != 0)
    return SecurityWebSocketHandshakeStatus::NotWebSocket;
  const bool protocolAllowed = handshake.protocolHeaders == 0 ||
      (handshake.protocolHeaders == 1 && handshake.protocol &&
       std::strcmp(handshake.protocol, kSecurityWebSocketProtocol) == 0);
  const bool valid = handshake.connectionHeaders == 1 &&
      handshake.upgradeHeaders == 1 && handshake.versionHeaders == 1 &&
      handshake.keyHeaders == 1 && handshake.protocolHeaders <= 1 &&
      handshake.cookieHeaders <= 1 &&
      connectionHasUpgradeToken(handshake.connection) &&
      equalsIgnoreCase(handshake.upgrade, "websocket") &&
      handshake.version && std::strcmp(handshake.version, "13") == 0 &&
      validWebSocketKey(handshake.key) && protocolAllowed;
  return valid ? SecurityWebSocketHandshakeStatus::Valid
               : SecurityWebSocketHandshakeStatus::Invalid;
}

bool securityWebSocketSubprotocolRequested(
    const SecurityWebSocketHandshake& handshake) {
  return handshake.protocolHeaders == 1 && handshake.protocol &&
         std::strcmp(handshake.protocol, kSecurityWebSocketProtocol) == 0;
}

bool securityWebSocketCommandAllowed(const char* command) {
  if (!command || !*command) return false;
  // Keep the browser transport explicit and fail closed. Destructive legacy
  // maintenance commands (format, clearspiffs, reset) intentionally do not
  // appear here; they have dedicated recovery/configuration workflows.
  static const char* const allowed[] = {
      "ping", "tone", "trebble", "middle", "bass", "source",
      "webtransport", "start", "stop", "toggle", "prev", "next",
      "volm", "volp", "ballance", "playstation", "play", "vol",
      "volume", "vol100", "mute", "maximumvolume", "startupmode",
      "startupfixedvolume", "dspon", "dim", "getindex", "getrssi",
      "getsystem", "getwebstatus", "getscreen", "stationlisttimeout",
      "bttransporttimeout", "numplaylist", "flipscreen", "brightness",
      "screenon", "contrast", "screensaverenabled", "screensavertimeout",
      "screensaverblank", "screensaverplayingenabled",
      "screensaverplayingtimeout", "screensaverplayingblank", "abuff",
      "scrollsettings",
      "tzh", "tzm", "sntp2", "sntp1", "timeint", "timeintrtc",
      "volsteps", "encacc", "oneclickswitching", "balance", "reboot",
      "boot", "smartstart", "vumeter", "softap", "mdnsname"};
  for (const char* allowedCommand : allowed)
    if (std::strcmp(command, allowedCommand) == 0) return true;
  return false;
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
    case SecurityAccessStatus::InvalidHandshake: return 400;
  }
  return 403;
}

SecurityHttpDecision evaluateSecurityHttpRequest(
    const SecurityAccessRequest& request, bool enforcementEnabled,
    bool websocketRequest) {
  if (!enforcementEnabled)
    return {SecurityAccessStatus::Allowed, 200, true, websocketRequest};

  const SecurityAccessStatus status = evaluateSecurityAccess(request);
  return {status, securityAccessHttpStatus(status),
          status == SecurityAccessStatus::Allowed, websocketRequest};
}

}  // namespace security
}  // namespace voxone
