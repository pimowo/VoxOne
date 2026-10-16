#include "security_recovery_http.h"

#include <cstring>

namespace voxone {
namespace security_recovery {
namespace {

bool equalIgnoreCase(const char* left, size_t leftLength,
                     const char* right) {
  const size_t rightLength = std::strlen(right);
  if (leftLength != rightLength) return false;
  for (size_t index = 0; index < leftLength; ++index) {
    char a = left[index];
    char b = right[index];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

const char* findCrlf(const char* begin, const char* end) {
  for (const char* cursor = begin; cursor + 1 < end; ++cursor)
    if (cursor[0] == '\r' && cursor[1] == '\n') return cursor;
  return nullptr;
}

const char* findHeaderEnd(const char* request, size_t length) {
  if (!request) return nullptr;
  for (size_t index = 0; index + 3U < length; ++index)
    if (std::memcmp(request + index, "\r\n\r\n", 4U) == 0)
      return request + index;
  return nullptr;
}

bool authorityMatches(const char* value, size_t length,
                      const char* expectedHost) {
  const size_t expectedLength = std::strlen(expectedHost);
  if (length == expectedLength &&
      std::memcmp(value, expectedHost, expectedLength) == 0)
    return true;
  return length == expectedLength + 3U &&
         std::memcmp(value, expectedHost, expectedLength) == 0 &&
         std::memcmp(value + expectedLength, ":80", 3U) == 0;
}

bool originMatches(const char* value, size_t length,
                   const char* expectedHost) {
  constexpr char prefix[] = "http://";
  return length > sizeof(prefix) - 1U &&
         std::memcmp(value, prefix, sizeof(prefix) - 1U) == 0 &&
         authorityMatches(value + sizeof(prefix) - 1U,
                          length - sizeof(prefix) + 1U, expectedHost);
}

bool parseDecimal(const char* value, size_t length, size_t& output) {
  if (!length) return false;
  size_t result = 0;
  for (size_t index = 0; index < length; ++index) {
    if (value[index] < '0' || value[index] > '9') return false;
    const size_t digit = static_cast<size_t>(value[index] - '0');
    if (result > (static_cast<size_t>(-1) - digit) / 10U) return false;
    result = result * 10U + digit;
  }
  output = result;
  return true;
}

bool constantTimeTokenEqual(const char* value, size_t length,
                            const char* expected) {
  if (!expected) return false;
  const size_t expectedLength = std::strlen(expected);
  uint8_t difference = static_cast<uint8_t>(length != expectedLength);
  const size_t compared = length > expectedLength ? length : expectedLength;
  for (size_t index = 0; index < compared; ++index) {
    const uint8_t a = index < length ? static_cast<uint8_t>(value[index]) : 0U;
    const uint8_t b = index < expectedLength
                          ? static_cast<uint8_t>(expected[index])
                          : 0U;
    difference |= static_cast<uint8_t>(a ^ b);
  }
  return difference == 0U;
}

bool cookieContainsSession(const char* value, size_t length,
                           const char* expectedToken) {
  constexpr char name[] = "voxone_recovery=";
  size_t cursor = 0;
  bool matched = false;
  bool seen = false;
  while (cursor < length) {
    while (cursor < length && (value[cursor] == ' ' || value[cursor] == '\t'))
      ++cursor;
    size_t end = cursor;
    while (end < length && value[end] != ';') ++end;
    size_t trimmed = end;
    while (trimmed > cursor &&
           (value[trimmed - 1U] == ' ' || value[trimmed - 1U] == '\t'))
      --trimmed;
    if (trimmed - cursor >= sizeof(name) - 1U &&
        std::memcmp(value + cursor, name, sizeof(name) - 1U) == 0) {
      if (seen) return false;
      seen = true;
      matched = constantTimeTokenEqual(
          value + cursor + sizeof(name) - 1U,
          trimmed - cursor - sizeof(name) + 1U, expectedToken);
    }
    cursor = end < length ? end + 1U : end;
  }
  return seen && matched;
}

struct HeaderState {
  bool hostFound = false;
  bool hostValid = false;
  bool originFound = false;
  bool originValid = false;
  bool cookieFound = false;
  bool cookieValid = false;
  bool contentTypeFound = false;
  bool contentTypeValid = false;
  bool contentLengthFound = false;
  size_t contentLength = 0;
};

bool inspectHeaders(const char* begin, const char* headerEnd,
                    const char* expectedHost, const char* expectedToken,
                    HeaderState& state) {
  const char* cursor = begin;
  while (cursor < headerEnd) {
    const char* lineEnd = findCrlf(cursor, headerEnd + 2U);
    if (!lineEnd || lineEnd == cursor || *cursor == ' ' || *cursor == '\t')
      return false;
    const char* colon = static_cast<const char*>(
        std::memchr(cursor, ':', static_cast<size_t>(lineEnd - cursor)));
    if (!colon || colon == cursor) return false;
    const char* value = colon + 1;
    while (value < lineEnd && (*value == ' ' || *value == '\t')) ++value;
    const char* valueEnd = lineEnd;
    while (valueEnd > value &&
           (valueEnd[-1] == ' ' || valueEnd[-1] == '\t'))
      --valueEnd;
    const size_t nameLength = static_cast<size_t>(colon - cursor);
    const size_t valueLength = static_cast<size_t>(valueEnd - value);
    if (equalIgnoreCase(cursor, nameLength, "Host")) {
      if (state.hostFound) return false;
      state.hostFound = true;
      state.hostValid = authorityMatches(value, valueLength, expectedHost);
    } else if (equalIgnoreCase(cursor, nameLength, "Origin")) {
      if (state.originFound) return false;
      state.originFound = true;
      state.originValid = originMatches(value, valueLength, expectedHost);
    } else if (equalIgnoreCase(cursor, nameLength, "Cookie")) {
      if (state.cookieFound) return false;
      state.cookieFound = true;
      state.cookieValid =
          cookieContainsSession(value, valueLength, expectedToken);
    } else if (equalIgnoreCase(cursor, nameLength, "Content-Type")) {
      if (state.contentTypeFound) return false;
      state.contentTypeFound = true;
      state.contentTypeValid = equalIgnoreCase(
          value, valueLength, "application/x-www-form-urlencoded");
    } else if (equalIgnoreCase(cursor, nameLength, "Content-Length")) {
      if (state.contentLengthFound ||
          !parseDecimal(value, valueLength, state.contentLength))
        return false;
      state.contentLengthFound = true;
    } else if (equalIgnoreCase(cursor, nameLength, "Transfer-Encoding") ||
               equalIgnoreCase(cursor, nameLength, "Upgrade")) {
      return false;
    }
    cursor = lineEnd + 2U;
  }
  return cursor == headerEnd + 2U;
}

}  // namespace

RecoveryHttpFraming inspectRecoveryHttpFraming(
    const char* request, size_t length, size_t& expectedLength) {
  expectedLength = 0;
  if (!request || !length || length > kRecoveryHttpMaxRequestBytes ||
      std::memchr(request, '\0', length))
    return length > kRecoveryHttpMaxRequestBytes
               ? RecoveryHttpFraming::TooLarge
               : RecoveryHttpFraming::Invalid;
  const char* headerEnd = findHeaderEnd(request, length);
  if (!headerEnd)
    return length >= kRecoveryHttpMaxHeaderBytes
               ? RecoveryHttpFraming::TooLarge
               : RecoveryHttpFraming::Incomplete;
  const size_t headerBytes = static_cast<size_t>(headerEnd - request) + 4U;
  if (headerBytes > kRecoveryHttpMaxHeaderBytes)
    return RecoveryHttpFraming::TooLarge;

  const char* requestLineEnd = findCrlf(request, headerEnd + 2U);
  if (!requestLineEnd) return RecoveryHttpFraming::Invalid;
  const bool post = requestLineEnd - request >= 5 &&
                    std::memcmp(request, "POST ", 5U) == 0;
  HeaderState state;
  if (!inspectHeaders(requestLineEnd + 2U, headerEnd, "0", "", state))
    return RecoveryHttpFraming::Invalid;
  if (post) {
    if (!state.contentLengthFound || !state.contentLength)
      return RecoveryHttpFraming::Invalid;
  } else if (state.contentLengthFound) {
    return RecoveryHttpFraming::Invalid;
  }
  if (state.contentLength > kRecoveryHttpMaxBodyBytes)
    return RecoveryHttpFraming::TooLarge;
  expectedLength = headerBytes + state.contentLength;
  if (expectedLength > kRecoveryHttpMaxRequestBytes)
    return RecoveryHttpFraming::TooLarge;
  if (length < expectedLength) return RecoveryHttpFraming::Incomplete;
  return length == expectedLength ? RecoveryHttpFraming::Complete
                                  : RecoveryHttpFraming::Invalid;
}

RecoveryHttpDecision evaluateRecoveryHttpRequest(
    const char* request, size_t length, const char* expectedHost,
    bool requestAllowed) {
  RecoveryHttpRequest parsed{};
  return evaluateRecoveryHttpRequest(request, length, expectedHost, "",
                                     requestAllowed, parsed);
}

RecoveryHttpDecision evaluateRecoveryHttpRequest(
    const char* request, size_t length, const char* expectedHost,
    const char* expectedSessionToken, bool requestAllowed,
    RecoveryHttpRequest& parsed) {
  parsed = {RecoveryHttpDecision::BadRequest, 0, 0};
  size_t expectedLength = 0;
  const RecoveryHttpFraming framing =
      inspectRecoveryHttpFraming(request, length, expectedLength);
  if (framing == RecoveryHttpFraming::TooLarge)
    return parsed.decision = RecoveryHttpDecision::PayloadTooLarge;
  if (framing != RecoveryHttpFraming::Complete || expectedLength != length ||
      !expectedHost || !expectedHost[0])
    return parsed.decision;

  const char* const headerEnd = findHeaderEnd(request, length);
  const char* lineEnd = findCrlf(request, headerEnd + 2U);
  if (!lineEnd) return parsed.decision;
  const char* firstSpace = static_cast<const char*>(
      std::memchr(request, ' ', static_cast<size_t>(lineEnd - request)));
  if (!firstSpace) return parsed.decision;
  const char* secondSpace = static_cast<const char*>(
      std::memchr(firstSpace + 1, ' ',
                  static_cast<size_t>(lineEnd - firstSpace - 1)));
  if (!secondSpace || secondSpace + 9 != lineEnd ||
      std::memcmp(secondSpace + 1, "HTTP/1.1", 8U) != 0)
    return parsed.decision;

  const char* const method = request;
  const size_t methodLength = static_cast<size_t>(firstSpace - method);
  const char* const path = firstSpace + 1;
  const size_t pathLength = static_cast<size_t>(secondSpace - path);
  const bool get = methodLength == 3U && std::memcmp(method, "GET", 3U) == 0;
  const bool post = methodLength == 4U && std::memcmp(method, "POST", 4U) == 0;
  if ((!get && !post) || !pathLength || path[0] != '/')
    return parsed.decision;

  HeaderState headers;
  if (!inspectHeaders(lineEnd + 2U, headerEnd, expectedHost,
                      expectedSessionToken, headers))
    return parsed.decision;
  if (!headers.hostFound || !headers.hostValid)
    return parsed.decision = RecoveryHttpDecision::HostRejected;
  if (headers.originFound && !headers.originValid)
    return parsed.decision = RecoveryHttpDecision::OriginRejected;
  if (!requestAllowed)
    return parsed.decision = RecoveryHttpDecision::RateLimited;

  const bool root = pathLength == 1U && path[0] == '/';
  const bool status = pathLength == 7U &&
                      std::memcmp(path, "/status", 7U) == 0;
  const bool prepare = pathLength == 8U &&
                       std::memcmp(path, "/prepare", 8U) == 0;
  if ((root || status) && !get)
    return parsed.decision = RecoveryHttpDecision::MethodNotAllowed;
  if (prepare && !post)
    return parsed.decision = RecoveryHttpDecision::MethodNotAllowed;
  if (root) return parsed.decision = RecoveryHttpDecision::Root;
  if (status) {
    if (!headers.cookieValid)
      return parsed.decision = RecoveryHttpDecision::Unauthorized;
    return parsed.decision = RecoveryHttpDecision::Status;
  }
  if (prepare) {
    if (!headers.originFound || !headers.originValid)
      return parsed.decision = RecoveryHttpDecision::OriginRejected;
    if (!headers.cookieValid)
      return parsed.decision = RecoveryHttpDecision::Unauthorized;
    if (!headers.contentTypeFound || !headers.contentTypeValid)
      return parsed.decision = RecoveryHttpDecision::UnsupportedMediaType;
    if (!headers.contentLengthFound || !headers.contentLength)
      return parsed.decision;
    parsed.bodyOffset = static_cast<size_t>(headerEnd - request) + 4U;
    parsed.bodyLength = headers.contentLength;
    return parsed.decision = RecoveryHttpDecision::Prepare;
  }
  return parsed.decision = RecoveryHttpDecision::NotFound;
}

const char* recoveryHttpDecisionName(RecoveryHttpDecision decision) {
  switch (decision) {
    case RecoveryHttpDecision::Root: return "root";
    case RecoveryHttpDecision::Status: return "status";
    case RecoveryHttpDecision::Prepare: return "prepare";
    case RecoveryHttpDecision::BadRequest: return "bad_request";
    case RecoveryHttpDecision::HostRejected: return "host_rejected";
    case RecoveryHttpDecision::OriginRejected: return "origin_rejected";
    case RecoveryHttpDecision::Unauthorized: return "unauthorized";
    case RecoveryHttpDecision::UnsupportedMediaType:
      return "unsupported_media_type";
    case RecoveryHttpDecision::PayloadTooLarge: return "payload_too_large";
    case RecoveryHttpDecision::MethodNotAllowed: return "method_not_allowed";
    case RecoveryHttpDecision::NotFound: return "not_found";
    case RecoveryHttpDecision::RateLimited: return "rate_limited";
  }
  return "unknown";
}

}  // namespace security_recovery
}  // namespace voxone
