#include "security_recovery_ap_runtime.h"

#include "factory_reset.h"
#include "network.h"
#include "security_recovery_http.h"
#include "security_recovery_session_runtime.h"
#include "security_settings.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <esp_system.h>
#include <cstdio>
#include <cstring>

namespace voxone { namespace security_recovery { namespace {

constexpr char kBadRequestBody[] = "Bad Request\n";
constexpr char kForbiddenBody[] = "Forbidden\n";
constexpr char kNotFoundBody[] = "Not Found\n";
constexpr char kMethodBody[] = "Method Not Allowed\n";
constexpr char kRateBody[] = "Too Many Requests\n";
constexpr char kPayloadBody[] = "Payload Too Large\n";
constexpr char kMediaTypeBody[] = "Unsupported Media Type\n";
constexpr char kInternalBody[] = "Recovery operation failed\n";
constexpr size_t kCsrfBytes = 24U;
static_assert(kRecoveryCsrfCharacters == kCsrfBytes * 2U,
              "Recovery CSRF size must match its hexadecimal encoding");

void secureZero(void* memory, size_t size) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(memory);
  while (size--) *bytes++ = 0;
}

void encodeHex(const uint8_t* input, size_t size, char* output) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i=0;i<size;++i) { output[i*2U]=digits[input[i]>>4U]; output[i*2U+1U]=digits[input[i]&0x0fU]; }
  output[size*2U]='\0';
}

class RuntimeSessionControl : public RecoveryApSessionControl {
 public:
  RecoveryState state() const override { return recoverySessionSnapshot().state; }
  bool live() const override { return recoverySessionSnapshot().live; }
  RecoveryTransition activate() override { return activateRecoverySession(); }
  RecoveryTransition fail() override { return failRecoverySession(); }
  RecoveryTransition complete(bool success) override { return completeRecoverySession(success); }
};

class RuntimeApPlatform : public RecoveryApPlatform {
 public:
  bool startWifi() override {
    const uint32_t suffix=static_cast<uint32_t>(ESP.getEfuseMac()&0x00ffffffULL);
    const int written=std::snprintf(ssid_,sizeof(ssid_),"VoxOne-Recovery-%06lX",static_cast<unsigned long>(suffix));
    if(written<=0||static_cast<size_t>(written)>=sizeof(ssid_)) return false;
    WiFi.persistent(false); WiFi.setAutoReconnect(false);
    if(!WiFi.mode(WIFI_AP)||!WiFi.softAP(ssid_)) { WiFi.mode(WIFI_OFF); return false; }
    return WiFi.getMode()==WIFI_AP;
  }

  bool startHttp() override {
    const IPAddress address=WiFi.softAPIP();
    if(std::snprintf(expectedHost_,sizeof(expectedHost_),"%u.%u.%u.%u",address[0],address[1],address[2],address[3])<=0) return false;
    if(std::snprintf(panelAddress_,sizeof(panelAddress_),"http://%s/",expectedHost_)<=0) return false;
    uint8_t random[kCsrfBytes]; esp_fill_random(random,sizeof(random)); encodeHex(random,sizeof(random),csrf_); secureZero(random,sizeof(random));
    server_.begin(); server_.setNoDelay(true); return static_cast<bool>(server_);
  }

  bool serviceHttp(uint32_t nowMs) override {
    if(!server_||WiFi.getMode()!=WIFI_AP||WiFi.softAPgetStationNum()>1U) return false;
    if(client_&&client_.connected()) {
      if(server_.hasClient()) { WiFiClient extra=server_.available(); extra.stop(); }
      readClient(nowMs); return true;
    }
    client_.stop(); clearRequest(); client_=server_.available();
    if(client_) { clientStartedMs_=nowMs; clientActive_=true; }
    return true;
  }

  void stopHttp() override { server_.stopAll(); server_.end(); }
  void disconnectClient() override { client_.stop(); client_=WiFiClient(); clientActive_=false; clearRequest(); }
  void stopWifi() override { WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); }
  void clearTemporary() override { secureZero(csrf_,sizeof(csrf_)); std::memset(ssid_,0,sizeof(ssid_)); std::memset(expectedHost_,0,sizeof(expectedHost_)); std::memset(panelAddress_,0,sizeof(panelAddress_)); clearRequest(); requestCount_=0; }
  void restart() override { ESP.restart(); }
  const char* ssid() const { return ssid_; }
  const char* panelAddress() const { return panelAddress_; }

 private:
  void clearRequest() { secureZero(request_,sizeof(request_)); requestLength_=0; }
  void closeClient() { client_.flush(); client_.stop(); clientActive_=false; clearRequest(); }
  void sendResponse(int status,const char* reason,const char* type,const char* body) {
    char header[768]; const size_t bodyLength=std::strlen(body);
    const int length=std::snprintf(header,sizeof(header),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store, max-age=0\r\nPragma: no-cache\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'\r\nReferrer-Policy: same-origin\r\nConnection: close\r\n\r\n",status,reason,type,static_cast<unsigned>(bodyLength));
    if(length>0&&static_cast<size_t>(length)<sizeof(header)) { client_.write(reinterpret_cast<const uint8_t*>(header),static_cast<size_t>(length)); client_.write(reinterpret_cast<const uint8_t*>(body),bodyLength); }
    closeClient();
  }
  void sendRoot() {
    char body[2300];
    const int length=renderRecoveryRootPage(csrf_,body,sizeof(body));
    if(length<=0||static_cast<size_t>(length)>=sizeof(body)) sendResponse(500,"Internal Server Error","text/plain; charset=utf-8",kInternalBody);
    else sendResponse(200,"OK","text/html; charset=utf-8",body);
  }
  void finishOperation(bool success,const char* successBody) {
    (void)completeRecoverySession(success);
    sendResponse(success?200:500,success?"OK":"Internal Server Error",success?"text/html; charset=utf-8":"text/plain; charset=utf-8",success?successBody:kInternalBody);
  }
  void handlePassword(const RecoveryHttpRequest& parsed) {
    char password[security::kMaxAdminPasswordBytes+1U]{};
    if(!parseRecoveryPasswordForm(request_+parsed.bodyOffset,parsed.bodyLength,csrf_,password,sizeof(password)) ||
       !security::securityValidateAdminPassword(password)) { secureZero(password,sizeof(password)); sendResponse(400,"Bad Request","text/plain; charset=utf-8",kBadRequestBody); return; }
    if(beginRecoveryCommit().to!=RecoveryState::Committing) { secureZero(password,sizeof(password)); sendResponse(500,"Internal Server Error","text/plain; charset=utf-8",kInternalBody); return; }
    const auto result=security::recoverDeviceSecurityPassword(password); secureZero(password,sizeof(password));
    finishOperation(result==security::SecurityOperationStatus::Applied,"<!doctype html><html lang=\"pl\"><meta charset=\"utf-8\"><title>VoxOne</title><p>Hasło zapisane. VoxOne uruchamia się ponownie.</p></html>");
  }
  void handleFactory(const RecoveryHttpRequest& parsed) {
    if(!parseRecoveryCsrfForm(request_+parsed.bodyOffset,parsed.bodyLength,csrf_)) { sendResponse(403,"Forbidden","text/plain; charset=utf-8",kForbiddenBody); return; }
    if(beginRecoveryCommit().to!=RecoveryState::Committing) { sendResponse(500,"Internal Server Error","text/plain; charset=utf-8",kInternalBody); return; }
    const auto result=factory_reset::performDeviceFactoryReset();
    finishOperation(result==factory_reset::Result::Applied,"<!doctype html><html lang=\"pl\"><meta charset=\"utf-8\"><title>VoxOne</title><p>Ustawienia fabryczne przywrócone. VoxOne uruchamia FIRST BOOT.</p></html>");
  }
  void respond(const RecoveryHttpRequest& parsed) {
    switch(parsed.decision) {
      case RecoveryHttpDecision::Root:sendRoot();return;
      case RecoveryHttpDecision::Password:handlePassword(parsed);return;
      case RecoveryHttpDecision::FactoryReset:handleFactory(parsed);return;
      case RecoveryHttpDecision::MethodNotAllowed:sendResponse(405,"Method Not Allowed","text/plain; charset=utf-8",kMethodBody);return;
      case RecoveryHttpDecision::NotFound:sendResponse(404,"Not Found","text/plain; charset=utf-8",kNotFoundBody);return;
      case RecoveryHttpDecision::RateLimited:sendResponse(429,"Too Many Requests","text/plain; charset=utf-8",kRateBody);return;
      case RecoveryHttpDecision::OriginRejected:case RecoveryHttpDecision::HostRejected:case RecoveryHttpDecision::BadRequest:sendResponse(400,"Bad Request","text/plain; charset=utf-8",kBadRequestBody);return;
      case RecoveryHttpDecision::UnsupportedMediaType:sendResponse(415,"Unsupported Media Type","text/plain; charset=utf-8",kMediaTypeBody);return;
      case RecoveryHttpDecision::PayloadTooLarge:sendResponse(413,"Payload Too Large","text/plain; charset=utf-8",kPayloadBody);return;
    }
  }
  void readClient(uint32_t nowMs) {
    if(clientActive_&&static_cast<uint32_t>(nowMs-clientStartedMs_)>=kRecoveryHttpClientTimeoutMs) { sendResponse(408,"Request Timeout","text/plain; charset=utf-8",kBadRequestBody); return; }
    while(client_.available()) {
      if(requestLength_==sizeof(request_)) { sendResponse(413,"Payload Too Large","text/plain; charset=utf-8",kPayloadBody); return; }
      const int byte=client_.read(); if(byte<0) break; request_[requestLength_++]=static_cast<char>(byte);
      size_t expected=0; const auto framing=inspectRecoveryHttpFraming(request_,requestLength_,expected);
      if(framing==RecoveryHttpFraming::Incomplete) continue;
      if(framing==RecoveryHttpFraming::TooLarge) { sendResponse(413,"Payload Too Large","text/plain; charset=utf-8",kPayloadBody); return; }
      if(framing==RecoveryHttpFraming::Invalid) { sendResponse(400,"Bad Request","text/plain; charset=utf-8",kBadRequestBody); return; }
      const bool allowed=requestCount_<kRecoveryHttpMaxRequests; if(requestCount_<UINT16_MAX) ++requestCount_;
      RecoveryHttpRequest parsed{}; (void)evaluateRecoveryHttpRequest(request_,requestLength_,expectedHost_,allowed,parsed); respond(parsed); return;
    }
  }
  WiFiServer server_{80,1}; WiFiClient client_; char ssid_[32]{}; char expectedHost_[16]{}; char panelAddress_[32]{};
  char csrf_[kRecoveryCsrfCharacters+1U]{}; char request_[kRecoveryHttpMaxRequestBytes]{}; size_t requestLength_=0; uint32_t clientStartedMs_=0; uint16_t requestCount_=0; bool clientActive_=false;
};

RuntimeSessionControl sessionControl; RuntimeApPlatform platform; RecoveryApController controller(sessionControl,platform);
}  // namespace

RecoveryApStartResult beginSecurityRecoveryAp(uint32_t nowMs) {
  const auto result = controller.start(nowMs);
  if (result == RecoveryApStartResult::Active)
    ::network.publishSecurityRecoveryAp(platform.ssid(),
                                        platform.panelAddress(), true);
  return result;
}
bool serviceSecurityRecoveryAp(uint32_t nowMs) { return controller.service(nowMs); }
bool securityRecoveryApOwnsRuntime() { return controller.ownsRuntime(); }
bool securityRecoveryApRunning() { return controller.running(); }

} }
