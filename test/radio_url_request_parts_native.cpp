#include "../src/core/radio_url_request_parts.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace {

int allocationCall = 0;
int failAt = 0;
int releaseCount = 0;

void* injectedAllocate(size_t size) {
  ++allocationCall;
  if (allocationCall == failAt) return nullptr;
  return std::malloc(size);
}

void injectedRelease(void* memory) {
  ++releaseCount;
  std::free(memory);
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void resetInjection(int failure) {
  allocationCall = 0;
  releaseCount = 0;
  failAt = failure;
}

}  // namespace

int main() {
  assert(voxone::radio::spacesOnlyEncodedCapacity(nullptr) == 0);
  assert(voxone::radio::spacesOnlyEncodedCapacity("") == 1);
  assert(voxone::radio::spacesOnlyEncodedCapacity("/a?b&c") == 7);
  assert(voxone::radio::spacesOnlyEncodedCapacity("/a b") == 7);

  resetInjection(0);
  {
    voxone::radio::UrlRequestParts parts;
    const char input[] = "radio.example:8000/a b?x=1&y=2";
    assert(parts.assign(input, 18, injectedAllocate, injectedRelease));
    assert(std::strcmp(parts.authority(), "radio.example:8000") == 0);
    assert(std::strcmp(parts.path(), "/a b?x=1&y=2") == 0);
    assert(parts.pathCapacity() ==
           voxone::radio::spacesOnlyEncodedCapacity("/a b?x=1&y=2"));
  }
  assert(releaseCount == 2);

  resetInjection(0);
  {
    voxone::radio::UrlRequestParts parts;
    assert(parts.assign("radio.example", -1, injectedAllocate,
                        injectedRelease));
    assert(std::strcmp(parts.authority(), "radio.example") == 0);
    assert(std::strcmp(parts.path(), "/") == 0);
  }
  assert(releaseCount == 2);

  for (int failure = 1; failure <= 2; ++failure) {
    resetInjection(failure);
    voxone::radio::UrlRequestParts parts;
    assert(!parts.assign("radio.example/path", 13, injectedAllocate,
                         injectedRelease));
    assert(parts.authority() == nullptr && parts.path() == nullptr);
    assert(releaseCount == failure - 1);
  }

  resetInjection(1);
  {
    voxone::radio::OwnedCString string;
    assert(!string.assign("radio.example", injectedAllocate,
                          injectedRelease));
    assert(string.get() == nullptr);
  }
  assert(releaseCount == 0);

  resetInjection(0);
  {
    voxone::radio::OwnedCString string;
    assert(string.assign("radio.example", injectedAllocate,
                         injectedRelease));
    assert(std::strcmp(string.get(), "radio.example") == 0);
  }
  assert(releaseCount == 1);

  const std::string audio = readFile("src/audioI2S/Audio.cpp");
  assert(audio.find("memcpy(extension, h_host + pos_slash, extLen)") ==
         std::string::npos);
  assert(audio.find("UrlRequestParts requestParts") != std::string::npos);
  assert(audio.find("OwnedCString strippedHost") != std::string::npos);
  assert(audio.find("out of memory preparing URL parts") !=
         std::string::npos);
}
