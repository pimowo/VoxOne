#include "security_recovery_ap_runtime.h"

#include "security_recovery_confirmation.h"
#include "security_recovery_http.h"
#include "security_recovery_pin.h"
#include "security_recovery_session_runtime.h"
#include "security_recovery_transaction.h"
#include "../hardware/hardware_descriptor.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <esp_system.h>
#include <cstdio>
#include <cstring>

namespace voxone {
namespace security_recovery {
namespace {

#if defined(VOXONE_SECURITY_RECOVERY_AP_PASSWORD)
constexpr char kRecoveryApPassword[] = VOXONE_SECURITY_RECOVERY_AP_PASSWORD;
constexpr bool kRecoveryApCredentialConfigured = true;
static_assert(sizeof(kRecoveryApPassword) - 1U >= 12U,
              "Recovery AP password must contain at least 12 characters");
static_assert(sizeof(kRecoveryApPassword) - 1U <= 63U,
              "Recovery AP password must contain at most 63 characters");
#else
// Deliberately empty: the build-time shared credential is supplied from the
// ignored local credential file and never stored in the repository.
constexpr char kRecoveryApPassword[] = "";
constexpr bool kRecoveryApCredentialConfigured = false;
#endif

constexpr char kBadRequestBody[] = "Bad Request\n";
constexpr char kUnauthorizedBody[] = "Unauthorized\n";
constexpr char kForbiddenBody[] = "Forbidden\n";
constexpr char kNotFoundBody[] = "Not Found\n";
constexpr char kMethodBody[] = "Method Not Allowed\n";
constexpr char kRateBody[] = "Too Many Requests\n";
constexpr char kPayloadBody[] = "Payload Too Large\n";
constexpr char kMediaTypeBody[] = "Unsupported Media Type\n";
constexpr char kConflictBody[] = "Recovery request already prepared\n";
constexpr char kInternalBody[] = "Recovery unavailable\n";
constexpr char kInvalidPinBody[] = "Invalid recovery credentials\n";
constexpr char kInvalidPasswordBody[] = "Invalid password fields\n";
constexpr char kConfirmationPage[] =
    "<!doctype html><html lang=\"pl\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>VoxOne Recovery</title></head><body><main>"
    "<h1>Potwierdzenie fizyczne</h1>"
    "<p>Zwolnij KEY, a nastepnie przytrzymaj go przez 1 sekunde. "
    "Masz 30 sekund.</p></main></body></html>";

class RuntimeRandomSource : public RecoveryRandomSource {
 public:
  bool fill(uint8_t* output, size_t size) override {
    if (!output && size) return false;
    if (size) esp_fill_random(output, size);
    return true;
  }
};

class RuntimeSessionControl : public RecoveryApSessionControl {
 public:
  RecoveryState state() const override {
    return recoverySessionSnapshot().state;
  }
  bool live() const override { return recoverySessionSnapshot().live; }
  RecoveryTransition activate(uint32_t nowMs) override {
    return activateRecoverySession(nowMs);
  }
  RecoveryTransition service(uint32_t nowMs) override {
    serviceRecoverySession(nowMs);
    const RecoverySessionSnapshot current = recoverySessionSnapshot();
    return {current.state, current.state, current.diagnostic, false};
  }
  RecoveryTransition cancel(uint32_t nowMs) override {
    return cancelRecoverySession(nowMs);
  }
  RecoveryTransition fail(uint32_t nowMs) override {
    return failRecoverySession(nowMs);
  }
  RecoveryTransition complete(bool success, uint32_t nowMs) override {
    return completeRecoverySession(success, nowMs);
  }
};

class RuntimeApPlatform : public RecoveryApPlatform {
 public:
  RuntimeApPlatform() : pinSession_(randomSource_) {}

  bool credentialConfigured() const override {
    return kRecoveryApCredentialConfigured;
  }

  bool securityBaselineEligible() override {
    return inspectDeviceSecurityRecoveryBaseline() ==
           RecoveryBaselineStatus::Eligible;
  }

  bool startWifi() override {
    if (!credentialConfigured()) return false;
    const uint32_t suffix =
        static_cast<uint32_t>(ESP.getEfuseMac() & 0x00ffffffULL);
    const int written = std::snprintf(
        ssid_, sizeof(ssid_), "VoxOne-Recovery-%06lX",
        static_cast<unsigned long>(suffix));
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(ssid_))
      return false;
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    if (!WiFi.mode(WIFI_AP)) return false;
    if (!WiFi.softAP(ssid_, kRecoveryApPassword, 1, 0, 1)) {
      WiFi.mode(WIFI_OFF);
      return false;
    }
    return WiFi.getMode() == WIFI_AP;
  }

  bool startHttp() override {
    const IPAddress address = WiFi.softAPIP();
    const int written = std::snprintf(
        expectedHost_, sizeof(expectedHost_), "%u.%u.%u.%u",
        static_cast<unsigned>(address[0]), static_cast<unsigned>(address[1]),
        static_cast<unsigned>(address[2]), static_cast<unsigned>(address[3]));
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(expectedHost_) ||
        !pinSession_.begin())
      return false;
    server_.begin();
    server_.setNoDelay(true);
    return static_cast<bool>(server_);
  }

  bool serviceHttp(uint32_t nowMs) override {
    if (!server_ || WiFi.getMode() != WIFI_AP ||
        WiFi.softAPgetStationNum() > 1U)
      return false;

    serviceConfirmation(nowMs);
    if (client_ && client_.connected()) {
      if (server_.hasClient()) {
        WiFiClient extra = server_.available();
        extra.stop();
      }
      readClient(nowMs);
      return true;
    }

    client_.stop();
    clearRequest();
    client_ = server_.available();
    if (client_) {
      clientStartedMs_ = nowMs;
      clientActive_ = true;
    }
    return true;
  }

  void stopHttp() override {
    server_.stopAll();
    server_.end();
  }

  void disconnectClient() override {
    client_.stop();
    client_ = WiFiClient();
    clientActive_ = false;
    clearRequest();
  }

  void stopWifi() override {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  void clearTemporary() override {
    pinSession_.clear();
    confirmation_ = ConfirmationKeyDetector();
    confirmationActive_ = false;
    confirmationHandled_ = false;
    std::memset(ssid_, 0, sizeof(ssid_));
    std::memset(expectedHost_, 0, sizeof(expectedHost_));
    clearRequest();
    requestCount_ = 0;
    clientStartedMs_ = 0;
    clientActive_ = false;
  }

  void restart() override { ESP.restart(); }

 private:
  bool keyLow() const {
    const hardware::Pin key = hardware::currentHardware().localInput.key;
    return hardware::hasPin(key) && digitalRead(key) == LOW;
  }

  void serviceConfirmation(uint32_t nowMs) {
    if (!confirmationActive_ || confirmationHandled_) return;
    const ConfirmationKeyState state = confirmation_.update(keyLow(), nowMs);
    if (state == ConfirmationKeyState::Accepted) {
      confirmationHandled_ = true;
      const RecoveryTransition confirmed = confirmRecoveryKey(nowMs);
      if (confirmed.to == RecoveryState::CommitReady) {
        const RecoveryTransition committing = beginRecoveryCommit(nowMs);
        if (committing.to != RecoveryState::Committing) return;
        const RecoveryTransactionResult committed =
            commitDeviceSecurityRecovery(pinSession_.candidate());
        (void)completeRecoverySession(
            committed == RecoveryTransactionResult::Applied, nowMs);
      }
    } else if (state == ConfirmationKeyState::StuckLow) {
      confirmationHandled_ = true;
      (void)failRecoverySession(nowMs);
    }
  }

  void clearRequest() {
    volatile uint8_t* bytes =
        reinterpret_cast<volatile uint8_t*>(request_);
    for (size_t index = 0; index < sizeof(request_); ++index)
      bytes[index] = 0;
    requestLength_ = 0;
  }

  void closeClient() {
    client_.flush();
    client_.stop();
    clientActive_ = false;
    clearRequest();
  }

  void sendResponse(int status, const char* reason, const char* contentType,
                    const char* body, const char* extraHeader = nullptr) {
    char header[768];
    const size_t bodyLength = std::strlen(body);
    const int length = std::snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
        "Cache-Control: no-store, max-age=0\r\nPragma: no-cache\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; "
        "form-action 'self'; base-uri 'none'; frame-ancestors 'none'\r\n"
        "Referrer-Policy: no-referrer\r\n%sConnection: close\r\n\r\n",
        status, reason, contentType, static_cast<unsigned>(bodyLength),
        extraHeader ? extraHeader : "");
    if (length > 0 && static_cast<size_t>(length) < sizeof(header)) {
      client_.write(reinterpret_cast<const uint8_t*>(header),
                    static_cast<size_t>(length));
      client_.write(reinterpret_cast<const uint8_t*>(body), bodyLength);
    }
    closeClient();
  }

  void sendRoot() {
    char body[2048];
    const int length = std::snprintf(
        body, sizeof(body),
        "<!doctype html><html lang=\"pl\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>VoxOne Recovery</title></head><body><main>"
        "<h1>VoxOne SECURITY RECOVERY</h1>"
        "<form method=\"post\" action=\"/prepare\">"
        "<input type=\"hidden\" name=\"csrf\" value=\"%s\">"
        "<label>PIN Recovery<input name=\"pin\" type=\"password\" "
        "inputmode=\"numeric\" pattern=\"[0-9]{4}\" minlength=\"4\" "
        "maxlength=\"4\" required></label>"
        "<label>Nowe haslo<input name=\"new_password\" type=\"password\" "
        "autocomplete=\"new-password\" maxlength=\"64\" required></label>"
        "<label>Powtorz haslo<input name=\"repeat_password\" type=\"password\" "
        "autocomplete=\"new-password\" maxlength=\"64\" required></label>"
        "<button type=\"submit\">Przygotuj zmiane</button></form>"
        "</main></body></html>",
        pinSession_.csrfToken());
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(body)) {
      sendResponse(500, "Internal Server Error", "text/plain; charset=utf-8",
                   kInternalBody);
      return;
    }
    char cookie[160];
    const int cookieLength = std::snprintf(
        cookie, sizeof(cookie),
        "Set-Cookie: voxone_recovery=%s; Path=/; HttpOnly; SameSite=Strict\r\n",
        pinSession_.sessionToken());
    if (cookieLength <= 0 || static_cast<size_t>(cookieLength) >= sizeof(cookie)) {
      sendResponse(500, "Internal Server Error", "text/plain; charset=utf-8",
                   kInternalBody);
      return;
    }
    sendResponse(200, "OK", "text/html; charset=utf-8", body, cookie);
  }

  void sendStatus() {
    const RecoverySessionSnapshot current = recoverySessionSnapshot();
    char body[192];
    const int length = std::snprintf(
        body, sizeof(body),
        "{\"mode\":\"security_recovery\",\"state\":\"%s\","
        "\"diagnostic\":\"%s\",\"persistentWrite\":true}",
        recoveryStateName(current.state),
        recoveryDiagnosticName(current.diagnostic));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(body)) {
      sendResponse(500, "Internal Server Error", "text/plain; charset=utf-8",
                   kInternalBody);
      return;
    }
    sendResponse(200, "OK", "application/json; charset=utf-8", body);
  }

  void handlePrepare(const RecoveryHttpRequest& parsed, uint32_t nowMs) {
    RecoveryFormData form{};
    if (!parseRecoveryForm(request_ + parsed.bodyOffset, parsed.bodyLength,
                           form)) {
      clearRecoveryFormData(form);
      sendResponse(400, "Bad Request", "text/plain; charset=utf-8",
                   kBadRequestBody);
      return;
    }
    const RecoveryPinResult result =
        pinSession_.submit(pinSession_.sessionToken(), form, nowMs);
    clearRecoveryFormData(form);
    switch (result) {
      case RecoveryPinResult::Accepted: {
        const RecoveryTransition awaiting = awaitRecoveryConfirmation(nowMs);
        if (awaiting.to != RecoveryState::AwaitConfirmation) {
          sendResponse(409, "Conflict", "text/plain; charset=utf-8",
                       kConflictBody);
          return;
        }
        const hardware::Pin key = hardware::currentHardware().localInput.key;
        if (!hardware::hasPin(key)) {
          (void)failRecoverySession(nowMs);
          sendResponse(500, "Internal Server Error",
                       "text/plain; charset=utf-8", kInternalBody);
          return;
        }
        pinMode(key, INPUT);
        confirmation_ = ConfirmationKeyDetector();
        confirmation_.begin(keyLow(), nowMs);
        confirmationActive_ = true;
        confirmationHandled_ = false;
        sendResponse(202, "Accepted", "text/html; charset=utf-8",
                     kConfirmationPage);
        return;
      }
      case RecoveryPinResult::InvalidPin:
        sendResponse(401, "Unauthorized", "text/plain; charset=utf-8",
                     kInvalidPinBody, "Retry-After: 1\r\n");
        return;
      case RecoveryPinResult::RetryLater:
        sendResponse(429, "Too Many Requests", "text/plain; charset=utf-8",
                     kRateBody, "Retry-After: 1\r\n");
        return;
      case RecoveryPinResult::AttemptsExhausted:
        sendResponse(403, "Forbidden", "text/plain; charset=utf-8",
                     kForbiddenBody);
        return;
      case RecoveryPinResult::InvalidPassword:
      case RecoveryPinResult::PasswordMismatch:
        sendResponse(400, "Bad Request", "text/plain; charset=utf-8",
                     kInvalidPasswordBody);
        return;
      case RecoveryPinResult::InvalidSession:
      case RecoveryPinResult::InvalidCsrf:
        sendResponse(403, "Forbidden", "text/plain; charset=utf-8",
                     kForbiddenBody);
        return;
      case RecoveryPinResult::Frozen:
        sendResponse(409, "Conflict", "text/plain; charset=utf-8",
                     kConflictBody);
        return;
      case RecoveryPinResult::RandomFailure:
      case RecoveryPinResult::DeriveFailure:
        (void)failRecoverySession(nowMs);
        sendResponse(500, "Internal Server Error", "text/plain; charset=utf-8",
                     kInternalBody);
        return;
      case RecoveryPinResult::MalformedForm:
        sendResponse(400, "Bad Request", "text/plain; charset=utf-8",
                     kBadRequestBody);
        return;
    }
  }

  void respond(const RecoveryHttpRequest& parsed, uint32_t nowMs) {
    switch (parsed.decision) {
      case RecoveryHttpDecision::Root:
        sendRoot();
        return;
      case RecoveryHttpDecision::Status:
        sendStatus();
        return;
      case RecoveryHttpDecision::Prepare:
        handlePrepare(parsed, nowMs);
        return;
      case RecoveryHttpDecision::MethodNotAllowed:
        sendResponse(405, "Method Not Allowed", "text/plain; charset=utf-8",
                     kMethodBody);
        return;
      case RecoveryHttpDecision::NotFound:
        sendResponse(404, "Not Found", "text/plain; charset=utf-8",
                     kNotFoundBody);
        return;
      case RecoveryHttpDecision::RateLimited:
        sendResponse(429, "Too Many Requests", "text/plain; charset=utf-8",
                     kRateBody);
        return;
      case RecoveryHttpDecision::Unauthorized:
        sendResponse(401, "Unauthorized", "text/plain; charset=utf-8",
                     kUnauthorizedBody);
        return;
      case RecoveryHttpDecision::OriginRejected:
      case RecoveryHttpDecision::HostRejected:
      case RecoveryHttpDecision::BadRequest:
        sendResponse(400, "Bad Request", "text/plain; charset=utf-8",
                     kBadRequestBody);
        return;
      case RecoveryHttpDecision::UnsupportedMediaType:
        sendResponse(415, "Unsupported Media Type",
                     "text/plain; charset=utf-8", kMediaTypeBody);
        return;
      case RecoveryHttpDecision::PayloadTooLarge:
        sendResponse(413, "Payload Too Large", "text/plain; charset=utf-8",
                     kPayloadBody);
        return;
    }
  }

  void readClient(uint32_t nowMs) {
    if (clientActive_ &&
        static_cast<uint32_t>(nowMs - clientStartedMs_) >=
            kRecoveryHttpClientTimeoutMs) {
      sendResponse(408, "Request Timeout", "text/plain; charset=utf-8",
                   kBadRequestBody);
      return;
    }
    while (client_.available()) {
      if (requestLength_ == sizeof(request_)) {
        sendResponse(413, "Payload Too Large", "text/plain; charset=utf-8",
                     kPayloadBody);
        return;
      }
      const int byte = client_.read();
      if (byte < 0) break;
      request_[requestLength_++] = static_cast<char>(byte);
      size_t expectedLength = 0;
      const RecoveryHttpFraming framing = inspectRecoveryHttpFraming(
          request_, requestLength_, expectedLength);
      if (framing == RecoveryHttpFraming::Incomplete) continue;
      if (framing == RecoveryHttpFraming::TooLarge) {
        sendResponse(413, "Payload Too Large", "text/plain; charset=utf-8",
                     kPayloadBody);
        return;
      }
      if (framing == RecoveryHttpFraming::Invalid) {
        sendResponse(400, "Bad Request", "text/plain; charset=utf-8",
                     kBadRequestBody);
        return;
      }
      const bool allowed = requestCount_ < kRecoveryHttpMaxRequests;
      if (requestCount_ < UINT16_MAX) ++requestCount_;
      RecoveryHttpRequest parsed{};
      (void)evaluateRecoveryHttpRequest(
          request_, requestLength_, expectedHost_, pinSession_.sessionToken(),
          allowed, parsed);
      respond(parsed, nowMs);
      return;
    }
  }

  WiFiServer server_{80, 1};
  WiFiClient client_;
  RuntimeRandomSource randomSource_;
  RecoveryPinSession pinSession_;
  ConfirmationKeyDetector confirmation_;
  char ssid_[32]{};
  char expectedHost_[16]{};
  char request_[kRecoveryHttpMaxRequestBytes]{};
  size_t requestLength_ = 0;
  uint32_t clientStartedMs_ = 0;
  uint16_t requestCount_ = 0;
  bool clientActive_ = false;
  bool confirmationActive_ = false;
  bool confirmationHandled_ = false;
};

RuntimeSessionControl sessionControl;
RuntimeApPlatform platform;
RecoveryApController controller(sessionControl, platform);

}  // namespace

RecoveryApStartResult beginSecurityRecoveryAp(uint32_t nowMs) {
  return controller.start(nowMs);
}

bool serviceSecurityRecoveryAp(uint32_t nowMs) {
  return controller.service(nowMs);
}

bool securityRecoveryApOwnsRuntime() { return controller.ownsRuntime(); }

bool securityRecoveryApRunning() { return controller.running(); }

}  // namespace security_recovery
}  // namespace voxone
