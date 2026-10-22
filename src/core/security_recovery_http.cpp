#include "security_recovery_http.h"
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
