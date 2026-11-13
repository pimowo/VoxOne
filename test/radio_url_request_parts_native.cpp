#include "../src/core/radio_url_request_parts.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

  {
    const std::string windowsPath =
        "/WMPNSSv4/12345678-1234-1234-1234-123456789abc/"
        "0$1$8I1000 Music/Very long artist & album name/Track 01.mp3"
        "?format=audio/mpeg&token=0123456789abcdef0123456789abcdef";
    std::vector<char> encoded(
        voxone::radio::spacesOnlyEncodedCapacity(windowsPath.c_str()));
    std::memcpy(encoded.data(), windowsPath.c_str(), windowsPath.size() + 1);
    assert(voxone::radio::urlEncodeInPlace(encoded.data(), encoded.size(), true));
    const std::string result(encoded.data());
    assert(result.find(' ') == std::string::npos);
    assert(result.find("%20") != std::string::npos);
    assert(result.find("&token=") != std::string::npos);
  }

  {
    char encoded[16] = "a b&c";
    assert(voxone::radio::urlEncodeInPlace(encoded, sizeof(encoded)));
    assert(std::strcmp(encoded, "a%20b%26c") == 0);
    char tooSmall[4] = "a b";
    assert(!voxone::radio::urlEncodeInPlace(tooSmall, sizeof(tooSmall)));
    assert(std::strcmp(tooSmall, "a b") == 0);
  }

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
  const std::size_t urlencodeStart = audio.find("void Audio::urlencode");
  assert(urlencodeStart != std::string::npos);
  const std::size_t urlencodeEnd = audio.find("//---------------------------------------------------------------------------------------------------------------------",
                                              urlencodeStart + 1);
  assert(urlencodeEnd != std::string::npos);
  const std::string urlencode =
      audio.substr(urlencodeStart, urlencodeEnd - urlencodeStart);
  assert(urlencode.find("malloc") == std::string::npos);
  assert(urlencode.find("urlEncodeInPlace") != std::string::npos);
}
