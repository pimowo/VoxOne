#include "security_recovery_http.h"
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
