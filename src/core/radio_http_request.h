#ifndef VOXONE_RADIO_HTTP_REQUEST_H
#define VOXONE_RADIO_HTTP_REQUEST_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

namespace voxone {
namespace radio {

constexpr size_t kMaxHttpRequestHeaderLength = 4096;

enum class HttpRequestBuildResult {
  Ok,
  Invalid,
  TooLarge,
  NoMemory,
  FormatError,
};

class HttpRequestHeader {
 public:
  using Allocate = void* (*)(size_t);
  using Release = void (*)(void*);

  HttpRequestHeader() = default;
  HttpRequestHeader(const HttpRequestHeader&) = delete;
  HttpRequestHeader& operator=(const HttpRequestHeader&) = delete;
  ~HttpRequestHeader() { reset(); }

  HttpRequestBuildResult build(const char* path, const char* authority,
                               const char* authorization,
                               const char* userAgentHeader, bool icyMetadata,
                               Allocate allocator = &malloc,
                               Release releaser = &free) {
    reset();
    if (!path || !authority || !userAgentHeader || !allocator || !releaser)
      return HttpRequestBuildResult::Invalid;

    const bool authenticated = authorization && authorization[0];
    const char* icyHeader = icyMetadata ? "Icy-MetaData:1\r\n" : "";
    const char* authorizationPrefix =
        authenticated ? "Authorization: Basic " : "";
    const char* authorizationValue = authenticated ? authorization : "";
    const char* authorizationSuffix = authenticated ? "\r\n" : "";
    constexpr const char* format =
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "%s%s%s%s"
        "Accept-Encoding: identity;q=1,*;q=0\r\n"
        "%s"
        "Connection: keep-alive\r\n\r\n";

    const int required = snprintf(
        nullptr, 0, format, path, authority, icyHeader, authorizationPrefix,
        authorizationValue, authorizationSuffix, userAgentHeader);
    if (required < 0) return HttpRequestBuildResult::FormatError;
    if (static_cast<size_t>(required) >= kMaxHttpRequestHeaderLength)
      return HttpRequestBuildResult::TooLarge;

    data_ = static_cast<char*>(allocator(static_cast<size_t>(required) + 1));
    if (!data_) return HttpRequestBuildResult::NoMemory;
    releaser_ = releaser;
    const int written = snprintf(
        data_, static_cast<size_t>(required) + 1, format, path, authority,
        icyHeader, authorizationPrefix, authorizationValue,
        authorizationSuffix, userAgentHeader);
    if (written != required) {
      reset();
      return HttpRequestBuildResult::FormatError;
    }
    return HttpRequestBuildResult::Ok;
  }

  void reset() {
    if (data_ && releaser_) releaser_(data_);
    data_ = nullptr;
    releaser_ = nullptr;
  }

  const char* get() const { return data_; }

 private:
  char* data_ = nullptr;
  Release releaser_ = nullptr;
};

}  // namespace radio
}  // namespace voxone

#endif
