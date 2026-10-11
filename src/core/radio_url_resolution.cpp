#include "radio_url_resolution.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace voxone {
namespace radio {
namespace {

struct TextView {
  const char* begin;
  size_t length;
};

TextView trimView(const char* value) {
  if (!value) return {nullptr, 0};
  const char* begin = value;
  while (*begin && isspace(static_cast<unsigned char>(*begin))) ++begin;
  const char* end = begin + strlen(begin);
  while (end > begin && isspace(static_cast<unsigned char>(end[-1]))) --end;
  return {begin, static_cast<size_t>(end - begin)};
}

bool startsWithIgnoreCase(const TextView& value, const char* prefix) {
  const size_t length = strlen(prefix);
  if (value.length < length) return false;
  for (size_t i = 0; i < length; ++i) {
    if (tolower(static_cast<unsigned char>(value.begin[i])) != prefix[i])
      return false;
  }
  return true;
}

bool hasInvalidUrlCharacter(const TextView& value) {
  for (size_t i = 0; i < value.length; ++i) {
    const unsigned char c = static_cast<unsigned char>(value.begin[i]);
    if (c <= 0x20 || c == 0x7f) return true;
  }
  return false;
}

UrlResolutionResult copyUrl(const TextView& value, char* output,
                            size_t outputSize) {
  if (!output || outputSize == 0) return UrlResolutionResult::TooLong;
  output[0] = '\0';
  if (!value.begin || value.length == 0) return UrlResolutionResult::Empty;
  if (value.length >= outputSize) return UrlResolutionResult::TooLong;
  memcpy(output, value.begin, value.length);
  output[value.length] = '\0';
  char* fragment = strchr(output, '#');
  if (fragment) *fragment = '\0';
  return output[0] ? UrlResolutionResult::Ok : UrlResolutionResult::Empty;
}

UrlResolutionResult validateAbsoluteUrl(const TextView& value) {
  if (!value.begin || value.length == 0) return UrlResolutionResult::Empty;
  if (hasInvalidUrlCharacter(value)) return UrlResolutionResult::Malformed;

  size_t schemeLength = 0;
  if (startsWithIgnoreCase(value, "http://")) schemeLength = 7;
  else if (startsWithIgnoreCase(value, "https://")) schemeLength = 8;
  else {
    for (size_t i = 0; i + 2 < value.length; ++i) {
      if (value.begin[i] == ':' && value.begin[i + 1] == '/' &&
          value.begin[i + 2] == '/')
        return UrlResolutionResult::UnsupportedScheme;
    }
    return UrlResolutionResult::Malformed;
  }

  if (value.length == schemeLength) return UrlResolutionResult::Malformed;
  size_t authorityEnd = schemeLength;
  while (authorityEnd < value.length && value.begin[authorityEnd] != '/' &&
         value.begin[authorityEnd] != '?' && value.begin[authorityEnd] != '#')
    ++authorityEnd;
  if (authorityEnd == schemeLength) return UrlResolutionResult::Malformed;
  return UrlResolutionResult::Ok;
}

UrlResolutionResult normalizeAbsoluteUrl(const TextView& value, char* output,
                                         size_t outputSize) {
  const UrlResolutionResult validation = validateAbsoluteUrl(value);
  if (validation != UrlResolutionResult::Ok) return validation;
  const UrlResolutionResult copied = copyUrl(value, output, outputSize);
  if (copied == UrlResolutionResult::Ok) {
    for (char* c = output; *c && *c != ':'; ++c)
      *c = static_cast<char>(tolower(static_cast<unsigned char>(*c)));
  }
  return copied;
}

bool append(char* output, size_t outputSize, size_t* used, const char* value,
            size_t length) {
  if (*used + length >= outputSize) return false;
  memcpy(output + *used, value, length);
  *used += length;
  output[*used] = '\0';
  return true;
}

UrlResolutionResult normalizePathUrl(const char* raw, char* output,
                                     size_t outputSize) {
  const TextView value = trimView(raw);
  const UrlResolutionResult validation = validateAbsoluteUrl(value);
  if (validation != UrlResolutionResult::Ok) return validation;

  const char* scheme = strstr(value.begin, "://");
  const size_t authorityStart = static_cast<size_t>(scheme - value.begin) + 3;
  size_t pathStart = authorityStart;
  while (pathStart < value.length && value.begin[pathStart] != '/' &&
         value.begin[pathStart] != '?' && value.begin[pathStart] != '#')
    ++pathStart;

  size_t suffixStart = pathStart;
  while (suffixStart < value.length && value.begin[suffixStart] != '?' &&
         value.begin[suffixStart] != '#')
    ++suffixStart;

  size_t used = 0;
  if (!append(output, outputSize, &used, value.begin, pathStart))
    return UrlResolutionResult::TooLong;
  if (pathStart == suffixStart) {
    if (!append(output, outputSize, &used, "/", 1))
      return UrlResolutionResult::TooLong;
  } else {
    size_t segmentStart = pathStart + 1;
    if (!append(output, outputSize, &used, "/", 1))
      return UrlResolutionResult::TooLong;
    while (segmentStart <= suffixStart) {
      size_t segmentEnd = segmentStart;
      while (segmentEnd < suffixStart && value.begin[segmentEnd] != '/')
        ++segmentEnd;
      const size_t segmentLength = segmentEnd - segmentStart;
      if (segmentLength == 2 && value.begin[segmentStart] == '.' &&
          value.begin[segmentStart + 1] == '.') {
        if (used > pathStart + 1) {
          --used;
          while (used > pathStart && output[used - 1] != '/') --used;
          output[used] = '\0';
        }
      } else if (!(segmentLength == 0 ||
                   (segmentLength == 1 && value.begin[segmentStart] == '.'))) {
        if (used > 0 && output[used - 1] != '/' &&
            !append(output, outputSize, &used, "/", 1))
          return UrlResolutionResult::TooLong;
        if (!append(output, outputSize, &used, value.begin + segmentStart,
                    segmentLength))
          return UrlResolutionResult::TooLong;
      }
      if (segmentEnd >= suffixStart) break;
      segmentStart = segmentEnd + 1;
    }
  }
  if (suffixStart < value.length && value.begin[suffixStart] == '?' &&
      !append(output, outputSize, &used, value.begin + suffixStart,
              value.length - suffixStart))
    return UrlResolutionResult::TooLong;
  char* fragment = strchr(output, '#');
  if (fragment) *fragment = '\0';
  for (char* c = output; *c && *c != ':'; ++c)
    *c = static_cast<char>(tolower(static_cast<unsigned char>(*c)));
  return UrlResolutionResult::Ok;
}

}  // namespace

bool isRedirectStatus(int statusCode) {
  return statusCode == 301 || statusCode == 302 || statusCode == 303 ||
         statusCode == 307 || statusCode == 308;
}

HttpResponseAction httpResponseAction(int statusCode) {
  if (isRedirectStatus(statusCode)) return HttpResponseAction::Redirect;
  if (statusCode >= 300) return HttpResponseAction::Error;
  return HttpResponseAction::Continue;
}

UrlResolutionResult resolveRelativeUrl(const char* baseUrl,
                                       const char* reference, char* output,
                                       size_t outputSize) {
  if (!output || outputSize == 0) return UrlResolutionResult::TooLong;
  output[0] = '\0';
  const TextView ref = trimView(reference);
  if (!ref.begin || ref.length == 0) return UrlResolutionResult::Empty;
  if (hasInvalidUrlCharacter(ref)) return UrlResolutionResult::Malformed;

  if (startsWithIgnoreCase(ref, "http://") ||
      startsWithIgnoreCase(ref, "https://"))
    return normalizeAbsoluteUrl(ref, output, outputSize);
  for (size_t i = 0; i + 2 < ref.length; ++i) {
    if (ref.begin[i] == ':' && ref.begin[i + 1] == '/' &&
        ref.begin[i + 2] == '/')
      return UrlResolutionResult::UnsupportedScheme;
  }

  const TextView base = trimView(baseUrl);
  const UrlResolutionResult baseValidation = validateAbsoluteUrl(base);
  if (baseValidation != UrlResolutionResult::Ok) return baseValidation;
  const char* schemeEnd = strstr(base.begin, "://");
  const size_t schemeLength = static_cast<size_t>(schemeEnd - base.begin);

  char combined[kMaxResolvedUrlLength];
  size_t used = 0;
  combined[0] = '\0';
  if (ref.length >= 2 && ref.begin[0] == '/' && ref.begin[1] == '/') {
    if (!append(combined, sizeof(combined), &used, base.begin, schemeLength) ||
        !append(combined, sizeof(combined), &used, ":", 1) ||
        !append(combined, sizeof(combined), &used, ref.begin, ref.length))
      return UrlResolutionResult::TooLong;
    return normalizePathUrl(combined, output, outputSize);
  }

  size_t authorityEnd = schemeLength + 3;
  while (authorityEnd < base.length && base.begin[authorityEnd] != '/' &&
         base.begin[authorityEnd] != '?' && base.begin[authorityEnd] != '#')
    ++authorityEnd;
  if (!append(combined, sizeof(combined), &used, base.begin, authorityEnd))
    return UrlResolutionResult::TooLong;

  if (ref.begin[0] == '/') {
    if (!append(combined, sizeof(combined), &used, ref.begin, ref.length))
      return UrlResolutionResult::TooLong;
  } else {
    size_t basePathEnd = authorityEnd;
    while (basePathEnd < base.length && base.begin[basePathEnd] != '?' &&
           base.begin[basePathEnd] != '#')
      ++basePathEnd;
    if (ref.begin[0] == '?') {
      if (!append(combined, sizeof(combined), &used, base.begin + authorityEnd,
                  basePathEnd - authorityEnd) ||
          !append(combined, sizeof(combined), &used, ref.begin, ref.length))
        return UrlResolutionResult::TooLong;
    } else {
      size_t directoryEnd = basePathEnd;
      while (directoryEnd > authorityEnd && base.begin[directoryEnd - 1] != '/')
        --directoryEnd;
      if (directoryEnd == authorityEnd &&
          !append(combined, sizeof(combined), &used, "/", 1))
        return UrlResolutionResult::TooLong;
      if (!append(combined, sizeof(combined), &used, base.begin + authorityEnd,
                  directoryEnd - authorityEnd) ||
          !append(combined, sizeof(combined), &used, ref.begin, ref.length))
        return UrlResolutionResult::TooLong;
    }
  }
  return normalizePathUrl(combined, output, outputSize);
}

UrlResolutionResult parseM3uEntry(const char* line, const char* baseUrl,
                                  char* output, size_t outputSize) {
  const TextView value = trimView(line);
  if (!value.begin || value.length == 0 || value.begin[0] == '#')
    return UrlResolutionResult::Empty;
  return resolveRelativeUrl(baseUrl, value.begin, output, outputSize);
}

UrlResolutionResult parsePlsEntry(const char* line, const char* baseUrl,
                                  uint16_t* entryNumber, char* output,
                                  size_t outputSize) {
  const TextView value = trimView(line);
  if (!value.begin || value.length < 6 ||
      tolower(static_cast<unsigned char>(value.begin[0])) != 'f' ||
      tolower(static_cast<unsigned char>(value.begin[1])) != 'i' ||
      tolower(static_cast<unsigned char>(value.begin[2])) != 'l' ||
      tolower(static_cast<unsigned char>(value.begin[3])) != 'e')
    return UrlResolutionResult::Empty;

  size_t pos = 4;
  uint32_t number = 0;
  const size_t digitStart = pos;
  while (pos < value.length && isdigit(static_cast<unsigned char>(value.begin[pos]))) {
    number = number * 10 + static_cast<unsigned>(value.begin[pos] - '0');
    if (number > UINT16_MAX) return UrlResolutionResult::Malformed;
    ++pos;
  }
  if (pos == digitStart || number == 0) return UrlResolutionResult::Malformed;
  while (pos < value.length && isspace(static_cast<unsigned char>(value.begin[pos])))
    ++pos;
  if (pos >= value.length || value.begin[pos] != '=')
    return UrlResolutionResult::Malformed;
  ++pos;
  while (pos < value.length && isspace(static_cast<unsigned char>(value.begin[pos])))
    ++pos;
  if (pos >= value.length) return UrlResolutionResult::Empty;

  const UrlResolutionResult resolved =
      resolveRelativeUrl(baseUrl, value.begin + pos, output, outputSize);
  if (resolved == UrlResolutionResult::Ok && entryNumber)
    *entryNumber = static_cast<uint16_t>(number);
  return resolved;
}

uint64_t UrlHopBudget::hashUrl(const char* url) {
  uint64_t hash = UINT64_C(14695981039346656037);
  if (!url) return hash;
  while (*url) {
    hash ^= static_cast<uint8_t>(*url++);
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

void UrlHopBudget::reset(const char* initialUrl) {
  visitedCount_ = 0;
  hops_ = 0;
  if (initialUrl && *initialUrl)
    visited_[visitedCount_++] = hashUrl(initialUrl);
}

UrlHopResult UrlHopBudget::follow(const char* url) {
  const uint64_t hash = hashUrl(url);
  for (uint8_t i = 0; i < visitedCount_; ++i) {
    if (visited_[i] == hash) return UrlHopResult::Loop;
  }
  if (hops_ >= kMaxResolutionHops)
    return UrlHopResult::LimitExceeded;
  ++hops_;
  visited_[visitedCount_++] = hash;
  return UrlHopResult::Accepted;
}

}  // namespace radio
}  // namespace voxone
