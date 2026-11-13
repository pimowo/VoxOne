#include "../src/core/radio_http_request.h"

#include <cassert>
#include <cstring>
#include <string>

using namespace voxone::radio;

namespace {
void* failAllocation(size_t) { return nullptr; }
}

int main() {
  HttpRequestHeader header;
  assert(header.build("/Music/A%20Long%20Song.mp3", "192.168.1.10:8200",
                      nullptr, "User-Agent: VoxOne/1.1.0\r\n", false) ==
         HttpRequestBuildResult::Ok);
  const std::string request(header.get());
  assert(request.find("GET /Music/A%20Long%20Song.mp3 HTTP/1.1\r\n") == 0);
  assert(request.find("Host: 192.168.1.10:8200\r\n") != std::string::npos);
  assert(request.find("Authorization:") == std::string::npos);
  assert(request.find("Icy-MetaData") == std::string::npos);
  assert(request.find("User-Agent: VoxOne/1.1.0\r\n") != std::string::npos);

  assert(header.build("/stream", "radio.example", "dXNlcjpwYXNz",
                      "User-Agent: VoxOne/test\r\n", true) ==
         HttpRequestBuildResult::Ok);
  assert(strstr(header.get(), "Icy-MetaData:1\r\n"));
  assert(strstr(header.get(), "Authorization: Basic dXNlcjpwYXNz\r\n"));

  std::string oversized(kMaxHttpRequestHeaderLength, 'a');
  assert(header.build(oversized.c_str(), "host", nullptr,
                      "User-Agent: test\r\n", false) ==
         HttpRequestBuildResult::TooLarge);
  assert(header.build("/", "host", nullptr, "User-Agent: test\r\n", false,
                      failAllocation) == HttpRequestBuildResult::NoMemory);
  return 0;
}
