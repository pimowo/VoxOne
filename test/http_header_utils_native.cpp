#include "../src/AsyncWebServer/http_header_utils.h"

#include <cassert>

using voxone::http::acceptsGzip;
using voxone::http::ifNoneMatchMatches;

int main() {
  assert(acceptsGzip("gzip"));
  assert(acceptsGzip("br, GZip; q=0.5"));
  assert(acceptsGzip("br;q=1, *;q=0.2"));
  assert(!acceptsGzip("identity"));
  assert(!acceptsGzip("gzip;q=0, *;q=1"));
  assert(!acceptsGzip("br, *;q=0"));
  assert(!acceptsGzip(""));

  constexpr const char* etag =
      "\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"";
  assert(ifNoneMatchMatches(etag, etag));
  assert(ifNoneMatchMatches(
      "W/\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"",
      etag));
  assert(ifNoneMatchMatches(
      "\"other\", W/\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"",
      etag));
  assert(ifNoneMatchMatches(" * ", etag));
  assert(!ifNoneMatchMatches("\"other\", \"missing\"", etag));
  assert(!ifNoneMatchMatches("W/other", etag));
  assert(ifNoneMatchMatches("\"opaque,comma\", \"target\"", "\"target\""));
  return 0;
}
