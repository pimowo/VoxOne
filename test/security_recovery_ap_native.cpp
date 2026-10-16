#include "../src/core/security_recovery_ap.h"
#include "../src/core/security_recovery_http.h"
#include "../src/core/security_recovery_session.h"
#include "../src/hardware/hardware_descriptor.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

using namespace voxone::security_recovery;

namespace {

GateConditions eligible() {
  return {true, RecoveryImageState::Valid, false, false, true, false};
}

class FakeGateSource : public RecoveryGateSource {
 public:
  GateConditions conditions = eligible();
  unsigned reads = 0;
  GateConditions read() override {
    ++reads;
    return conditions;
  }
};

class SessionControl : public RecoveryApSessionControl {
 public:
  explicit SessionControl(RecoverySession& session) : session_(session) {}
  RecoveryState state() const override { return session_.state(); }
  bool live() const override { return session_.live(); }
  RecoveryTransition activate(uint32_t nowMs) override {
    return session_.activate(nowMs);
  }
  RecoveryTransition service(uint32_t nowMs) override {
    return session_.service(nowMs);
  }
  RecoveryTransition cancel(uint32_t nowMs) override {
    return session_.cancel(nowMs);
  }
  RecoveryTransition fail(uint32_t nowMs) override {
    return session_.fail(nowMs);
  }
  RecoveryTransition complete(bool success, uint32_t nowMs) override {
    return session_.complete(success, nowMs);
  }

 private:
  RecoverySession& session_;
};

class FakePlatform : public RecoveryApPlatform {
 public:
  bool credential = true;
  bool baselineEligible = true;
  bool wifiOk = true;
  bool httpOk = true;
  bool serviceOk = true;
  unsigned wifiStarts = 0;
  unsigned httpStarts = 0;
  unsigned httpServices = 0;
  unsigned stopHttpCalls = 0;
  unsigned disconnectCalls = 0;
  unsigned stopWifiCalls = 0;
  unsigned clearCalls = 0;
  unsigned restartCalls = 0;
  unsigned baselineChecks = 0;

  bool credentialConfigured() const override { return credential; }
  bool securityBaselineEligible() override {
    ++baselineChecks;
    return baselineEligible;
  }
  bool startWifi() override {
    ++wifiStarts;
    return wifiOk;
  }
  bool startHttp() override {
    ++httpStarts;
    return httpOk;
  }
  bool serviceHttp(uint32_t) override {
    ++httpServices;
    return serviceOk;
  }
  void stopHttp() override { ++stopHttpCalls; }
  void disconnectClient() override { ++disconnectCalls; }
  void stopWifi() override { ++stopWifiCalls; }
  void clearTemporary() override { ++clearCalls; }
  void restart() override { ++restartCalls; }

  void assertCleanedOnce() const {
    assert(stopHttpCalls == 1);
    assert(disconnectCalls == 1);
    assert(stopWifiCalls == 1);
    assert(clearCalls == 1);
  }
};

struct Fixture {
  FakeGateSource gate;
  RecoverySession session;
  SessionControl sessionControl;
  FakePlatform platform;
  RecoveryApController controller;

  Fixture()
      : session(gate), sessionControl(session),
        controller(sessionControl, platform) {}

  void ready(uint32_t now = 0) {
    assert(session.begin(KeyState::Accepted, now).to == RecoveryState::Ready);
  }
};

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

RecoveryHttpDecision request(const char* text, bool allowed = true) {
  return evaluateRecoveryHttpRequest(text, std::strlen(text), "192.168.4.1",
                                     allowed);
}

RecoveryHttpDecision authenticatedRequest(const std::string& text,
                                          const char* token,
                                          RecoveryHttpRequest& parsed,
                                          bool allowed = true) {
  return evaluateRecoveryHttpRequest(text.data(), text.size(),
                                     "192.168.4.1", token, allowed, parsed);
}

}  // namespace

int main() {
  Fixture active;
  active.ready();
  assert(active.gate.reads == 1);
  assert(active.controller.start(1) == RecoveryApStartResult::Active);
  assert(active.session.state() == RecoveryState::Active);
  assert(active.gate.reads == 2);
  assert(active.platform.baselineChecks == 1);
  assert(active.controller.ownsRuntime() && active.controller.running());
  assert(active.platform.wifiStarts == 1 && active.platform.httpStarts == 1);

  Fixture cancelledKey;
  assert(cancelledKey.session.begin(KeyState::Cancelled, 0).to ==
         RecoveryState::Idle);
  assert(cancelledKey.controller.start(1) ==
         RecoveryApStartResult::NotRequested);
  assert(cancelledKey.platform.wifiStarts == 0);

  Fixture stuckKey;
  assert(stuckKey.session.begin(KeyState::StuckLow, 0).to ==
         RecoveryState::Idle);
  assert(stuckKey.controller.start(1) == RecoveryApStartResult::NotRequested);

  const GateConditions denied[] = {
      {true, RecoveryImageState::PendingVerify, true, false, true, false},
      {true, RecoveryImageState::Invalid, false, false, true, false},
      {true, RecoveryImageState::Unknown, false, false, true, false},
      {true, RecoveryImageState::Valid, false, true, true, false},
      {true, RecoveryImageState::Valid, false, false, true, true},
      {false, RecoveryImageState::Valid, false, false, true, false},
      {true, RecoveryImageState::Valid, false, false, false, false},
  };
  for (const auto& conditions : denied) {
    Fixture fixture;
    fixture.gate.conditions = conditions;
    assert(fixture.session.begin(KeyState::Accepted, 0).to ==
           RecoveryState::Failed);
    assert(fixture.controller.start(1) == RecoveryApStartResult::NotRequested);
    assert(fixture.platform.wifiStarts == 0);
  }

  Fixture missingCredential;
  missingCredential.ready();
  missingCredential.platform.credential = false;
  assert(missingCredential.controller.start(1) ==
         RecoveryApStartResult::CredentialUnavailable);
  assert(missingCredential.session.state() == RecoveryState::Cancelled);
  assert(!missingCredential.controller.ownsRuntime());
  assert(missingCredential.platform.wifiStarts == 0);

  Fixture staleGateDenied;
  staleGateDenied.ready();
  staleGateDenied.gate.conditions.otaUploadActive = true;
  assert(staleGateDenied.controller.start(1) ==
         RecoveryApStartResult::GateDenied);
  assert(staleGateDenied.session.state() == RecoveryState::Failed);
  assert(staleGateDenied.platform.baselineChecks == 0);
  assert(staleGateDenied.platform.wifiStarts == 0);

  Fixture baselineRejected;
  baselineRejected.ready();
  baselineRejected.platform.baselineEligible = false;
  assert(baselineRejected.controller.start(1) ==
         RecoveryApStartResult::BaselineRejected);
  assert(baselineRejected.session.state() == RecoveryState::Failed);
  assert(baselineRejected.platform.baselineChecks == 1);
  assert(!baselineRejected.controller.ownsRuntime());
  assert(baselineRejected.platform.wifiStarts == 0);
  baselineRejected.controller.complete(true, 2);
  assert(baselineRejected.platform.restartCalls == 0);

  Fixture wifiFailure;
  wifiFailure.ready();
  wifiFailure.platform.wifiOk = false;
  assert(wifiFailure.controller.start(1) == RecoveryApStartResult::WifiFailed);
  assert(wifiFailure.controller.ownsRuntime());
  assert(!wifiFailure.controller.running());
  assert(wifiFailure.session.state() == RecoveryState::Failed);
  wifiFailure.platform.assertCleanedOnce();

  Fixture httpFailure;
  httpFailure.ready();
  httpFailure.platform.httpOk = false;
  assert(httpFailure.controller.start(1) == RecoveryApStartResult::HttpFailed);
  assert(httpFailure.controller.ownsRuntime());
  assert(httpFailure.session.state() == RecoveryState::Failed);
  httpFailure.platform.assertCleanedOnce();

  Fixture timeout;
  timeout.ready(20);
  assert(timeout.controller.start(21) == RecoveryApStartResult::Active);
  assert(timeout.controller.service(20 + 599999U));
  assert(timeout.controller.running());
  assert(timeout.controller.service(20 + 600000U));
  assert(timeout.session.state() == RecoveryState::Timeout);
  assert(!timeout.controller.running());
  timeout.platform.assertCleanedOnce();
  assert(timeout.platform.restartCalls == 0);
  timeout.controller.service(20 + 600001U);
  timeout.platform.assertCleanedOnce();

  Fixture serviceFailure;
  serviceFailure.ready();
  assert(serviceFailure.controller.start(1) == RecoveryApStartResult::Active);
  serviceFailure.platform.serviceOk = false;
  assert(serviceFailure.controller.service(2));
  assert(serviceFailure.session.state() == RecoveryState::Failed);
  serviceFailure.platform.assertCleanedOnce();
  assert(serviceFailure.platform.restartCalls == 0);

  Fixture cancelled;
  cancelled.ready();
  assert(cancelled.controller.start(1) == RecoveryApStartResult::Active);
  cancelled.controller.cancel(2);
  assert(cancelled.session.state() == RecoveryState::Cancelled);
  assert(!cancelled.controller.running());
  cancelled.platform.assertCleanedOnce();

  Fixture earlyComplete;
  earlyComplete.ready();
  assert(earlyComplete.controller.start(1) == RecoveryApStartResult::Active);
  earlyComplete.controller.complete(true, 2);
  assert(earlyComplete.session.state() == RecoveryState::Failed);
  assert(!earlyComplete.session.live());
  earlyComplete.platform.assertCleanedOnce();
  assert(earlyComplete.platform.restartCalls == 0);

  Fixture committed;
  committed.ready();
  assert(committed.controller.start(1) == RecoveryApStartResult::Active);
  assert(committed.session.awaitConfirmation(2).to ==
         RecoveryState::AwaitConfirmation);
  assert(committed.session.confirm(3).to == RecoveryState::CommitReady);
  assert(committed.session.beginCommit(4).to == RecoveryState::Committing);
  assert(committed.session.complete(true, 5).to == RecoveryState::Completed);
  assert(committed.controller.service(6));
  committed.platform.assertCleanedOnce();
  assert(committed.platform.restartCalls == 1);
  assert(committed.controller.service(7));
  assert(committed.platform.restartCalls == 1);

  const uint32_t wrapStart = UINT32_MAX - 100U;
  Fixture wrap;
  wrap.ready(wrapStart);
  assert(wrap.controller.start(wrapStart + 1U) ==
         RecoveryApStartResult::Active);
  assert(wrap.controller.service(wrapStart + 599999U));
  assert(wrap.controller.running());
  assert(wrap.controller.service(wrapStart + 600000U));
  assert(wrap.session.state() == RecoveryState::Timeout);
  wrap.platform.assertCleanedOnce();

  assert(request("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n") ==
         RecoveryHttpDecision::Root);
  assert(request("GET /status HTTP/1.1\r\nHost: 192.168.4.1:80\r\n\r\n") ==
         RecoveryHttpDecision::Unauthorized);
  assert(request("GET /update HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n") ==
         RecoveryHttpDecision::NotFound);
  assert(request("GET /emergency HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n") ==
         RecoveryHttpDecision::NotFound);
  assert(request("GET /ws HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n") ==
         RecoveryHttpDecision::NotFound);
  assert(request("POST / HTTP/1.1\r\nHost: 192.168.4.1\r\n"
                 "Content-Length: 1\r\n\r\nx") ==
         RecoveryHttpDecision::MethodNotAllowed);
  assert(request("GET / HTTP/1.1\r\nHost: evil.invalid\r\n\r\n") ==
         RecoveryHttpDecision::HostRejected);
  assert(request("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n"
                 "Content-Length: 0\r\n\r\n") ==
         RecoveryHttpDecision::BadRequest);
  assert(request("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n"
                 "Upgrade: websocket\r\n\r\n") ==
         RecoveryHttpDecision::BadRequest);
  assert(request("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n"
                 "Host: 192.168.4.1\r\n\r\n") ==
         RecoveryHttpDecision::BadRequest);
  assert(request("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n", false) ==
         RecoveryHttpDecision::RateLimited);
  char oversized[kRecoveryHttpMaxRequestBytes + 1U];
  std::memset(oversized, 'A', sizeof(oversized));
  assert(evaluateRecoveryHttpRequest(oversized, sizeof(oversized),
                                     "192.168.4.1", true) ==
         RecoveryHttpDecision::PayloadTooLarge);

  size_t expectedLength = 0;
  const char partial[] =
      "POST /prepare HTTP/1.1\r\nHost: 192.168.4.1\r\n"
      "Content-Length: 4\r\n\r\nab";
  assert(inspectRecoveryHttpFraming(partial, std::strlen(partial),
                                    expectedLength) ==
         RecoveryHttpFraming::Incomplete);
  assert(expectedLength == std::strlen(partial) + 2U);
  const char complete[] =
      "POST /prepare HTTP/1.1\r\nHost: 192.168.4.1\r\n"
      "Content-Length: 4\r\n\r\nabcd";
  assert(inspectRecoveryHttpFraming(complete, std::strlen(complete),
                                    expectedLength) ==
         RecoveryHttpFraming::Complete);

  constexpr char token[] =
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  RecoveryHttpRequest parsed{};
  const std::string status =
      std::string("GET /status HTTP/1.1\r\nHost: 192.168.4.1\r\n") +
      "Cookie: voxone_recovery=" + token + "\r\n\r\n";
  assert(authenticatedRequest(status, token, parsed) ==
         RecoveryHttpDecision::Status);
  const std::string body =
      "pin=1293&new_password=abcd&repeat_password=abcd&csrf=token";
  const std::string prepare =
      std::string("POST /prepare HTTP/1.1\r\nHost: 192.168.4.1\r\n") +
      "Origin: http://192.168.4.1\r\nCookie: voxone_recovery=" + token +
      "\r\nContent-Type: application/x-www-form-urlencoded\r\n"
      "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
  assert(authenticatedRequest(prepare, token, parsed) ==
         RecoveryHttpDecision::Prepare);
  assert(parsed.bodyLength == body.size());
  assert(prepare.substr(parsed.bodyOffset, parsed.bodyLength) == body);
  std::string foreignOrigin = prepare;
  foreignOrigin.replace(foreignOrigin.find("http://192.168.4.1"),
                        std::strlen("http://192.168.4.1"),
                        "http://evil.invalid");
  assert(authenticatedRequest(foreignOrigin, token, parsed) ==
         RecoveryHttpDecision::OriginRejected);
  const std::string noOrigin =
      std::string("POST /prepare HTTP/1.1\r\nHost: 192.168.4.1\r\n") +
      "Cookie: voxone_recovery=" + token +
      "\r\nContent-Type: application/x-www-form-urlencoded\r\n"
      "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
  assert(authenticatedRequest(noOrigin, token, parsed) ==
         RecoveryHttpDecision::OriginRejected);
  assert(authenticatedRequest(prepare, "bad", parsed) ==
         RecoveryHttpDecision::Unauthorized);

  const auto& hardware = voxone::hardware::currentHardware();
#if defined(VOXONE_PROFILE_W0)
  assert(hardware.localInput.key == 32);
#elif defined(VOXONE_PROFILE_L0)
  assert(hardware.localInput.key == 39);
#else
  assert(hardware.localInput.key == 4);
#endif

  const std::string core = readFile("src/core/security_recovery_ap.cpp");
  const std::string runtime =
      readFile("src/core/security_recovery_ap_runtime.cpp");
  const std::string main = readFile("src/main.cpp");
  const std::string platformio = readFile("platformio.ini");
  const std::string credentialScript =
      readFile("scripts/recovery_ap_credential.py");
  const std::string gitignore = readFile(".gitignore");
  const std::string combined = core + runtime;
  assert(combined.find("1293") == std::string::npos);
  assert(combined.find("AsyncWebServer") == std::string::npos);
  assert(combined.find("Access-Control-Allow-Origin") == std::string::npos);
  assert(combined.find("nvs_") == std::string::npos);
  assert(combined.find("Preferences") == std::string::npos);
  assert(combined.find("esp_ota_mark_app_valid") == std::string::npos);
  assert(combined.find("esp_ota_mark_app_invalid") == std::string::npos);
  assert(core.find("platform_.restart()") != std::string::npos);
  const size_t activated = core.find("session_.activate(nowMs)");
  const size_t baseline = core.find("platform_.securityBaselineEligible()");
  const size_t wifi = core.find("platform_.startWifi()");
  assert(activated < baseline && baseline < wifi);
  assert(runtime.find("ESP.restart()") != std::string::npos);
  assert(runtime.find("inspectDeviceSecurityRecoveryBaseline()") !=
         std::string::npos);
  assert(runtime.find("Serial") == std::string::npos);
  assert(runtime.find("VOXONE_SECURITY_RECOVERY_AP_PASSWORD") !=
         std::string::npos);
  assert(runtime.find("WiFi.mode(WIFI_AP)") != std::string::npos);
  assert(runtime.find("WiFi.softAP(ssid_, kRecoveryApPassword, 1, 0, 1)") !=
         std::string::npos);
  assert(runtime.find("Cache-Control: no-store") != std::string::npos);
  assert(runtime.find("X-Content-Type-Options: nosniff") !=
         std::string::npos);
  assert(runtime.find("beginRecoveryCommit(nowMs)") != std::string::npos);
  assert(runtime.find("commitDeviceSecurityRecovery(pinSession_.candidate())") !=
         std::string::npos);
  assert(runtime.find("beginRecoveryCommit(nowMs)") <
         runtime.find("commitDeviceSecurityRecovery(pinSession_.candidate())"));
  assert(runtime.find("securityValidateAdminPassword") == std::string::npos);
  assert(runtime.find("persistentWrite\\\":true") != std::string::npos);
  assert(platformio.find("pre:scripts/recovery_ap_credential.py") !=
         std::string::npos);
  assert(platformio.find("VOXONE_SECURITY_RECOVERY_AP_PASSWORD") ==
         std::string::npos);
  assert(credentialScript.find("CPPDEFINES") == std::string::npos);
  assert(credentialScript.find("CCFLAGS") != std::string::npos);
  assert(credentialScript.find(".voxone-recovery-ap-password") !=
         std::string::npos);
  assert(credentialScript.find("print(") == std::string::npos);
  assert(gitignore.find(".voxone-recovery-ap-password") !=
         std::string::npos);
  assert(main.find("beginSecurityRecoveryAp(") >
         main.find("recoverAtBoot()"));
  assert(main.find("beginSecurityRecoveryAp(") < main.find("config.init()"));
  const size_t loop = main.find("void loop()");
  assert(main.find("securityRecoveryApOwnsRuntime()", loop) <
         main.find("timekeeper.loop1()", loop));
}
