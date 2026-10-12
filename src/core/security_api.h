#ifndef VOXONE_SECURITY_API_H
#define VOXONE_SECURITY_API_H

#include "security_session.h"

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace security {

constexpr char kSecurityStateRoute[] = "/api/security/state";
constexpr char kSecuritySetupRoute[] = "/api/security/setup";
constexpr char kSecurityLoginRoute[] = "/api/security/login";
constexpr char kSecurityLogoutRoute[] = "/api/security/logout";
constexpr char kSecuritySessionCookieName[] = "voxone_session";
constexpr size_t kSecurityMaxRequestBodyBytes = 256;
constexpr size_t kSecurityStateJsonSize = 192;

enum class SecurityRequestStatus : uint8_t {
  Allowed,
  InvalidHost,
  InvalidOrigin,
  UnsupportedMediaType,
  BodyTooLarge,
  BodyRequired
};

struct SecurityRequestContext {
  const char* host;
  const char* origin;
  const char* localAddress;
  const char* deviceHostname;
  const char* contentType;
  size_t contentLength;
};

SecurityRequestStatus validateSecurityReadRequest(
    const SecurityRequestContext& request);
SecurityRequestStatus validateSecurityFormRequest(
    const SecurityRequestContext& request);
SecurityRequestStatus validateSecurityActionRequest(
    const SecurityRequestContext& request);

bool securitySessionTokenFromCookie(
    const char* cookieHeader,
    char token[kSecuritySessionTokenBufferSize]);

enum class SecurityApiError : uint8_t {
  None,
  InvalidRequest,
  InvalidMode,
  InvalidPassword,
  InvalidState,
  AuthenticationFailed,
  RateLimited,
  Locked,
  RandomError,
  StorageError
};

struct SecurityApiState {
  SecurityState state;
  bool setupAllowed;
  bool loginAllowed;
  bool authenticated;
  bool protectionEnforced;
};

struct SecurityApiResult {
  uint16_t httpStatus;
  SecurityApiError error;
  bool setSessionCookie;
  bool clearSessionCookie;
  char sessionToken[kSecuritySessionTokenBufferSize];
};

class SecurityApiService {
 public:
  SecurityApiService(SecurityService& security,
                     SecuritySessionService& sessions);

  SecurityApiState state(const char* sessionToken, uint32_t nowMs);
  SecurityApiResult setup(const char* mode, const char* password);
  SecurityApiResult login(const char* password, uint32_t nowMs);
  SecurityApiResult logout(const char* sessionToken);

 private:
  SecurityService& security_;
  SecuritySessionService& sessions_;
};

bool securityStateJson(const SecurityApiState& state, char* output,
                       size_t outputSize);
const char* securityApiErrorName(SecurityApiError error);

// Device-owned session state. It is RAM-only and is intentionally recreated
// after every restart.
SecuritySessionService& deviceSecuritySessionService();

}  // namespace security
}  // namespace voxone

#endif
