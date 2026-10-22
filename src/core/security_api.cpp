#include "security_api.h"

#include <cstdio>
#include <cstring>

namespace voxone {
namespace security {
namespace {

constexpr size_t kMaxAuthorityLength = 95;

void secureZero(void* memory, size_t size) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(memory);
  while (size--) *bytes++ = 0;
}

char asciiLower(char value) {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value + 32) : value;
}

bool asciiEqualIgnoreCase(const char* left, const char* right) {
  if (!left || !right) return false;
  while (*left && *right) {
    if (asciiLower(*left++) != asciiLower(*right++)) return false;
  }
  return *left == '\0' && *right == '\0';
}

bool validAuthorityCharacter(char value) {
  const unsigned char byte = static_cast<unsigned char>(value);
  return byte > 0x20 && byte < 0x7f && value != '/' && value != '\\' &&
         value != '?' && value != '#' && value != '@' && value != ',';
}

bool normalizeAuthority(const char* input, char* output, size_t outputSize) {
  if (!input || !*input || !output || outputSize < 2) return false;
  size_t length = 0;
  while (input[length]) {
    if (length >= kMaxAuthorityLength ||
        !validAuthorityCharacter(input[length]))
      return false;
    ++length;
  }

  size_t hostLength = length;
  const char* colon = std::strchr(input, ':');
  if (colon) {
    if (std::strchr(colon + 1, ':') || std::strcmp(colon, ":80") != 0)
      return false;
    hostLength = static_cast<size_t>(colon - input);
  }
  if (!hostLength || hostLength + 1 > outputSize) return false;
  for (size_t index = 0; index < hostLength; ++index)
    output[index] = asciiLower(input[index]);
  output[hostLength] = '\0';
  return true;
}

bool allowedHost(const SecurityRequestContext& request) {
  char host[kMaxAuthorityLength + 1];
  if (!normalizeAuthority(request.host, host, sizeof(host))) return false;
  if (request.localAddress &&
      asciiEqualIgnoreCase(host, request.localAddress))
    return true;
  if (!request.deviceHostname || !*request.deviceHostname) return false;
  if (asciiEqualIgnoreCase(host, request.deviceHostname)) return true;
  char localName[kMaxAuthorityLength + 1];
  const int written = std::snprintf(localName, sizeof(localName), "%s.local",
                                    request.deviceHostname);
  return written > 0 && static_cast<size_t>(written) < sizeof(localName) &&
         asciiEqualIgnoreCase(host, localName);
}

bool sameOrigin(const SecurityRequestContext& request) {
  if (!request.origin || std::strncmp(request.origin, "http://", 7) != 0)
    return false;
  char originHost[kMaxAuthorityLength + 1];
  char requestHost[kMaxAuthorityLength + 1];
  return normalizeAuthority(request.origin + 7, originHost,
                            sizeof(originHost)) &&
         normalizeAuthority(request.host, requestHost, sizeof(requestHost)) &&
         asciiEqualIgnoreCase(originHost, requestHost);
}

bool formContentType(const char* contentType) {
  static const char expected[] = "application/x-www-form-urlencoded";
  if (!contentType || std::strncmp(contentType, expected,
                                   sizeof(expected) - 1) != 0)
    return false;
  const char* suffix = contentType + sizeof(expected) - 1;
  return *suffix == '\0' || *suffix == ';';
}

SecurityApiResult result(uint16_t status, SecurityApiError error) {
  SecurityApiResult response{};
  response.httpStatus = status;
  response.error = error;
  return response;
}

SecurityApiResult mapSetupResult(SecurityOperationStatus status) {
  switch (status) {
    case SecurityOperationStatus::Applied:
      return result(200, SecurityApiError::None);
    case SecurityOperationStatus::InvalidPassword:
      return result(422, SecurityApiError::InvalidPassword);
    case SecurityOperationStatus::InvalidState:
    case SecurityOperationStatus::Unchanged:
      return result(409, SecurityApiError::InvalidState);
    case SecurityOperationStatus::RandomError:
      return result(500, SecurityApiError::RandomError);
    case SecurityOperationStatus::StorageError:
      return result(500, SecurityApiError::StorageError);
    case SecurityOperationStatus::AuthenticationFailed:
      return result(401, SecurityApiError::AuthenticationFailed);
  }
  return result(500, SecurityApiError::StorageError);
}

bool lowerHexToken(const char* token) {
  if (!token) return false;
  for (size_t index = 0; index < kSecuritySessionTokenHexLength; ++index) {
    const char value = token[index];
    if (!((value >= '0' && value <= '9') ||
          (value >= 'a' && value <= 'f')))
      return false;
  }
  return token[kSecuritySessionTokenHexLength] == '\0';
}

}  // namespace

SecurityRequestStatus validateSecurityReadRequest(
    const SecurityRequestContext& request) {
  return securityRequestHostAllowed(request) ? SecurityRequestStatus::Allowed
                                             : SecurityRequestStatus::InvalidHost;
}

SecurityRequestStatus validateSecurityFormRequest(
    const SecurityRequestContext& request) {
  if (!securityRequestHostAllowed(request))
    return SecurityRequestStatus::InvalidHost;
  if (!securityRequestOriginAllowed(request))
    return SecurityRequestStatus::InvalidOrigin;
  if (!request.contentLength) return SecurityRequestStatus::BodyRequired;
  if (request.contentLength > kSecurityMaxRequestBodyBytes)
    return SecurityRequestStatus::BodyTooLarge;
  if (!formContentType(request.contentType))
    return SecurityRequestStatus::UnsupportedMediaType;
  return SecurityRequestStatus::Allowed;
}

SecurityRequestStatus validateSecurityActionRequest(
    const SecurityRequestContext& request) {
  if (!securityRequestHostAllowed(request))
    return SecurityRequestStatus::InvalidHost;
  if (!securityRequestOriginAllowed(request))
    return SecurityRequestStatus::InvalidOrigin;
  return request.contentLength == 0 ? SecurityRequestStatus::Allowed
                                    : SecurityRequestStatus::BodyTooLarge;
}

bool securityRequestHostAllowed(const SecurityRequestContext& request) {
  return allowedHost(request);
}

bool securityRequestOriginAllowed(const SecurityRequestContext& request) {
  return sameOrigin(request);
}

bool securitySessionTokenFromCookie(
    const char* cookieHeader,
    char token[kSecuritySessionTokenBufferSize]) {
  if (!token) return false;
  token[0] = '\0';
  if (!cookieHeader) return false;
  const size_t headerLength = std::strlen(cookieHeader);
  if (!headerLength || headerLength > 512) return false;

  const size_t nameLength = sizeof(kSecuritySessionCookieName) - 1;
  bool found = false;
  const char* cursor = cookieHeader;
  while (*cursor) {
    while (*cursor == ' ' || *cursor == ';') ++cursor;
    const char* end = std::strchr(cursor, ';');
    if (!end) end = cursor + std::strlen(cursor);
    const char* equals = static_cast<const char*>(
        std::memchr(cursor, '=', static_cast<size_t>(end - cursor)));
    if (equals && static_cast<size_t>(equals - cursor) == nameLength &&
        std::memcmp(cursor, kSecuritySessionCookieName, nameLength) == 0) {
      if (found || static_cast<size_t>(end - equals - 1) !=
                       kSecuritySessionTokenHexLength)
        return false;
      std::memcpy(token, equals + 1, kSecuritySessionTokenHexLength);
      token[kSecuritySessionTokenHexLength] = '\0';
      if (!lowerHexToken(token)) {
        secureZero(token, kSecuritySessionTokenBufferSize);
        return false;
      }
      found = true;
    }
    cursor = *end ? end + 1 : end;
  }
  return found;
}

SecurityApiService::SecurityApiService(SecurityService& security,
                                       SecuritySessionService& sessions,
                                       SecurityHttpEnforcementMode enforcementMode)
    : security_(security), sessions_(sessions),
      enforcementMode_(enforcementMode) {}

SecurityApiState SecurityApiService::state(const char* sessionToken,
                                           uint32_t nowMs) {
  const SecurityState current = security_.state();
  const bool authenticated = current == SecurityState::Protected &&
                             sessions_.validate(sessionToken, nowMs);
  return {current, current == SecurityState::LegacyUnclaimed,
          current == SecurityState::Protected, authenticated,
          enforcementMode_ != SecurityHttpEnforcementMode::Disabled,
          enforcementMode_};
}

SecurityApiResult SecurityApiService::setup(const char* mode,
                                            const char* password) {
  if (security_.state() == SecurityState::Locked)
    return result(423, SecurityApiError::Locked);
  if (security_.state() != SecurityState::LegacyUnclaimed)
    return result(409, SecurityApiError::InvalidState);
  if (!mode) return result(400, SecurityApiError::InvalidMode);
  if (std::strcmp(mode, "open") == 0) {
    if (password && *password)
      return result(400, SecurityApiError::InvalidRequest);
    return mapSetupResult(security_.chooseOpen());
  }
  if (std::strcmp(mode, "protected") == 0) {
    if (!password) return result(422, SecurityApiError::InvalidPassword);
    return mapSetupResult(security_.chooseProtected(password));
  }
  return result(400, SecurityApiError::InvalidMode);
}

SecurityApiResult SecurityApiService::login(const char* password,
                                            uint32_t nowMs) {
  if (security_.state() == SecurityState::Locked)
    return result(423, SecurityApiError::Locked);
  SecurityApiResult response{};
  const SecurityLoginStatus status =
      sessions_.login(password, nowMs, response.sessionToken);
  switch (status) {
    case SecurityLoginStatus::Created:
      response.httpStatus = 200;
      response.error = SecurityApiError::None;
      response.setSessionCookie = true;
      return response;
    case SecurityLoginStatus::InvalidPassword:
      return result(401, SecurityApiError::AuthenticationFailed);
    case SecurityLoginStatus::RateLimited:
      return result(429, SecurityApiError::RateLimited);
    case SecurityLoginStatus::InvalidState:
      return result(409, SecurityApiError::InvalidState);
    case SecurityLoginStatus::RandomError:
      return result(500, SecurityApiError::RandomError);
  }
  return result(500, SecurityApiError::RandomError);
}

SecurityApiResult SecurityApiService::logout(const char* sessionToken) {
  if (sessionToken) sessions_.logout(sessionToken);
  SecurityApiResult response = result(204, SecurityApiError::None);
  response.clearSessionCookie = true;
  return response;
}

SecurityApiResult SecurityApiService::manage(const char* sessionToken,
                                             const char* mode,
                                             const char* password,
                                             uint32_t nowMs) {
  const SecurityState current = security_.state();
  if (current == SecurityState::Locked)
    return result(423, SecurityApiError::Locked);
  if (!mode) return result(400, SecurityApiError::InvalidMode);
  SecurityOperationStatus status = SecurityOperationStatus::InvalidState;
  if (current == SecurityState::Open) {
    if (std::strcmp(mode, "protected") != 0 || !password)
      return result(400, SecurityApiError::InvalidMode);
    status = security_.protectOpen(password);
  } else if (current == SecurityState::Protected) {
    if (!sessions_.validate(sessionToken, nowMs))
      return result(401, SecurityApiError::AuthenticationFailed);
    if (std::strcmp(mode, "protected") == 0 && password)
      status = security_.changePasswordAuthorized(password);
    else if (std::strcmp(mode, "open") == 0 && (!password || !*password))
      status = security_.unprotectAuthorized();
    else
      return result(400, SecurityApiError::InvalidMode);
  } else {
    return result(409, SecurityApiError::InvalidState);
  }
  SecurityApiResult response = mapSetupResult(status);
  if (status == SecurityOperationStatus::Applied) {
    sessions_.invalidateAll();
    response.clearSessionCookie = true;
  }
  return response;
}

bool securityStateJson(const SecurityApiState& state, char* output,
                       size_t outputSize) {
  if (!output || !outputSize) return false;
  const int written = std::snprintf(
      output, outputSize,
      "{\"state\":\"%s\",\"setupAllowed\":%s,\"loginAllowed\":%s,"
      "\"authenticated\":%s,\"protectionEnforced\":%s,"
      "\"enforcementMode\":\"%s\"}",
      securityStateName(state.state), state.setupAllowed ? "true" : "false",
      state.loginAllowed ? "true" : "false",
      state.authenticated ? "true" : "false",
      state.protectionEnforced ? "true" : "false",
      securityHttpEnforcementModeName(state.enforcementMode));
  return written > 0 && static_cast<size_t>(written) < outputSize;
}

const char* securityApiErrorName(SecurityApiError error) {
  switch (error) {
    case SecurityApiError::None: return "none";
    case SecurityApiError::InvalidRequest: return "invalid_request";
    case SecurityApiError::InvalidMode: return "invalid_mode";
    case SecurityApiError::InvalidPassword: return "invalid_password";
    case SecurityApiError::InvalidState: return "invalid_state";
    case SecurityApiError::AuthenticationFailed:
      return "authentication_failed";
    case SecurityApiError::RateLimited: return "rate_limited";
    case SecurityApiError::Locked: return "locked";
    case SecurityApiError::RandomError: return "random_error";
    case SecurityApiError::StorageError: return "storage_error";
  }
  return "internal_error";
}

}  // namespace security
}  // namespace voxone
