#include "../src/core/audio_info_bitrate.h"

#include <cassert>
#include <cstdint>
#include <string>

static void expectBitrate(const char* info, uint32_t expected) {
  uint32_t bitrate = 0xA5A5A5A5U;
  assert(parseAudioInfoBitrate(info, bitrate));
  assert(bitrate == expected);
}

static void expectNoBitrate(const char* info) {
  const uint32_t unchanged = 0xA5A5A5A5U;
  uint32_t bitrate = unchanged;
  assert(!parseAudioInfoBitrate(info, bitrate));
  assert(bitrate == unchanged);
}

int main() {
  expectBitrate("BitRate: 128000", 128000U);
  expectBitrate("BitRate: 320000", 320000U);
  expectBitrate("BitRate: 44100", 44100U);
  expectBitrate("BitRate: 0", 0U);

  const std::string longPrefix(160, 'P');
  const std::string longSuffix(160, 'S');
  expectBitrate((longPrefix + " BitRate: 128000").c_str(), 128000U);
  expectBitrate((longPrefix + " BitRate: 320000 " + longSuffix).c_str(), 320000U);

  expectNoBitrate("metadata without a bitrate field");
  expectNoBitrate("BitRate:");
  expectNoBitrate("BitRate: ");
  expectNoBitrate("BitRate: N/A");
  expectNoBitrate("BitRate: 12oops");
  expectNoBitrate("BitRate: 4294967296");
  expectNoBitrate(nullptr);
}
