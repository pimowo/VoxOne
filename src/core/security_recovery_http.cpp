#include "security_recovery_http.h"

#include <cstring>

namespace voxone {
namespace security_recovery {
namespace {

bool equalIgnoreCase(const char* left, size_t leftLength,
                     const char* right) {
  const size_t rightLength = std::strlen(right);
  if (leftLength != rightLength) return false;
  for (size_t i = 0; i < leftLength; ++i) {
    char a = left[i];
    char b = right[i];
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

bool hostMatches(const char* value, size_t length, const char* expectedHost) {
  const size_t expectedLength = std::strlen(expectedHost);
  if (length == expectedLength &&
      std::memcmp(value, expectedHost, expectedLength) == 0)
    return true;
  return length == expectedLength + 3U &&
         std::memcmp(value, expectedHost, expectedLength) == 0 &&
         std::memcmp(value + expectedLength, ":80", 3) == 0;
}

}  // namespace

RecoveryHttpDecision evaluateRecoveryHttpRequest(
    const char* request, size_t length, const char* expectedHost,
    bool requestAllowed) {
  if (!request || !expectedHost || !expectedHost[0] || !length ||
      length > kRecoveryHttpMaxRequestBytes ||
      std::memchr(request, '\0', length))
    return RecoveryHttpDecision::BadRequest;
  if (length < 4 || std::memcmp(request + length - 4, "\r\n\r\n", 4) != 0)
    return RecoveryHttpDecision::BadRequest;

  const char* const end = request + length;
  const char* lineEnd = findCrlf(request, end);
  if (!lineEnd) return RecoveryHttpDecision::BadRequest;
  const char* firstSpace = static_cast<const char*>(
      std::memchr(request, ' ', static_cast<size_t>(lineEnd - request)));
  if (!firstSpace) return RecoveryHttpDecision::BadRequest;
  const char* secondSpace = static_cast<const char*>(
      std::memchr(firstSpace + 1, ' ',
                  static_cast<size_t>(lineEnd - firstSpace - 1)));
  if (!secondSpace || secondSpace + 9 != lineEnd ||
      std::memcmp(secondSpace + 1, "HTTP/1.1", 8) != 0)
    return RecoveryHttpDecision::BadRequest;

  const char* const method = request;
  const size_t methodLength = static_cast<size_t>(firstSpace - method);
  const char* const path = firstSpace + 1;
  const size_t pathLength = static_cast<size_t>(secondSpace - path);
  if (!methodLength || !pathLength || path[0] != '/')
    return RecoveryHttpDecision::BadRequest;

  bool hostFound = false;
  bool hostValid = false;
  const char* cursor = lineEnd + 2;
  while (cursor < end - 2) {
    lineEnd = findCrlf(cursor, end);
    if (!lineEnd) return RecoveryHttpDecision::BadRequest;
    if (lineEnd == cursor) break;
    if (*cursor == ' ' || *cursor == '\t')
      return RecoveryHttpDecision::BadRequest;
    const char* colon = static_cast<const char*>(
        std::memchr(cursor, ':', static_cast<size_t>(lineEnd - cursor)));
    if (!colon || colon == cursor) return RecoveryHttpDecision::BadRequest;
    const char* value = colon + 1;
    while (value < lineEnd && (*value == ' ' || *value == '\t')) ++value;
    const char* valueEnd = lineEnd;
    while (valueEnd > value &&
           (valueEnd[-1] == ' ' || valueEnd[-1] == '\t'))
      --valueEnd;
    const size_t nameLength = static_cast<size_t>(colon - cursor);
    const size_t valueLength = static_cast<size_t>(valueEnd - value);
    if (equalIgnoreCase(cursor, nameLength, "Host")) {
      if (hostFound) return RecoveryHttpDecision::BadRequest;
      hostFound = true;
      hostValid = hostMatches(value, valueLength, expectedHost);
    } else if (equalIgnoreCase(cursor, nameLength, "Content-Length") ||
               equalIgnoreCase(cursor, nameLength, "Transfer-Encoding") ||
               equalIgnoreCase(cursor, nameLength, "Upgrade")) {
      return RecoveryHttpDecision::BadRequest;
    }
    cursor = lineEnd + 2;
  }
  if (!hostFound || !hostValid) return RecoveryHttpDecision::HostRejected;
  if (!requestAllowed) return RecoveryHttpDecision::RateLimited;

  const bool root = pathLength == 1 && path[0] == '/';
  const bool status = pathLength == 7 && std::memcmp(path, "/status", 7) == 0;
  const bool get = methodLength == 3 && std::memcmp(method, "GET", 3) == 0;
  if ((root || status) && !get)
    return RecoveryHttpDecision::MethodNotAllowed;
  if (root) return RecoveryHttpDecision::Root;
  if (status) return RecoveryHttpDecision::Status;
  return RecoveryHttpDecision::NotFound;
}

const char* recoveryHttpDecisionName(RecoveryHttpDecision decision) {
  switch (decision) {
    case RecoveryHttpDecision::Root: return "root";
    case RecoveryHttpDecision::Status: return "status";
    case RecoveryHttpDecision::BadRequest: return "bad_request";
    case RecoveryHttpDecision::HostRejected: return "host_rejected";
    case RecoveryHttpDecision::MethodNotAllowed: return "method_not_allowed";
    case RecoveryHttpDecision::NotFound: return "not_found";
    case RecoveryHttpDecision::RateLimited: return "rate_limited";
  }
  return "unknown";
}

}  // namespace security_recovery
}  // namespace voxone
