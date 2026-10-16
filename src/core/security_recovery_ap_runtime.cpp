#include "security_recovery_ap_runtime.h"

#include "security_recovery_http.h"
#include "security_recovery_session_runtime.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiServer.h>
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
// Deliberately empty: there is no product credential until the user approves
// one. The full AP path still compiles, but the controller cannot activate it.
constexpr char kRecoveryApPassword[] = "";
constexpr bool kRecoveryApCredentialConfigured = false;
#endif

constexpr char kRecoveryPage[] =
    "<!doctype html><html lang=\"pl\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>VoxOne Recovery</title></head><body><main>"
    "<h1>VoxOne SECURITY RECOVERY</h1>"
    "<p>Tryb odzyskiwania jest aktywny. Zmiana hasla nie jest jeszcze "
    "dostepna.</p></main></body></html>";
constexpr char kStatusBody[] =
    "{\"mode\":\"security_recovery\",\"state\":\"active\","
    "\"timeoutMs\":600000}";
constexpr char kBadRequestBody[] = "Bad Request\n";
constexpr char kNotFoundBody[] = "Not Found\n";
constexpr char kMethodBody[] = "Method Not Allowed\n";
constexpr char kRateBody[] = "Too Many Requests\n";

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
  bool credentialConfigured() const override {
    return kRecoveryApCredentialConfigured;
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
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(expectedHost_))
      return false;
    server_.begin();
    server_.setNoDelay(true);
    return static_cast<bool>(server_);
  }

  bool serviceHttp(uint32_t nowMs) override {
    if (!server_ || WiFi.getMode() != WIFI_AP ||
        WiFi.softAPgetStationNum() > 1U)
      return false;

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
    std::memset(ssid_, 0, sizeof(ssid_));
    std::memset(expectedHost_, 0, sizeof(expectedHost_));
    std::memset(request_, 0, sizeof(request_));
    requestLength_ = 0;
    requestCount_ = 0;
    clientStartedMs_ = 0;
    clientActive_ = false;
  }

 private:
  bool requestComplete() const {
    return requestLength_ >= 4U &&
           std::memcmp(request_ + requestLength_ - 4U, "\r\n\r\n", 4U) == 0;
  }

  void clearRequest() {
    std::memset(request_, 0, sizeof(request_));
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
    char header[384];
    const size_t bodyLength = std::strlen(body);
    const int length = std::snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
        "Cache-Control: no-store, max-age=0\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; "
        "base-uri 'none'; frame-ancestors 'none'\r\n"
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

  void respond(RecoveryHttpDecision decision) {
    switch (decision) {
      case RecoveryHttpDecision::Root:
        sendResponse(200, "OK", "text/html; charset=utf-8", kRecoveryPage);
        return;
      case RecoveryHttpDecision::Status:
        sendResponse(200, "OK", "application/json; charset=utf-8",
                     kStatusBody);
        return;
      case RecoveryHttpDecision::MethodNotAllowed:
        sendResponse(405, "Method Not Allowed", "text/plain; charset=utf-8",
                     kMethodBody, "Allow: GET\r\n");
        return;
      case RecoveryHttpDecision::NotFound:
        sendResponse(404, "Not Found", "text/plain; charset=utf-8",
                     kNotFoundBody);
        return;
      case RecoveryHttpDecision::RateLimited:
        sendResponse(429, "Too Many Requests", "text/plain; charset=utf-8",
                     kRateBody);
        return;
      case RecoveryHttpDecision::BadRequest:
      case RecoveryHttpDecision::HostRejected:
        sendResponse(400, "Bad Request", "text/plain; charset=utf-8",
                     kBadRequestBody);
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
        sendResponse(431, "Request Header Fields Too Large",
                     "text/plain; charset=utf-8", kBadRequestBody);
        return;
      }
      const int byte = client_.read();
      if (byte < 0) break;
      request_[requestLength_++] = static_cast<char>(byte);
      if (requestComplete()) {
        const bool allowed = requestCount_ < kRecoveryHttpMaxRequests;
        if (requestCount_ < UINT16_MAX) ++requestCount_;
        respond(evaluateRecoveryHttpRequest(
            request_, requestLength_, expectedHost_, allowed));
        return;
      }
    }
  }

  WiFiServer server_{80, 1};
  WiFiClient client_;
  char ssid_[32]{};
  char expectedHost_[16]{};
  char request_[kRecoveryHttpMaxRequestBytes]{};
  size_t requestLength_ = 0;
  uint32_t clientStartedMs_ = 0;
  uint16_t requestCount_ = 0;
  bool clientActive_ = false;
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
