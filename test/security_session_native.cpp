#include "../src/core/security_session.h"

#include <cassert>
#include <cstdint>
#include <cstring>

using namespace voxone::security;

namespace {

class FakeStorage : public SecurityStorage {
 public:
  SecurityStorageReadStatus read(uint8_t* output, size_t capacity,
                                 size_t& outputSize) override {
    outputSize = 0;
    if (!present) return SecurityStorageReadStatus::Missing;
    outputSize = size;
    if (size <= capacity) std::memcpy(output, bytes, size);
    return SecurityStorageReadStatus::Found;
  }

  bool write(const uint8_t* input, size_t inputSize) override {
    if (inputSize > sizeof(bytes)) return false;
    std::memcpy(bytes, input, inputSize);
    size = inputSize;
    present = true;
    return true;
  }

  bool erase() override {
    std::memset(bytes, 0, sizeof(bytes));
    size = 0;
    present = false;
    return true;
  }

  uint8_t bytes[kSecurityRecordSize]{};
  size_t size = 0;
  bool present = false;
};

class DeterministicRandom : public SecurityRandomSource {
 public:
  bool fill(uint8_t* output, size_t size) override {
    ++calls;
    if (fail) return false;
    for (size_t index = 0; index < size; ++index)
      output[index] = zero ? 0 : static_cast<uint8_t>(next + index);
    if (!repeat) ++next;
    return true;
  }

  uint8_t next = 1;
  unsigned calls = 0;
  bool fail = false;
  bool zero = false;
  bool repeat = false;
};

struct Fixture {
  Fixture() : security(storage, random), sessions(security, random) {
    assert(security.begin(true) == SecurityLoadStatus::LegacyUnclaimed);
    assert(security.chooseProtected("abcd") ==
           SecurityOperationStatus::Applied);
  }

  FakeStorage storage;
  DeterministicRandom random;
  SecurityService security;
  SecuritySessionService sessions;
};

void copyToken(char destination[kSecuritySessionTokenBufferSize],
               const char source[kSecuritySessionTokenBufferSize]) {
  std::memcpy(destination, source, kSecuritySessionTokenBufferSize);
}

void testSha256AndSessionLifecycle() {
  uint8_t digest[kSecurityVerifierSize];
  assert(securitySha256(reinterpret_cast<const uint8_t*>("abc"), 3, digest));
  const uint8_t expected[] = {
      0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
      0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
      0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
      0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  assert(std::memcmp(digest, expected, sizeof(expected)) == 0);

  Fixture fixture;
  char token[kSecuritySessionTokenBufferSize];
  assert(fixture.sessions.login("wrong", 100, token) ==
         SecurityLoginStatus::InvalidPassword);
  assert(token[0] == '\0');
  assert(fixture.sessions.login("abcd", 101, token) ==
         SecurityLoginStatus::Created);
  assert(std::strlen(token) == kSecuritySessionTokenHexLength);
  assert(fixture.sessions.activeSessionCount(101) == 1);
  assert(fixture.sessions.validate(token, 102));
  assert(!fixture.sessions.validate("00000000000000000000000000000000",
                                    103));
  assert(!fixture.sessions.validate("ABCDEF00000000000000000000000000",
                                    103));
  assert(fixture.sessions.logout(token));
  assert(!fixture.sessions.logout(token));
  assert(fixture.sessions.activeSessionCount(104) == 0);
}

void testTwoSessionLimitEvictsLeastRecentlyUsed() {
  Fixture fixture;
  char output[kSecuritySessionTokenBufferSize];
  char first[kSecuritySessionTokenBufferSize];
  char second[kSecuritySessionTokenBufferSize];
  char third[kSecuritySessionTokenBufferSize];
  assert(fixture.sessions.login("abcd", 100, output) ==
         SecurityLoginStatus::Created);
  copyToken(first, output);
  assert(fixture.sessions.login("abcd", 200, output) ==
         SecurityLoginStatus::Created);
  copyToken(second, output);
  assert(fixture.sessions.validate(first, 300));
  assert(fixture.sessions.login("abcd", 400, output) ==
         SecurityLoginStatus::Created);
  copyToken(third, output);
  assert(fixture.sessions.activeSessionCount(400) == 2);
  assert(fixture.sessions.validate(first, 401));
  assert(!fixture.sessions.validate(second, 401));
  assert(fixture.sessions.validate(third, 401));
}

void testIdleAbsoluteAndMillisWrapTimeouts() {
  Fixture idle;
  char token[kSecuritySessionTokenBufferSize];
  assert(idle.sessions.login("abcd", 100, token) ==
         SecurityLoginStatus::Created);
  assert(!idle.sessions.validate(
      token, static_cast<uint32_t>(100 + kSecuritySessionIdleTimeoutMs)));

  Fixture active;
  const uint32_t started = 1000;
  assert(active.sessions.login("abcd", started, token) ==
         SecurityLoginStatus::Created);
  assert(active.sessions.validate(token, started + 7UL * 60UL * 60UL * 1000UL));
  assert(active.sessions.validate(token, started + 14UL * 60UL * 60UL * 1000UL));
  assert(active.sessions.validate(token, started + 21UL * 60UL * 60UL * 1000UL));
  assert(!active.sessions.validate(
      token, static_cast<uint32_t>(started +
                                   kSecuritySessionAbsoluteTimeoutMs)));

  Fixture wrapping;
  const uint32_t nearWrap = 0xfffffff0U;
  assert(wrapping.sessions.login("abcd", nearWrap, token) ==
         SecurityLoginStatus::Created);
  assert(wrapping.sessions.validate(token, nearWrap + 1000U));
  assert(!wrapping.sessions.validate(
      token, static_cast<uint32_t>(nearWrap + 1000U +
                                   kSecuritySessionIdleTimeoutMs)));
}

void testRngFailuresAndDuplicateTokens() {
  Fixture fixture;
  char token[kSecuritySessionTokenBufferSize];
  fixture.random.fail = true;
  assert(fixture.sessions.login("abcd", 1, token) ==
         SecurityLoginStatus::RandomError);
  assert(token[0] == '\0');
  fixture.random.fail = false;
  fixture.random.zero = true;
  assert(fixture.sessions.login("abcd", 2, token) ==
         SecurityLoginStatus::RandomError);
  fixture.random.zero = false;
  fixture.random.repeat = true;
  assert(fixture.sessions.login("abcd", 3, token) ==
         SecurityLoginStatus::Created);
  char duplicate[kSecuritySessionTokenBufferSize];
  assert(fixture.sessions.login("abcd", 4, duplicate) ==
         SecurityLoginStatus::RandomError);
  assert(duplicate[0] == '\0');
  assert(fixture.sessions.activeSessionCount(4) == 1);
}

void testSecurityChangesLogoutAndRestart() {
  Fixture fixture;
  char token[kSecuritySessionTokenBufferSize];
  assert(fixture.sessions.login("abcd", 10, token) ==
         SecurityLoginStatus::Created);
  assert(fixture.security.changePassword("abcd", "nowe") ==
         SecurityOperationStatus::Applied);
  assert(!fixture.sessions.validate(token, 11));
  assert(fixture.sessions.activeSessionCount(11) == 0);

  assert(fixture.sessions.login("nowe", 12, token) ==
         SecurityLoginStatus::Created);
  SecuritySessionService restarted(fixture.security, fixture.random);
  assert(!restarted.validate(token, 13));
  assert(restarted.activeSessionCount(13) == 0);

  assert(fixture.security.unprotect("nowe") ==
         SecurityOperationStatus::Applied);
  assert(!fixture.sessions.validate(token, 14));
  assert(fixture.sessions.login("nowe", 14, token) ==
         SecurityLoginStatus::InvalidState);
  assert(fixture.security.protectOpen("inne") ==
         SecurityOperationStatus::Applied);
  assert(fixture.sessions.login("inne", 15, token) ==
         SecurityLoginStatus::Created);
  fixture.sessions.invalidateAll();
  assert(!fixture.sessions.validate(token, 16));
}

void testLongLivedConnectionBindings() {
  Fixture fixture;
  char first[kSecuritySessionTokenBufferSize];
  char second[kSecuritySessionTokenBufferSize];
  char third[kSecuritySessionTokenBufferSize];
  assert(fixture.sessions.login("abcd", 100, first) ==
         SecurityLoginStatus::Created);
  SecuritySessionBinding binding{};
  assert(fixture.sessions.bind(first, 101, binding));
  assert(binding.mode == SecuritySessionBindingMode::Protected);
  assert(fixture.sessions.validateBinding(binding, 102, true));

  assert(fixture.sessions.logout(first));
  assert(!fixture.sessions.validateBinding(binding, 103, true));

  assert(fixture.sessions.login("abcd", 200, first) ==
         SecurityLoginStatus::Created);
  assert(fixture.sessions.bind(first, 201, binding));
  assert(fixture.security.changePassword("abcd", "nowe") ==
         SecurityOperationStatus::Applied);
  assert(!fixture.sessions.validateBinding(binding, 202, false));

  assert(fixture.sessions.login("nowe", 300, first) ==
         SecurityLoginStatus::Created);
  assert(fixture.sessions.bind(first, 301, binding));
  assert(fixture.security.unprotect("nowe") ==
         SecurityOperationStatus::Applied);
  assert(!fixture.sessions.validateBinding(binding, 302, false));

  SecuritySessionBinding openBinding{};
  assert(fixture.sessions.bind(nullptr, 303, openBinding));
  assert(openBinding.mode == SecuritySessionBindingMode::Open);
  assert(fixture.sessions.validateBinding(openBinding, 304, false));
  assert(fixture.security.protectOpen("inne") ==
         SecurityOperationStatus::Applied);
  assert(!fixture.sessions.validateBinding(openBinding, 305, false));

  assert(fixture.sessions.login("inne", 400, first) ==
         SecurityLoginStatus::Created);
  assert(fixture.sessions.bind(first, 401, binding));
  assert(!fixture.sessions.validateBinding(
      binding, 401 + kSecuritySessionIdleTimeoutMs, true));

  assert(fixture.sessions.login("inne", 500, first) ==
         SecurityLoginStatus::Created);
  assert(fixture.sessions.bind(first, 501, binding));
  assert(fixture.sessions.validateBinding(
      binding, 500 + 7UL * 60UL * 60UL * 1000UL, true));
  assert(fixture.sessions.validateBinding(
      binding, 500 + 14UL * 60UL * 60UL * 1000UL, true));
  assert(fixture.sessions.validateBinding(
      binding, 500 + 21UL * 60UL * 60UL * 1000UL, true));
  assert(!fixture.sessions.validateBinding(
      binding, 500 + kSecuritySessionAbsoluteTimeoutMs, true));

  assert(fixture.sessions.login("inne", 1000, first) ==
         SecurityLoginStatus::Created);
  SecuritySessionBinding oldest{};
  assert(fixture.sessions.bind(first, 1001, oldest));
  assert(fixture.sessions.login("inne", 1100, second) ==
         SecurityLoginStatus::Created);
  assert(fixture.sessions.login("inne", 1200, third) ==
         SecurityLoginStatus::Created);
  assert(!fixture.sessions.validateBinding(oldest, 1201, false));

  SecuritySessionService::clearBinding(binding);
  assert(binding.mode == SecuritySessionBindingMode::None);
  assert(!fixture.sessions.validateBinding(binding, 1202, false));
}

void testLoginRateLimitAndWrap() {
  Fixture fixture;
  char token[kSecuritySessionTokenBufferSize];
  for (uint8_t attempt = 1; attempt < kSecurityLoginFailureLimit; ++attempt) {
    assert(fixture.sessions.login("wrong", 100 + attempt, token) ==
           SecurityLoginStatus::InvalidPassword);
  }
  const uint32_t lockedAt = 100 + kSecurityLoginFailureLimit;
  assert(fixture.sessions.login("wrong", lockedAt, token) ==
         SecurityLoginStatus::RateLimited);
  assert(fixture.sessions.rateLimited(lockedAt));
  assert(fixture.sessions.login("abcd", lockedAt + 1, token) ==
         SecurityLoginStatus::RateLimited);
  assert(fixture.sessions.login(
             "abcd", static_cast<uint32_t>(lockedAt +
                                            kSecurityLoginLockoutMs),
             token) == SecurityLoginStatus::Created);
  assert(!fixture.sessions.rateLimited(
      static_cast<uint32_t>(lockedAt + kSecurityLoginLockoutMs)));

  Fixture window;
  for (uint8_t attempt = 0; attempt < kSecurityLoginFailureLimit - 1;
       ++attempt) {
    assert(window.sessions.login("wrong", 200 + attempt, token) ==
           SecurityLoginStatus::InvalidPassword);
  }
  assert(window.sessions.login(
             "wrong", static_cast<uint32_t>(200 +
                                             kSecurityLoginFailureWindowMs),
             token) == SecurityLoginStatus::InvalidPassword);

  Fixture wrapping;
  const uint32_t nearWrap = 0xfffffff0U;
  for (uint8_t attempt = 1; attempt < kSecurityLoginFailureLimit; ++attempt) {
    assert(wrapping.sessions.login("wrong", nearWrap + attempt, token) ==
           SecurityLoginStatus::InvalidPassword);
  }
  const uint32_t wrapLockedAt = nearWrap + kSecurityLoginFailureLimit;
  assert(wrapping.sessions.login("wrong", wrapLockedAt, token) ==
         SecurityLoginStatus::RateLimited);
  assert(wrapping.sessions.login(
             "abcd", static_cast<uint32_t>(wrapLockedAt +
                                            kSecurityLoginLockoutMs),
             token) == SecurityLoginStatus::Created);
}

}  // namespace

int main() {
  testSha256AndSessionLifecycle();
  testTwoSessionLimitEvictsLeastRecentlyUsed();
  testIdleAbsoluteAndMillisWrapTimeouts();
  testRngFailuresAndDuplicateTokens();
  testSecurityChangesLogoutAndRestart();
  testLongLivedConnectionBindings();
  testLoginRateLimitAndWrap();
  return 0;
}
