#include "../src/core/security_api.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using namespace voxone::security;

namespace {

class FakeStorage : public SecurityStorage {
 public:
  SecurityStorageReadStatus read(uint8_t* output, size_t capacity,
                                 size_t& outputSize) override {
    outputSize = 0;
    if (readError) return SecurityStorageReadStatus::Error;
    if (!present) return SecurityStorageReadStatus::Missing;
    outputSize = size;
    if (size <= capacity) std::memcpy(output, bytes, size);
    return SecurityStorageReadStatus::Found;
  }

  bool write(const uint8_t* input, size_t inputSize) override {
    if (writeError || inputSize > sizeof(bytes)) return false;
    std::memcpy(bytes, input, inputSize);
    size = inputSize;
    present = true;
    return true;
  }

  bool erase() override { return true; }

  uint8_t bytes[kSecurityRecordSize]{};
  size_t size = 0;
  bool present = false;
  bool readError = false;
  bool writeError = false;
};

class DeterministicRandom : public SecurityRandomSource {
 public:
  bool fill(uint8_t* output, size_t size) override {
    if (fail) return false;
    for (size_t index = 0; index < size; ++index)
      output[index] = static_cast<uint8_t>(next + index);
    ++next;
    return true;
  }

  uint8_t next = 1;
  bool fail = false;
};

struct Fixture {
  Fixture(bool wifiConfigured = true,
          SecurityHttpEnforcementMode enforcementMode =
              SecurityHttpEnforcementMode::Disabled)
      : security(storage, random), sessions(security, random),
        api(security, sessions, enforcementMode) {
    security.begin(wifiConfigured);
  }

  FakeStorage storage;
  DeterministicRandom random;
  SecurityService security;
  SecuritySessionService sessions;
  SecurityApiService api;
};

SecurityRequestContext request(const char* host = "192.168.1.44",
                               const char* origin =
                                   "http://192.168.1.44",
                               const char* contentType =
                                   "application/x-www-form-urlencoded",
                               size_t contentLength = 20) {
  return {host, origin, "192.168.1.44", "VoxOne-A1B2C3", contentType,
          contentLength};
}

std::string readFile(const char* path) {
  std::ifstream input(path);
  assert(input.good());
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

void testHostOriginCsrfAndBodyPolicy() {
  assert(validateSecurityReadRequest(request()) ==
         SecurityRequestStatus::Allowed);
  assert(validateSecurityReadRequest(request("192.168.1.44:80")) ==
         SecurityRequestStatus::Allowed);
  assert(validateSecurityReadRequest(request("voxone-a1b2c3")) ==
         SecurityRequestStatus::Allowed);
  assert(validateSecurityReadRequest(request("VoxOne-A1B2C3.local")) ==
         SecurityRequestStatus::Allowed);
  assert(validateSecurityReadRequest(request("voxone-a1b2c3.local.evil")) ==
         SecurityRequestStatus::InvalidHost);
  assert(validateSecurityReadRequest(request("evil.example")) ==
         SecurityRequestStatus::InvalidHost);
  assert(validateSecurityReadRequest(request("192.168.1.44:8080")) ==
         SecurityRequestStatus::InvalidHost);

  assert(validateSecurityFormRequest(request()) ==
         SecurityRequestStatus::Allowed);
  assert(validateSecurityFormRequest(
             request("VoxOne-A1B2C3.local",
                     "http://voxone-a1b2c3.local:80")) ==
         SecurityRequestStatus::Allowed);
  assert(validateSecurityFormRequest(request("192.168.1.44", nullptr)) ==
         SecurityRequestStatus::InvalidOrigin);
  assert(validateSecurityFormRequest(
             request("192.168.1.44", "http://evil.example")) ==
         SecurityRequestStatus::InvalidOrigin);
  assert(validateSecurityFormRequest(
             request("192.168.1.44", "https://192.168.1.44")) ==
         SecurityRequestStatus::InvalidOrigin);
  assert(validateSecurityFormRequest(
             request("192.168.1.44", "http://192.168.1.44/path")) ==
         SecurityRequestStatus::InvalidOrigin);
  assert(validateSecurityFormRequest(
             request("192.168.1.44", "http://192.168.1.44",
                     "application/json")) ==
         SecurityRequestStatus::UnsupportedMediaType);
  assert(validateSecurityFormRequest(
             request("192.168.1.44", "http://192.168.1.44",
                     "application/x-www-form-urlencoded", 0)) ==
         SecurityRequestStatus::BodyRequired);
  assert(validateSecurityFormRequest(
             request("192.168.1.44", "http://192.168.1.44",
                     "application/x-www-form-urlencoded",
                     kSecurityMaxRequestBodyBytes + 1)) ==
         SecurityRequestStatus::BodyTooLarge);

  assert(validateSecurityActionRequest(
             request("192.168.1.44", "http://192.168.1.44", "", 0)) ==
         SecurityRequestStatus::Allowed);
  assert(validateSecurityActionRequest(request()) ==
         SecurityRequestStatus::BodyTooLarge);
}

void testCookieParsing() {
  const char valid[] = "0123456789abcdef0123456789abcdef";
  char cookie[160];
  std::snprintf(cookie, sizeof(cookie), "theme=dark; %s=%s; mode=radio",
                kSecuritySessionCookieName, valid);
  char token[kSecuritySessionTokenBufferSize];
  assert(securitySessionTokenFromCookie(cookie, token));
  assert(std::strcmp(token, valid) == 0);
  assert(!securitySessionTokenFromCookie("voxone_session=short", token));
  assert(!securitySessionTokenFromCookie(
      "voxone_session=0123456789ABCDEF0123456789ABCDEF", token));
  std::snprintf(cookie, sizeof(cookie), "%s=%s; %s=%s",
                kSecuritySessionCookieName, valid,
                kSecuritySessionCookieName, valid);
  assert(!securitySessionTokenFromCookie(cookie, token));
}

void testStateMachineAndSetup() {
  Fixture firstBoot(false);
  SecurityApiState state = firstBoot.api.state(nullptr, 1);
  assert(state.state == SecurityState::Unconfigured);
  assert(!state.setupAllowed && !state.loginAllowed &&
         !state.protectionEnforced);
  assert(state.enforcementMode == SecurityHttpEnforcementMode::Disabled);
  assert(firstBoot.api.setup("open", nullptr).httpStatus == 409);

  Fixture open;
  state = open.api.state(nullptr, 1);
  assert(state.state == SecurityState::LegacyUnclaimed &&
         state.setupAllowed);
  SecurityApiResult setup = open.api.setup("open", nullptr);
  assert(setup.httpStatus == 200 && setup.error == SecurityApiError::None);
  assert(open.security.state() == SecurityState::Open);
  assert(open.api.setup("protected", "abcd").httpStatus == 409);
  assert(open.api.login("abcd", 2).httpStatus == 409);

  Fixture protectedDevice;
  assert(protectedDevice.api.setup("invalid", nullptr).httpStatus == 400);
  assert(protectedDevice.api.setup("open", "unexpected").httpStatus == 400);
  assert(protectedDevice.api.setup("protected", "abc").httpStatus == 422);
  setup = protectedDevice.api.setup("protected", "hasło");
  assert(setup.httpStatus == 200);
  assert(protectedDevice.security.state() == SecurityState::Protected);
  assert(protectedDevice.api.setup("open", nullptr).httpStatus == 409);

  char json[kSecurityStateJsonSize];
  state = protectedDevice.api.state(nullptr, 3);
  assert(securityStateJson(state, json, sizeof(json)));
  assert(std::strstr(json, "\"state\":\"protected\"") != nullptr);
  assert(std::strstr(json, "\"protectionEnforced\":false") != nullptr);
  assert(std::strstr(json, "\"enforcementMode\":\"disabled\"") != nullptr);
  assert(std::strstr(json, "hasło") == nullptr);
  assert(std::strstr(json, "salt") == nullptr);
  assert(std::strstr(json, "verifier") == nullptr);
  assert(std::strstr(json, "token") == nullptr);

  Fixture testProtected(true, SecurityHttpEnforcementMode::Test);
  assert(testProtected.api.setup("protected", "hasło").httpStatus == 200);
  state = testProtected.api.state(nullptr, 4);
  assert(state.protectionEnforced &&
         state.enforcementMode == SecurityHttpEnforcementMode::Test);
  assert(securityStateJson(state, json, sizeof(json)));
  assert(std::strstr(json, "\"protectionEnforced\":true") != nullptr);
  assert(std::strstr(json, "\"enforcementMode\":\"test\"") != nullptr);

  Fixture productionProtected(true, SecurityHttpEnforcementMode::Production);
  assert(productionProtected.api.setup("protected", "hasło").httpStatus == 200);
  state = productionProtected.api.state(nullptr, 5);
  assert(state.protectionEnforced &&
         state.enforcementMode == SecurityHttpEnforcementMode::Production);

  Fixture locked;
  locked.storage.readError = true;
  assert(locked.security.begin(true) == SecurityLoadStatus::StorageError);
  assert(locked.api.setup("open", nullptr).httpStatus == 423);
  assert(locked.api.login("abcd", 1).httpStatus == 423);
}

void testLoginLogoutSessionsAndRateLimit() {
  Fixture fixture;
  assert(fixture.api.setup("protected", "hasło").httpStatus == 200);
  SecurityApiResult login = fixture.api.login("wrong", 10);
  assert(login.httpStatus == 401 &&
         login.error == SecurityApiError::AuthenticationFailed);
  login = fixture.api.login("hasło", 11);
  assert(login.httpStatus == 200 && login.setSessionCookie);
  assert(std::strlen(login.sessionToken) == kSecuritySessionTokenHexLength);
  SecurityApiState state = fixture.api.state(login.sessionToken, 12);
  assert(state.authenticated);
  SecurityApiResult logout = fixture.api.logout(login.sessionToken);
  assert(logout.httpStatus == 204 && logout.clearSessionCookie);
  assert(!fixture.api.state(login.sessionToken, 13).authenticated);

  Fixture limited;
  assert(limited.api.setup("protected", "abcd").httpStatus == 200);
  for (uint8_t attempt = 1; attempt < kSecurityLoginFailureLimit; ++attempt)
    assert(limited.api.login("wrong", 100 + attempt).httpStatus == 401);
  assert(limited.api.login("wrong", 100 + kSecurityLoginFailureLimit)
             .httpStatus == 429);
  assert(limited.api.login("abcd", 110).httpStatus == 429);
}

void testNormalPanelSecurityManagement() {
  Fixture fixture;
  assert(fixture.api.setup("open", nullptr).httpStatus == 200);

  assert(fixture.api.manage(nullptr, "open", nullptr, 1).httpStatus == 400);
  assert(fixture.api.manage(nullptr, "protected", "abc", 2).httpStatus == 422);
  SecurityApiResult enabled =
      fixture.api.manage(nullptr, "protected", "abcd", 3);
  assert(enabled.httpStatus == 200 && enabled.clearSessionCookie);
  assert(fixture.security.state() == SecurityState::Protected);

  assert(fixture.api.manage(nullptr, "protected", "efgh", 4).httpStatus == 401);
  SecurityApiResult login = fixture.api.login("abcd", 5);
  assert(login.httpStatus == 200 && login.setSessionCookie);
  SecurityApiResult changed =
      fixture.api.manage(login.sessionToken, "protected", "efgh", 6);
  assert(changed.httpStatus == 200 && changed.clearSessionCookie);
  assert(!fixture.api.state(login.sessionToken, 7).authenticated);
  assert(fixture.api.login("abcd", 8).httpStatus == 401);

  login = fixture.api.login("efgh", 9);
  assert(login.httpStatus == 200);
  SecurityApiResult disabled =
      fixture.api.manage(login.sessionToken, "open", nullptr, 10);
  assert(disabled.httpStatus == 200 && disabled.clearSessionCookie);
  assert(fixture.security.state() == SecurityState::Open);
  assert(!fixture.api.state(login.sessionToken, 11).authenticated);
}

void testStorageAndRandomFailures() {
  Fixture storageFailure;
  storageFailure.storage.writeError = true;
  SecurityApiResult setup = storageFailure.api.setup("open", nullptr);
  assert(setup.httpStatus == 500 &&
         setup.error == SecurityApiError::StorageError);

  Fixture saltFailure;
  saltFailure.random.fail = true;
  setup = saltFailure.api.setup("protected", "abcd");
  assert(setup.httpStatus == 500 &&
         setup.error == SecurityApiError::RandomError);

  Fixture tokenFailure;
  assert(tokenFailure.api.setup("protected", "abcd").httpStatus == 200);
  tokenFailure.random.fail = true;
  SecurityApiResult login = tokenFailure.api.login("abcd", 1);
  assert(login.httpStatus == 500 &&
         login.error == SecurityApiError::RandomError &&
         !login.setSessionCookie);
}

void testProductionIntegrationContract() {
  const std::string network = readFile("src/core/network.cpp");
  const std::string server = readFile("src/core/netserver.cpp");
  const std::string mqtt = readFile("src/core/mqtt.cpp");

  const size_t settings = network.find("initializeDeviceSecurity(");
  const size_t sessions = network.find("deviceSecuritySessionService()");
  const size_t wifi = network.find("setWifiParams();");
  assert(settings != std::string::npos && sessions != std::string::npos &&
         wifi != std::string::npos && settings < sessions && sessions < wifi);
  assert(network.find("enforcement=%s") != std::string::npos);

  assert(server.find("kSecurityStateRoute, HTTP_GET") != std::string::npos);
  assert(server.find("kSecuritySetupRoute, HTTP_POST") != std::string::npos);
  assert(server.find("kSecurityLoginRoute, HTTP_POST") != std::string::npos);
  assert(server.find("kSecurityLogoutRoute, HTTP_POST") != std::string::npos);
  assert(server.find("kSecurityManageRoute, HTTP_POST") != std::string::npos);
  assert(server.find("HttpOnly; SameSite=Strict; Max-Age=86400") !=
         std::string::npos);
  assert(server.find("Secure; Max-Age=86400") == std::string::npos);
  assert(server.find("webserver.on(\"/api/ready\", HTTP_GET") !=
         std::string::npos);
  assert(server.find("request->url()==\"/update\"") != std::string::npos);
  assert(mqtt.find("security_api.h") == std::string::npos);
}

}  // namespace

int main() {
  testHostOriginCsrfAndBodyPolicy();
  testCookieParsing();
  testStateMachineAndSetup();
  testLoginLogoutSessionsAndRateLimit();
  testNormalPanelSecurityManagement();
  testStorageAndRandomFailures();
  testProductionIntegrationContract();
  return 0;
}
