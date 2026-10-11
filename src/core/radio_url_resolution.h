#ifndef VOXONE_RADIO_URL_RESOLUTION_H
#define VOXONE_RADIO_URL_RESOLUTION_H

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace radio {

constexpr uint8_t kMaxResolutionHops = 5;
constexpr size_t kMaxResolvedUrlLength = 512;

enum class UrlResolutionResult : uint8_t {
  Ok,
  Empty,
  Malformed,
  UnsupportedScheme,
  TooLong
};

enum class UrlHopResult : uint8_t {
  Accepted,
  Loop,
  LimitExceeded
};

enum class HttpResponseAction : uint8_t {
  Continue,
  Redirect,
  Error
};

bool isRedirectStatus(int statusCode);
HttpResponseAction httpResponseAction(int statusCode);

UrlResolutionResult resolveRelativeUrl(const char* baseUrl,
                                       const char* reference,
                                       char* output,
                                       size_t outputSize);

// Returns the first playable, non-comment M3U line resolved against baseUrl.
UrlResolutionResult parseM3uEntry(const char* line, const char* baseUrl,
                                  char* output, size_t outputSize);

// Parses FileN= and reports N. Non-FileN metadata returns Empty.
UrlResolutionResult parsePlsEntry(const char* line, const char* baseUrl,
                                  uint16_t* entryNumber, char* output,
                                  size_t outputSize);

class UrlHopBudget {
 public:
  void reset(const char* initialUrl);
  UrlHopResult follow(const char* url);
  uint8_t hops() const { return hops_; }

 private:
  static uint64_t hashUrl(const char* url);

  uint64_t visited_[kMaxResolutionHops + 1]{};
  uint8_t visitedCount_ = 0;
  uint8_t hops_ = 0;
};

}  // namespace radio
}  // namespace voxone

#endif
