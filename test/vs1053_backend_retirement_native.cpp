#include "../src/core/config_format.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void absent(const std::string& source, const char* text) {
  assert(source.find(text) == std::string::npos);
}
}  // namespace

int main() {
  static_assert(voxone::config_format::kConfigV7SerializedSize == 255,
                "Config v7 wire size changed");

  const auto playerHeader = readFile("src/core/player.h");
  const auto playerCode = readFile("src/core/player.cpp");
  const auto configCode = readFile("src/core/config.cpp");
  const auto audioCode = readFile("src/audioI2S/Audio.cpp");
  const auto optionsChecker = readFile("src/core/optionschecker.h");
  const auto build = readFile("platformio.ini");
  const auto options = readFile("src/core/options.h");
  const auto unavailable = readFile("profiles/unavailable_hardware.h");

  for (const auto& source : {playerHeader, playerCode, configCode,
                             audioCode, optionsChecker}) {
    absent(source, "VS1053");
    absent(source, "ResetChip");
  }
  assert(playerHeader.find("../audioI2S/AudioEx.h") != std::string::npos);
  assert(playerCode.find("Player::Player() {}") != std::string::npos);
  assert(playerCode.find("Audio(true, I2S_DAC_CHANNEL_BOTH_EN)") !=
         std::string::npos);
  assert(playerCode.find("setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT)") !=
         std::string::npos);
  assert(configCode.find("mdns, 7)") != std::string::npos);
  assert(build.find("audioVS1053") == std::string::npos);
  assert(build.find("build_src_filter") == std::string::npos);
  assert(!std::ifstream("src/audioVS1053/audioVS1053Ex.cpp").good());
  assert(!std::ifstream("src/audioVS1053/audioVS1053Ex.h").good());
  assert(!std::ifstream("src/audioVS1053/vs1053b-patches-flac.h").good());
  assert(readFile("src/audioI2S/AudioEx.h").size() > 0);
  for (const char* macro : {"VS1053_CS", "VS1053_DCS", "VS1053_DREQ",
                            "VS1053_RST", "VS_HSPI"}) {
    absent(options, macro);
    absent(unavailable, macro);
  }
  for (const char* shared : {"#include <SPI.h>", "DSP_HSPI", "#define VSPI FSPI",
                             "TFT_CS", "I2S_INTERNAL", "MUTE_LOCK"})
    assert(options.find(shared) != std::string::npos);
}
