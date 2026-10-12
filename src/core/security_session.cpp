#include "security_session.h"

#include <cstring>

namespace voxone {
namespace security {
namespace {

void secureZero(void* memory, size_t size) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(memory);
  while (size--) *bytes++ = 0;
}

bool allZero(const uint8_t* bytes, size_t size) {
  uint8_t combined = 0;
  for (size_t index = 0; index < size; ++index) combined |= bytes[index];
  return combined == 0;
}

bool constantTimeEqual(const uint8_t* left, const uint8_t* right,
                       size_t size) {
  uint8_t difference = 0;
  for (size_t index = 0; index < size; ++index)
    difference |= left[index] ^ right[index];
  return difference == 0;
}

bool elapsedAtLeast(uint32_t nowMs, uint32_t startedAtMs,
                    uint32_t durationMs) {
  return static_cast<uint32_t>(nowMs - startedAtMs) >= durationMs;
}

char hexDigit(uint8_t value) {
  return value < 10 ? static_cast<char>('0' + value)
                    : static_cast<char>('a' + value - 10);
}

bool isLowerHex(char value) {
  return (value >= '0' && value <= '9') ||
         (value >= 'a' && value <= 'f');
}

}  // namespace

SecuritySessionService::SecuritySessionService(SecurityService& security,
                                               SecurityRandomSource& random)
    : security_(security),
      random_(random),
      sessions_{},
      observedState_(security.state()),
      observedGeneration_(security.generation()),
      failedAttempts_(0),
      failureWindowStartedAtMs_(0),
      lockoutStartedAtMs_(0),
      failureWindowActive_(false),
      lockoutActive_(false) {}

SecurityLoginStatus SecuritySessionService::login(
    const char* password, uint32_t nowMs,
    char token[kSecuritySessionTokenBufferSize]) {
  if (token) token[0] = '\0';
  synchronizeSecurity();
  expireSessions(nowMs);
  refreshRateLimit(nowMs);
  if (!token || security_.state() != SecurityState::Protected)
    return SecurityLoginStatus::InvalidState;
  if (lockoutActive_) return SecurityLoginStatus::RateLimited;
  if (!security_.verifyPassword(password)) return recordFailedLogin(nowMs);
  resetRateLimit();

  uint8_t rawToken[kSecuritySessionTokenBytes];
  if (!random_.fill(rawToken, sizeof(rawToken)) ||
      allZero(rawToken, sizeof(rawToken))) {
    secureZero(rawToken, sizeof(rawToken));
    return SecurityLoginStatus::RandomError;
  }
  for (size_t index = 0; index < sizeof(rawToken); ++index) {
    token[index * 2] = hexDigit(static_cast<uint8_t>(rawToken[index] >> 4));
    token[index * 2 + 1] = hexDigit(rawToken[index] & 0x0f);
  }
  token[kSecuritySessionTokenHexLength] = '\0';

  uint8_t hash[kSecurityVerifierSize];
  const bool hashed = securitySha256(
      reinterpret_cast<const uint8_t*>(token),
      kSecuritySessionTokenHexLength, hash);
  secureZero(rawToken, sizeof(rawToken));
  if (!hashed || matchingSession(hash) >= 0) {
    secureZero(hash, sizeof(hash));
    secureZero(token, kSecuritySessionTokenBufferSize);
    return SecurityLoginStatus::RandomError;
  }

  Session& session = sessions_[targetSession(nowMs)];
  clearSession(session);
  std::memcpy(session.tokenHash, hash, sizeof(session.tokenHash));
  session.createdAtMs = nowMs;
  session.lastActivityMs = nowMs;
  session.active = true;
  secureZero(hash, sizeof(hash));
  return SecurityLoginStatus::Created;
}

bool SecuritySessionService::validate(const char* token, uint32_t nowMs) {
  synchronizeSecurity();
  expireSessions(nowMs);
  if (security_.state() != SecurityState::Protected) return false;
  uint8_t hash[kSecurityVerifierSize];
  if (!tokenHash(token, hash)) return false;
  const int match = matchingSession(hash);
  secureZero(hash, sizeof(hash));
  if (match < 0) return false;
  sessions_[static_cast<size_t>(match)].lastActivityMs = nowMs;
  return true;
}

bool SecuritySessionService::logout(const char* token) {
  synchronizeSecurity();
  uint8_t hash[kSecurityVerifierSize];
  if (!tokenHash(token, hash)) return false;
  const int match = matchingSession(hash);
  secureZero(hash, sizeof(hash));
  if (match < 0) return false;
  clearSession(sessions_[static_cast<size_t>(match)]);
  return true;
}

void SecuritySessionService::invalidateAll() {
  for (size_t index = 0; index < kMaxSecuritySessions; ++index)
    clearSession(sessions_[index]);
}

size_t SecuritySessionService::activeSessionCount(uint32_t nowMs) {
  synchronizeSecurity();
  expireSessions(nowMs);
  size_t count = 0;
  for (size_t index = 0; index < kMaxSecuritySessions; ++index)
    if (sessions_[index].active) ++count;
  return count;
}

bool SecuritySessionService::rateLimited(uint32_t nowMs) {
  synchronizeSecurity();
  refreshRateLimit(nowMs);
  return lockoutActive_;
}

void SecuritySessionService::synchronizeSecurity() {
  if (observedState_ == security_.state() &&
      observedGeneration_ == security_.generation())
    return;
  invalidateAll();
  resetRateLimit();
  observedState_ = security_.state();
  observedGeneration_ = security_.generation();
}

void SecuritySessionService::expireSessions(uint32_t nowMs) {
  for (size_t index = 0; index < kMaxSecuritySessions; ++index) {
    Session& session = sessions_[index];
    if (!session.active) continue;
    if (elapsedAtLeast(nowMs, session.createdAtMs,
                       kSecuritySessionAbsoluteTimeoutMs) ||
        elapsedAtLeast(nowMs, session.lastActivityMs,
                       kSecuritySessionIdleTimeoutMs))
      clearSession(session);
  }
}

void SecuritySessionService::refreshRateLimit(uint32_t nowMs) {
  if (lockoutActive_) {
    if (!elapsedAtLeast(nowMs, lockoutStartedAtMs_,
                        kSecurityLoginLockoutMs))
      return;
    resetRateLimit();
    return;
  }
  if (failureWindowActive_ &&
      elapsedAtLeast(nowMs, failureWindowStartedAtMs_,
                     kSecurityLoginFailureWindowMs))
    resetRateLimit();
}

void SecuritySessionService::resetRateLimit() {
  failedAttempts_ = 0;
  failureWindowStartedAtMs_ = 0;
  lockoutStartedAtMs_ = 0;
  failureWindowActive_ = false;
  lockoutActive_ = false;
}

SecurityLoginStatus SecuritySessionService::recordFailedLogin(uint32_t nowMs) {
  if (!failureWindowActive_) {
    failureWindowActive_ = true;
    failureWindowStartedAtMs_ = nowMs;
  }
  if (failedAttempts_ < kSecurityLoginFailureLimit) ++failedAttempts_;
  if (failedAttempts_ < kSecurityLoginFailureLimit)
    return SecurityLoginStatus::InvalidPassword;
  lockoutActive_ = true;
  lockoutStartedAtMs_ = nowMs;
  return SecurityLoginStatus::RateLimited;
}

bool SecuritySessionService::tokenHash(
    const char* token, uint8_t output[kSecurityVerifierSize]) const {
  if (!token || !output) return false;
  size_t length = 0;
  while (length <= kSecuritySessionTokenHexLength && token[length]) ++length;
  if (length != kSecuritySessionTokenHexLength) return false;
  for (size_t index = 0; index < length; ++index)
    if (!isLowerHex(token[index])) return false;
  return securitySha256(reinterpret_cast<const uint8_t*>(token), length,
                        output);
}

int SecuritySessionService::matchingSession(
    const uint8_t hash[kSecurityVerifierSize]) const {
  int match = -1;
  for (size_t index = 0; index < kMaxSecuritySessions; ++index) {
    const bool equal = constantTimeEqual(hash, sessions_[index].tokenHash,
                                         kSecurityVerifierSize);
    if (sessions_[index].active && equal) match = static_cast<int>(index);
  }
  return match;
}

size_t SecuritySessionService::targetSession(uint32_t nowMs) const {
  for (size_t index = 0; index < kMaxSecuritySessions; ++index)
    if (!sessions_[index].active) return index;
  const uint32_t firstIdle = nowMs - sessions_[0].lastActivityMs;
  const uint32_t secondIdle = nowMs - sessions_[1].lastActivityMs;
  return secondIdle > firstIdle ? 1 : 0;
}

void SecuritySessionService::clearSession(Session& session) {
  secureZero(&session, sizeof(session));
}

}  // namespace security
}  // namespace voxone
