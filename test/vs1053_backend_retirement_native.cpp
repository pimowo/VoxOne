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
  assert(build.find("-<audioVS1053/>") != std::string::npos);
}
