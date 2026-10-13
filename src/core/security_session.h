#ifndef VOXONE_SECURITY_SESSION_H
#define VOXONE_SECURITY_SESSION_H

#include "security_settings.h"

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace security {

constexpr size_t kSecuritySessionTokenBytes = 16;
constexpr size_t kSecuritySessionTokenHexLength = 32;
constexpr size_t kSecuritySessionTokenBufferSize = 33;
constexpr size_t kMaxSecuritySessions = 2;
constexpr uint32_t kSecuritySessionIdleTimeoutMs = 8UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kSecuritySessionAbsoluteTimeoutMs =
    24UL * 60UL * 60UL * 1000UL;
constexpr uint8_t kSecurityLoginFailureLimit = 5;
constexpr uint32_t kSecurityLoginFailureWindowMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kSecurityLoginLockoutMs = 60UL * 1000UL;

enum class SecurityLoginStatus : uint8_t {
  Created,
  InvalidState,
  InvalidPassword,
  RateLimited,
  RandomError
};

enum class SecuritySessionBindingMode : uint8_t {
  None,
  Open,
  Protected
};

// Opaque, RAM-only authorization captured when a long-lived connection is
// accepted.  Protected bindings contain only the SHA-256 token digest, never
// the HttpOnly cookie value.  The security generation makes mode/password
// changes invalidate an already accepted connection.
struct SecuritySessionBinding {
  uint8_t tokenHash[kSecurityVerifierSize];
  uint32_t securityGeneration;
  SecuritySessionBindingMode mode;
};

class SecuritySessionService {
 public:
  SecuritySessionService(SecurityService& security,
                         SecurityRandomSource& random);

  SecurityLoginStatus login(
      const char* password, uint32_t nowMs,
      char token[kSecuritySessionTokenBufferSize]);
  bool validate(const char* token, uint32_t nowMs);
  bool logout(const char* token);
  void invalidateAll();
  size_t activeSessionCount(uint32_t nowMs);
  bool rateLimited(uint32_t nowMs);

  // OPEN binds without a token. PROTECTED requires a valid token. Other
  // security states fail closed. validateBinding() can either observe the
  // binding or count an accepted client frame as session activity.
  bool bind(const char* token, uint32_t nowMs,
            SecuritySessionBinding& binding);
  bool validateBinding(const SecuritySessionBinding& binding, uint32_t nowMs,
                       bool recordActivity);
  static void clearBinding(SecuritySessionBinding& binding);

 private:
  struct Session {
    uint8_t tokenHash[kSecurityVerifierSize];
    uint32_t createdAtMs;
    uint32_t lastActivityMs;
    bool active;
  };

  void synchronizeSecurity();
  void expireSessions(uint32_t nowMs);
  void refreshRateLimit(uint32_t nowMs);
  void resetRateLimit();
  SecurityLoginStatus recordFailedLogin(uint32_t nowMs);
  bool tokenHash(const char* token,
                 uint8_t output[kSecurityVerifierSize]) const;
  int matchingSession(const uint8_t hash[kSecurityVerifierSize]) const;
  size_t targetSession(uint32_t nowMs) const;
  void clearSession(Session& session);

  SecurityService& security_;
  SecurityRandomSource& random_;
  Session sessions_[kMaxSecuritySessions];
  SecurityState observedState_;
  uint32_t observedGeneration_;
  uint8_t failedAttempts_;
  uint32_t failureWindowStartedAtMs_;
  uint32_t lockoutStartedAtMs_;
  bool failureWindowActive_;
  bool lockoutActive_;
};

}  // namespace security
}  // namespace voxone

#endif
