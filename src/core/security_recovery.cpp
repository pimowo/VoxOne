#include "security_recovery.h"

// Consolidated from security_recovery_key.cpp.
namespace voxone {
namespace security_recovery {

BootKeyDetector::BootKeyDetector(KeyTimings timings)
    : timings_(timings), state_(KeyState::Idle), stateStartedMs_(0) {}

void BootKeyDetector::enter(KeyState state, uint32_t nowMs) {
  state_ = state;
  stateStartedMs_ = nowMs;
}

bool BootKeyDetector::elapsed(uint32_t nowMs, uint32_t intervalMs) const {
  return static_cast<uint32_t>(nowMs - stateStartedMs_) >= intervalMs;
}

KeyState BootKeyDetector::begin(bool keyLow, uint32_t nowMs) {
  if (state_ != KeyState::Idle) return state_;
  enter(keyLow ? KeyState::Debounce : KeyState::Cancelled, nowMs);
  return state_;
}

KeyState BootKeyDetector::update(bool keyLow, uint32_t nowMs) {
  switch (state_) {
    case KeyState::Idle:
      return state_;
    case KeyState::Debounce:
      if (!keyLow)
        enter(KeyState::Cancelled, nowMs);
      else if (elapsed(nowMs, timings_.debounceMs))
        enter(KeyState::Accepted, nowMs);
      break;
    case KeyState::Accepted:
    case KeyState::Cancelled:
      break;
  }
  return state_;
}

bool BootKeyDetector::finished() const {
  return state_ == KeyState::Accepted || state_ == KeyState::Cancelled;
}

}  // namespace security_recovery
}  // namespace voxone

// Consolidated from security_recovery_state.cpp.
namespace voxone {
namespace security_recovery {

RecoveryTransition RecoveryStateMachine::transition(RecoveryState state,
                                                     RecoveryDiagnostic diagnostic) {
  const RecoveryState from = state_;
  state_ = state;
  diagnostic_ = diagnostic;
  return {from, state_, diagnostic_, from != state_};
}

RecoveryTransition RecoveryStateMachine::report(RecoveryDiagnostic diagnostic) {
  diagnostic_ = diagnostic;
  return {state_, state_, diagnostic_, false};
}

RecoveryTransition RecoveryStateMachine::observeBootKey(KeyState keyState) {
  if (bootKeyObserved_) return report(RecoveryDiagnostic::DuplicateRequest);
  bootKeyObserved_ = true;
  if (keyState != KeyState::Accepted) return report(RecoveryDiagnostic::KeyNotAccepted);
  requestConsumed_ = true;
  return transition(RecoveryState::Requested, RecoveryDiagnostic::Requested);
}

RecoveryTransition RecoveryStateMachine::dispatch(RecoveryEvent event) {
  if (event == RecoveryEvent::InternalFailure && state_ != RecoveryState::Idle &&
      state_ != RecoveryState::Completed)
    return transition(RecoveryState::Failed, RecoveryDiagnostic::InternalFailure);
  switch (state_) {
    case RecoveryState::Requested:
      if (event == RecoveryEvent::Activate)
        return transition(RecoveryState::Active, RecoveryDiagnostic::Activated);
      break;
    case RecoveryState::Active:
      if (event == RecoveryEvent::BeginCommit)
        return transition(RecoveryState::Committing, RecoveryDiagnostic::Committing);
      break;
    case RecoveryState::Committing:
      if (event == RecoveryEvent::CommitSucceeded)
        return transition(RecoveryState::Completed, RecoveryDiagnostic::Completed);
      if (event == RecoveryEvent::CommitFailed)
        return transition(RecoveryState::Failed, RecoveryDiagnostic::CommitFailed);
      break;
    case RecoveryState::Idle:
    case RecoveryState::Completed:
    case RecoveryState::Failed:
      break;
  }
  return report(RecoveryDiagnostic::InvalidEvent);
}

const char* recoveryStateName(RecoveryState state) {
  switch (state) {
    case RecoveryState::Idle: return "idle";
    case RecoveryState::Requested: return "requested";
    case RecoveryState::Active: return "active";
    case RecoveryState::Committing: return "committing";
    case RecoveryState::Completed: return "completed";
    case RecoveryState::Failed: return "failed";
  }
  return "unknown";
}

const char* recoveryDiagnosticName(RecoveryDiagnostic diagnostic) {
  switch (diagnostic) {
    case RecoveryDiagnostic::None: return "none";
    case RecoveryDiagnostic::KeyNotAccepted: return "key_not_accepted";
    case RecoveryDiagnostic::DuplicateRequest: return "duplicate_request";
    case RecoveryDiagnostic::Requested: return "requested";
    case RecoveryDiagnostic::Activated: return "activated";
    case RecoveryDiagnostic::Committing: return "committing";
    case RecoveryDiagnostic::Completed: return "completed";
    case RecoveryDiagnostic::CommitFailed: return "commit_failed";
    case RecoveryDiagnostic::InternalFailure: return "internal_failure";
    case RecoveryDiagnostic::InvalidEvent: return "invalid_event";
  }
  return "unknown";
}

}  // namespace security_recovery
}  // namespace voxone

// Consolidated from security_recovery_session.cpp.
namespace voxone { namespace security_recovery {
RecoveryTransition RecoverySession::current() const { return {machine_.state(), machine_.state(), machine_.diagnostic(), false}; }
bool RecoverySession::live() const { return machine_.state() == RecoveryState::Requested || machine_.state() == RecoveryState::Active || machine_.state() == RecoveryState::Committing; }
bool RecoverySession::activated() const { return machine_.state() == RecoveryState::Active || machine_.state() == RecoveryState::Committing; }
RecoveryTransition RecoverySession::begin(KeyState keyState) { return machine_.observeBootKey(keyState); }
RecoveryTransition RecoverySession::activate() { return machine_.dispatch(RecoveryEvent::Activate); }
RecoveryTransition RecoverySession::beginCommit() { return machine_.dispatch(RecoveryEvent::BeginCommit); }
RecoveryTransition RecoverySession::complete(bool success) { return machine_.dispatch(success ? RecoveryEvent::CommitSucceeded : RecoveryEvent::CommitFailed); }
RecoveryTransition RecoverySession::fail() { return machine_.dispatch(RecoveryEvent::InternalFailure); }
} }

// Consolidated from security_recovery_ap.cpp.
namespace voxone {
namespace security_recovery {

RecoveryApStartResult RecoveryApController::start(uint32_t nowMs) {
  (void)nowMs;
  if (session_.state() != RecoveryState::Requested)
    return RecoveryApStartResult::NotRequested;

  const RecoveryTransition activated = session_.activate();
  if (activated.to != RecoveryState::Active)
    return RecoveryApStartResult::NotRequested;

  // Once ACTIVE is reached, normal services must never start during this boot,
  // even if AP or HTTP startup subsequently fails.
  ownsRuntime_ = true;
  cleaned_ = false;
  restartIssued_ = false;
  if (!platform_.startWifi()) {
    session_.fail();
    cleanup();
    return RecoveryApStartResult::WifiFailed;
  }
  if (!platform_.startHttp()) {
    session_.fail();
    cleanup();
    return RecoveryApStartResult::HttpFailed;
  }
  running_ = true;
  return RecoveryApStartResult::Active;
}

bool RecoveryApController::service(uint32_t nowMs) {
  if (!ownsRuntime_) return false;
  if (!session_.live()) {
    const bool completed = session_.state() == RecoveryState::Completed;
    cleanup();
    if (completed && !restartIssued_) {
      restartIssued_ = true;
      platform_.restart();
    }
    return true;
  }
  if (running_ && !platform_.serviceHttp(nowMs)) {
    session_.fail();
    cleanup();
  }
  return true;
}

void RecoveryApController::complete(bool success) {
  if (session_.live()) session_.complete(success);
  if (session_.live()) session_.fail();
  if (ownsRuntime_) {
    const bool completed = session_.state() == RecoveryState::Completed;
    cleanup();
    if (completed && !restartIssued_) {
      restartIssued_ = true;
      platform_.restart();
    }
  }
}

void RecoveryApController::cleanup() {
  if (cleaned_) return;
  platform_.stopHttp();
  platform_.disconnectClient();
  platform_.stopWifi();
  platform_.clearTemporary();
  running_ = false;
  cleaned_ = true;
}

const char* recoveryApStartResultName(RecoveryApStartResult result) {
  switch (result) {
    case RecoveryApStartResult::NotRequested: return "not_requested";
    case RecoveryApStartResult::WifiFailed: return "wifi_failed";
    case RecoveryApStartResult::HttpFailed: return "http_failed";
    case RecoveryApStartResult::Active: return "active";
  }
  return "unknown";
}

}  // namespace security_recovery
}  // namespace voxone

// Consolidated from security_recovery_http.cpp.
#include <cstdio>
#include <cstring>

namespace voxone { namespace security_recovery { namespace {
const char* findCrlf(const char* begin, const char* end) {
  for (const char* p = begin; p + 1 < end; ++p) if (p[0] == '\r' && p[1] == '\n') return p;
  return nullptr;
}
const char* findHeaderEnd(const char* request, size_t length) {
  for (size_t i = 0; i + 3 < length; ++i)
    if (request[i] == '\r' && request[i+1] == '\n' && request[i+2] == '\r' && request[i+3] == '\n') return request + i;
  return nullptr;
}
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }
bool equalIgnoreCase(const char* value, size_t length, const char* expected) {
  size_t i = 0;
  while (expected[i]) { if (i >= length || lower(value[i]) != lower(expected[i])) return false; ++i; }
  return i == length;
}
bool parseDecimal(const char* value, size_t length, size_t& output) {
  if (!length) return false;
  size_t result = 0;
  for (size_t i = 0; i < length; ++i) {
    if (value[i] < '0' || value[i] > '9') return false;
    const size_t digit = static_cast<size_t>(value[i] - '0');
    if (result > (SIZE_MAX - digit) / 10U) return false;
    result = result * 10U + digit;
  }
  output = result; return true;
}
bool authorityMatches(const char* value, size_t length, const char* host) {
  if (!host) return false;
  const size_t hostLength = std::strlen(host);
  return (length == hostLength && std::memcmp(value, host, length) == 0) ||
         (length == hostLength + 3U && std::memcmp(value, host, hostLength) == 0 &&
          std::memcmp(value + hostLength, ":80", 3U) == 0);
}
bool originMatches(const char* value, size_t length, const char* host) {
  static const char prefix[] = "http://";
  return length > sizeof(prefix) - 1U && std::memcmp(value, prefix, sizeof(prefix)-1U) == 0 &&
         authorityMatches(value + sizeof(prefix)-1U, length - sizeof(prefix)+1U, host);
}
void secureZero(void* memory, size_t size) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(memory);
  while (size--) *bytes++ = 0;
}
uint8_t hexValue(char value) {
  if(value>='0'&&value<='9') return static_cast<uint8_t>(value-'0');
  if(value>='a'&&value<='f') return static_cast<uint8_t>(value-'a'+10);
  if(value>='A'&&value<='F') return static_cast<uint8_t>(value-'A'+10);
  return 0xffU;
}
bool decodeComponent(const char* input,size_t length,char* output,size_t capacity) {
  size_t written=0;
  if(!input||!output||!capacity) return false;
  for(size_t i=0;i<length;++i) {
    uint8_t value=static_cast<uint8_t>(input[i]);
    if(value=='+') value=' ';
    else if(value=='%') { if(i+2U>=length) return false; const uint8_t hi=hexValue(input[i+1U]),lo=hexValue(input[i+2U]); if(hi==0xffU||lo==0xffU) return false; value=static_cast<uint8_t>((hi<<4U)|lo); i+=2U; }
    if(!value||written+1U>=capacity) return false;
    output[written++]=static_cast<char>(value);
  }
  output[written]='\0'; return true;
}
bool tokenMatches(const char* supplied,const char* expected) {
  if(!supplied||!expected) return false;
  uint8_t difference=0;
  for(size_t i=0;i<=kRecoveryCsrfCharacters;++i) difference|=static_cast<uint8_t>(supplied[i]^expected[i]);
  return difference==0;
}
bool parseForm(const char* body,size_t length,const char* expectedCsrf,
               char* password,size_t passwordCapacity,bool passwordRequired) {
  char csrf[kRecoveryCsrfCharacters+1U]{}; uint8_t fields=0; size_t cursor=0;
  if(password&&passwordCapacity) password[0]='\0';
  while(body&&cursor<length) {
    size_t end=cursor; while(end<length&&body[end]!='&') ++end;
    size_t equals=cursor; while(equals<end&&body[equals]!='=') ++equals;
    if(equals==cursor||equals==end) { secureZero(csrf,sizeof(csrf)); return false; }
    char name[24]{}; const bool named=decodeComponent(body+cursor,equals-cursor,name,sizeof(name));
    bool assigned=false;
    if(named&&std::strcmp(name,"csrf")==0&&!(fields&1U)) { assigned=decodeComponent(body+equals+1U,end-equals-1U,csrf,sizeof(csrf)); fields|=assigned?1U:0U; }
    else if(named&&std::strcmp(name,"new_password")==0&&password&&!(fields&2U)) { assigned=decodeComponent(body+equals+1U,end-equals-1U,password,passwordCapacity); fields|=assigned?2U:0U; }
    secureZero(name,sizeof(name));
    if(!assigned) { secureZero(csrf,sizeof(csrf)); if(password) secureZero(password,passwordCapacity); return false; }
    if(end==length) break;
    cursor=end+1U;
    if(cursor==length) return false;
  }
  const bool valid=fields==(passwordRequired?3U:1U)&&tokenMatches(csrf,expectedCsrf);
  secureZero(csrf,sizeof(csrf)); if(!valid&&password) secureZero(password,passwordCapacity); return valid;
}
struct Headers {
  bool hostFound=false, hostValid=false, originFound=false, originValid=false;
  bool typeFound=false, typeValid=false, lengthFound=false; size_t contentLength=0;
};
bool inspectHeaders(const char* begin, const char* end, const char* host, Headers& out) {
  const char* cursor = begin;
  while (cursor < end) {
    const char* lineEnd = findCrlf(cursor, end + 2U);
    if (!lineEnd || lineEnd == cursor || *cursor == ' ' || *cursor == '\t') return false;
    const char* colon = static_cast<const char*>(std::memchr(cursor, ':', static_cast<size_t>(lineEnd-cursor)));
    if (!colon || colon == cursor) return false;
    const char* value = colon + 1; while (value < lineEnd && (*value == ' ' || *value == '\t')) ++value;
    const char* valueEnd = lineEnd; while (valueEnd > value && (valueEnd[-1] == ' ' || valueEnd[-1] == '\t')) --valueEnd;
    const size_t nameLength = static_cast<size_t>(colon-cursor), valueLength = static_cast<size_t>(valueEnd-value);
    if (equalIgnoreCase(cursor,nameLength,"Host")) { if(out.hostFound) return false; out.hostFound=true; out.hostValid=authorityMatches(value,valueLength,host); }
    else if (equalIgnoreCase(cursor,nameLength,"Origin")) { if(out.originFound) return false; out.originFound=true; out.originValid=originMatches(value,valueLength,host); }
    else if (equalIgnoreCase(cursor,nameLength,"Content-Type")) { if(out.typeFound) return false; out.typeFound=true; out.typeValid=equalIgnoreCase(value,valueLength,"application/x-www-form-urlencoded"); }
    else if (equalIgnoreCase(cursor,nameLength,"Content-Length")) { if(out.lengthFound || !parseDecimal(value,valueLength,out.contentLength)) return false; out.lengthFound=true; }
    else if (equalIgnoreCase(cursor,nameLength,"Transfer-Encoding") || equalIgnoreCase(cursor,nameLength,"Upgrade")) return false;
    cursor = lineEnd + 2U;
  }
  return cursor == end + 2U;
}
}  // namespace

int renderRecoveryRootPage(const char* csrf, char* output, size_t capacity) {
  if (!csrf || !output || !capacity) return -1;
  return std::snprintf(output,capacity,"<!doctype html><html lang=\"pl\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>VoxOne Recovery</title><style>:root{color-scheme:dark}body{max-width:520px;margin:40px auto;padding:20px;background:#09111a;color:#edf5fa;font:16px system-ui}section{margin:20px 0;padding:20px;border:1px solid #314456;border-radius:12px;background:#151f2b}label,input,button{display:block;width:100%%;box-sizing:border-box}input,button{margin-top:10px;padding:12px}button{background:#173f4b;color:#edf5fa;border:1px solid #71d8e8;border-radius:8px}</style></head><body><main><h1>VoxOne</h1><p>SECURITY RECOVERY</p><section><h2>Zmiana hasła WWW</h2><form method=\"post\" action=\"/password\"><input type=\"hidden\" name=\"csrf\" value=\"%s\"><label>Nowe hasło<input name=\"new_password\" type=\"password\" autocomplete=\"new-password\" maxlength=\"64\" required></label><button type=\"submit\">Zapisz hasło</button></form></section><section><h2>Ustawienia fabryczne</h2><form method=\"post\" action=\"/factory-reset\"><input type=\"hidden\" name=\"csrf\" value=\"%s\"><button type=\"submit\">Przywróć ustawienia fabryczne</button></form></section></main></body></html>",csrf,csrf);
}

bool parseRecoveryPasswordForm(const char* body, size_t length,
                               const char* expectedCsrf, char* password,
                               size_t passwordCapacity) {
  return parseForm(body,length,expectedCsrf,password,passwordCapacity,true);
}

bool parseRecoveryCsrfForm(const char* body, size_t length,
                           const char* expectedCsrf) {
  return parseForm(body,length,expectedCsrf,nullptr,0,false);
}

RecoveryHttpFraming inspectRecoveryHttpFraming(const char* request, size_t length, size_t& expectedLength) {
  expectedLength=0;
  if (!request || !length || length > kRecoveryHttpMaxRequestBytes || std::memchr(request,'\0',length))
    return length > kRecoveryHttpMaxRequestBytes ? RecoveryHttpFraming::TooLarge : RecoveryHttpFraming::Invalid;
  const char* headerEnd=findHeaderEnd(request,length);
  if(!headerEnd) return length >= kRecoveryHttpMaxHeaderBytes ? RecoveryHttpFraming::TooLarge : RecoveryHttpFraming::Incomplete;
  const size_t headerBytes=static_cast<size_t>(headerEnd-request)+4U;
  if(headerBytes>kRecoveryHttpMaxHeaderBytes) return RecoveryHttpFraming::TooLarge;
  const char* lineEnd=findCrlf(request,headerEnd+2U); if(!lineEnd) return RecoveryHttpFraming::Invalid;
  const bool post=lineEnd-request>=5 && std::memcmp(request,"POST ",5U)==0;
  Headers headers; if(!inspectHeaders(lineEnd+2U,headerEnd,"0",headers)) return RecoveryHttpFraming::Invalid;
  if(post && (!headers.lengthFound || !headers.contentLength)) return RecoveryHttpFraming::Invalid;
  if(!post && headers.lengthFound) return RecoveryHttpFraming::Invalid;
  if(headers.contentLength>kRecoveryHttpMaxBodyBytes) return RecoveryHttpFraming::TooLarge;
  expectedLength=headerBytes+headers.contentLength;
  if(expectedLength>kRecoveryHttpMaxRequestBytes) return RecoveryHttpFraming::TooLarge;
  if(length<expectedLength) return RecoveryHttpFraming::Incomplete;
  return length==expectedLength ? RecoveryHttpFraming::Complete : RecoveryHttpFraming::Invalid;
}

RecoveryHttpDecision evaluateRecoveryHttpRequest(const char* request,size_t length,const char* expectedHost,bool allowed,RecoveryHttpRequest& parsed) {
  parsed={RecoveryHttpDecision::BadRequest,0,0}; size_t expected=0;
  const auto framing=inspectRecoveryHttpFraming(request,length,expected);
  if(framing==RecoveryHttpFraming::TooLarge) return parsed.decision=RecoveryHttpDecision::PayloadTooLarge;
  if(framing!=RecoveryHttpFraming::Complete || expected!=length || !expectedHost || !*expectedHost) return parsed.decision;
  const char* headerEnd=findHeaderEnd(request,length); const char* lineEnd=findCrlf(request,headerEnd+2U);
  const char* first=static_cast<const char*>(std::memchr(request,' ',static_cast<size_t>(lineEnd-request)));
  const char* second=first?static_cast<const char*>(std::memchr(first+1,' ',static_cast<size_t>(lineEnd-first-1))):nullptr;
  if(!first||!second||second+9!=lineEnd||std::memcmp(second+1,"HTTP/1.1",8U)!=0) return parsed.decision;
  const bool get=first-request==3 && std::memcmp(request,"GET",3U)==0;
  const bool post=first-request==4 && std::memcmp(request,"POST",4U)==0;
  if(!get&&!post) return parsed.decision;
  const char* path=first+1; const size_t pathLength=static_cast<size_t>(second-path);
  Headers headers; if(!inspectHeaders(lineEnd+2U,headerEnd,expectedHost,headers)) return parsed.decision;
  if(!headers.hostFound||!headers.hostValid) return parsed.decision=RecoveryHttpDecision::HostRejected;
  if(headers.originFound&&!headers.originValid) return parsed.decision=RecoveryHttpDecision::OriginRejected;
  if(!allowed) return parsed.decision=RecoveryHttpDecision::RateLimited;
  const bool root=pathLength==1U&&path[0]=='/';
  const bool password=pathLength==9U&&std::memcmp(path,"/password",9U)==0;
  const bool factory=pathLength==14U&&std::memcmp(path,"/factory-reset",14U)==0;
  if(root) return parsed.decision=get?RecoveryHttpDecision::Root:RecoveryHttpDecision::MethodNotAllowed;
  if(password||factory) {
    if(!post) return parsed.decision=RecoveryHttpDecision::MethodNotAllowed;
    if(!headers.originFound||!headers.originValid) return parsed.decision=RecoveryHttpDecision::OriginRejected;
    if(!headers.typeFound||!headers.typeValid) return parsed.decision=RecoveryHttpDecision::UnsupportedMediaType;
    parsed.bodyOffset=static_cast<size_t>(headerEnd-request)+4U; parsed.bodyLength=headers.contentLength;
    return parsed.decision=password?RecoveryHttpDecision::Password:RecoveryHttpDecision::FactoryReset;
  }
  return parsed.decision=RecoveryHttpDecision::NotFound;
}

const char* recoveryHttpDecisionName(RecoveryHttpDecision decision) {
  switch(decision) {
    case RecoveryHttpDecision::Root:return "root"; case RecoveryHttpDecision::Password:return "password";
    case RecoveryHttpDecision::FactoryReset:return "factory_reset"; case RecoveryHttpDecision::BadRequest:return "bad_request";
    case RecoveryHttpDecision::HostRejected:return "host_rejected"; case RecoveryHttpDecision::OriginRejected:return "origin_rejected";
    case RecoveryHttpDecision::UnsupportedMediaType:return "unsupported_media_type"; case RecoveryHttpDecision::PayloadTooLarge:return "payload_too_large";
    case RecoveryHttpDecision::MethodNotAllowed:return "method_not_allowed"; case RecoveryHttpDecision::NotFound:return "not_found";
    case RecoveryHttpDecision::RateLimited:return "rate_limited";
  } return "unknown";
}
} }

// Consolidated from security_recovery_key_runtime.cpp.
#if defined(ARDUINO)
#include "Arduino.h"
#include "../hardware/hardware_descriptor.h"

namespace voxone {
namespace security_recovery {
namespace {
bool runtimeKeySuppressedUntilRelease = false;
}

KeyState detectBootKeyAtStartup() {
  const auto keyPin = hardware::currentHardware().localInput.key;
  pinMode(keyPin, INPUT);

  BootKeyDetector detector;
  const uint32_t startedMs = millis();
  detector.begin(digitalRead(keyPin) == LOW, startedMs);
  while (!detector.finished()) {
    delay(1);  // Yield during the short contact-debounce window.
    detector.update(digitalRead(keyPin) == LOW, millis());
  }

  return detector.state();
}

void suppressRuntimeKeyUntilRelease() {
  runtimeKeySuppressedUntilRelease = true;
}

bool runtimeKeyInputReady() {
  if (!runtimeKeySuppressedUntilRelease) return true;
  const auto keyPin = hardware::currentHardware().localInput.key;
  if (digitalRead(keyPin) == LOW) return false;
  runtimeKeySuppressedUntilRelease = false;
  return true;
}

}  // namespace security_recovery
}  // namespace voxone
#endif  // ARDUINO
// Consolidated from security_recovery_session_runtime.cpp.
namespace voxone { namespace security_recovery { namespace {
RecoverySession session;
RecoverySessionSnapshot snapshot() { return {session.state(), session.diagnostic(), session.requestedThisBoot(), session.live(), session.activated()}; }
}
RecoverySessionSnapshot beginRecoverySession(KeyState keyState) { session.begin(keyState); return snapshot(); }
RecoverySessionSnapshot recoverySessionSnapshot() { return snapshot(); }
RecoveryTransition activateRecoverySession() { return session.activate(); }
RecoveryTransition beginRecoveryCommit() { return session.beginCommit(); }
RecoveryTransition failRecoverySession() { return session.fail(); }
RecoveryTransition completeRecoverySession(bool success) { return session.complete(success); }
} }

// Consolidated from security_recovery_ap_runtime.cpp.
#if defined(ARDUINO)
#include "factory_reset.h"
#include "network.h"
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

void secureRuntimeZero(void* memory, size_t size) {
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
    uint8_t random[kCsrfBytes]; esp_fill_random(random,sizeof(random)); encodeHex(random,sizeof(random),csrf_); secureRuntimeZero(random,sizeof(random));
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
  void clearTemporary() override { secureRuntimeZero(csrf_,sizeof(csrf_)); std::memset(ssid_,0,sizeof(ssid_)); std::memset(expectedHost_,0,sizeof(expectedHost_)); std::memset(panelAddress_,0,sizeof(panelAddress_)); clearRequest(); requestCount_=0; }
  void restart() override { ESP.restart(); }
  const char* ssid() const { return ssid_; }
  const char* panelAddress() const { return panelAddress_; }

 private:
  void clearRequest() { secureRuntimeZero(request_,sizeof(request_)); requestLength_=0; }
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
       !security::securityValidateAdminPassword(password)) { secureRuntimeZero(password,sizeof(password)); sendResponse(400,"Bad Request","text/plain; charset=utf-8",kBadRequestBody); return; }
    if(beginRecoveryCommit().to!=RecoveryState::Committing) { secureRuntimeZero(password,sizeof(password)); sendResponse(500,"Internal Server Error","text/plain; charset=utf-8",kInternalBody); return; }
    const auto result=security::recoverDeviceSecurityPassword(password); secureRuntimeZero(password,sizeof(password));
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
#endif  // ARDUINO
