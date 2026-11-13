#ifndef VOXONE_HTTP_HEADER_UTILS_H
#define VOXONE_HTTP_HEADER_UTILS_H

#include <stddef.h>
#include <string.h>

namespace voxone {
namespace http {

inline bool asciiEqualIgnoreCase(const char* left, size_t leftLength,
                                 const char* right) {
  if (!left || !right || strlen(right) != leftLength) return false;
  for (size_t index = 0; index < leftLength; ++index) {
    char lhs = left[index];
    char rhs = right[index];
    if (lhs >= 'A' && lhs <= 'Z') lhs = static_cast<char>(lhs + ('a' - 'A'));
    if (rhs >= 'A' && rhs <= 'Z') rhs = static_cast<char>(rhs + ('a' - 'A'));
    if (lhs != rhs) return false;
  }
  return true;
}

inline void trimOptionalWhitespace(const char*& begin, const char*& end) {
  while (begin < end && (*begin == ' ' || *begin == '\t')) ++begin;
  while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) --end;
}

inline int parseQValue(const char* begin, const char* end) {
  trimOptionalWhitespace(begin, end);
  if (begin == end || (*begin != '0' && *begin != '1')) return 0;
  const bool one = *begin++ == '1';
  int quality = one ? 1000 : 0;
  if (begin == end) return quality;
  if (*begin++ != '.') return 0;
  int place = 100;
  size_t digits = 0;
  while (begin < end) {
    if (*begin < '0' || *begin > '9' || digits == 3) return 0;
    const int digit = *begin++ - '0';
    if (one && digit != 0) return 0;
    if (!one) quality += digit * place;
    place /= 10;
    ++digits;
  }
  return quality;
}

inline int encodingTokenQuality(const char* parameters,
                                const char* tokenEnd,
                                const char* itemEnd) {
  int quality = 1000;
  const char* cursor = parameters;
  while (cursor < itemEnd) {
    if (*cursor == ';') ++cursor;
    const char* parameterEnd = cursor;
    while (parameterEnd < itemEnd && *parameterEnd != ';') ++parameterEnd;
    const char* equals = cursor;
    while (equals < parameterEnd && *equals != '=') ++equals;
    const char* nameBegin = cursor;
    const char* nameEnd = equals;
    trimOptionalWhitespace(nameBegin, nameEnd);
    if (equals < parameterEnd &&
        asciiEqualIgnoreCase(nameBegin,
                             static_cast<size_t>(nameEnd - nameBegin), "q")) {
      quality = parseQValue(equals + 1, parameterEnd);
    }
    cursor = parameterEnd;
  }
  (void)tokenEnd;
  return quality;
}

inline bool acceptsGzip(const char* value) {
  if (!value || !value[0]) return false;
  int gzipQuality = -1;
  int wildcardQuality = -1;
  const char* cursor = value;
  while (*cursor) {
    const char* itemEnd = cursor;
    while (*itemEnd && *itemEnd != ',') ++itemEnd;
    const char* tokenBegin = cursor;
    const char* tokenEnd = tokenBegin;
    while (tokenEnd < itemEnd && *tokenEnd != ';') ++tokenEnd;
    const char* trimmedTokenEnd = tokenEnd;
    trimOptionalWhitespace(tokenBegin, trimmedTokenEnd);
    const int quality = encodingTokenQuality(tokenEnd, tokenEnd, itemEnd);
    if (asciiEqualIgnoreCase(
            tokenBegin, static_cast<size_t>(trimmedTokenEnd - tokenBegin),
            "gzip")) {
      if (quality > gzipQuality) gzipQuality = quality;
    } else if (trimmedTokenEnd - tokenBegin == 1 && *tokenBegin == '*') {
      if (quality > wildcardQuality) wildcardQuality = quality;
    }
    cursor = *itemEnd ? itemEnd + 1 : itemEnd;
  }
  return gzipQuality >= 0 ? gzipQuality > 0 : wildcardQuality > 0;
}

inline void stripWeakPrefix(const char*& begin, const char* end) {
  if (end - begin >= 2 && (begin[0] == 'W' || begin[0] == 'w') &&
      begin[1] == '/')
    begin += 2;
}

inline bool validEntityTag(const char* begin, const char* end) {
  return end - begin >= 2 && begin[0] == '"' && end[-1] == '"';
}

inline bool ifNoneMatchMatches(const char* value, const char* currentEtag) {
  if (!value || !currentEtag) return false;
  const char* currentBegin = currentEtag;
  const char* currentEnd = currentBegin + strlen(currentBegin);
  trimOptionalWhitespace(currentBegin, currentEnd);
  stripWeakPrefix(currentBegin, currentEnd);
  if (!validEntityTag(currentBegin, currentEnd)) return false;

  const char* cursor = value;
  while (*cursor) {
    const char* itemEnd = cursor;
    bool quoted = false;
    while (*itemEnd) {
      if (*itemEnd == '"') quoted = !quoted;
      if (*itemEnd == ',' && !quoted) break;
      ++itemEnd;
    }
    const char* itemBegin = cursor;
    const char* trimmedItemEnd = itemEnd;
    trimOptionalWhitespace(itemBegin, trimmedItemEnd);
    if (trimmedItemEnd - itemBegin == 1 && *itemBegin == '*') return true;
    stripWeakPrefix(itemBegin, trimmedItemEnd);
    if (validEntityTag(itemBegin, trimmedItemEnd) &&
        static_cast<size_t>(trimmedItemEnd - itemBegin) ==
            static_cast<size_t>(currentEnd - currentBegin) &&
        memcmp(itemBegin, currentBegin,
               static_cast<size_t>(currentEnd - currentBegin)) == 0)
      return true;
    cursor = *itemEnd ? itemEnd + 1 : itemEnd;
  }
  return false;
}

}  // namespace http
}  // namespace voxone

#endif
