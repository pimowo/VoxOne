#include <cassert>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void hasNone(const std::string& source,
             std::initializer_list<const char*> tokens) {
  for (const char* token : tokens) {
    assert(source.find(token) == std::string::npos);
  }
}

void hasAll(const std::string& source,
            std::initializer_list<const char*> tokens) {
  for (const char* token : tokens) {
    assert(source.find(token) != std::string::npos);
  }
}
}  // namespace

int main() {
  const auto options = readFile("src/core/options.h");
  const auto config = readFile("src/core/config.cpp");
  const auto controls = readFile("src/core/controls.cpp");
  const auto unavailable = readFile("profiles/unavailable_hardware.h");
  const auto platformio = readFile("platformio.ini");

  const std::initializer_list<const char*> retired = {
      "SDC_CS", "SD_HSPI", "SD_SPIPINS", "USE_SD", "SD_AUTOPLAY",
      "SD_MAX_LEVELS", "SDSPISPEED", "SDSPI", "SD_SPI", "SD_CARD",
      "SDCARD", "SD_MMC", "SDMMC", "VS_HSPI"};
  for (const auto& source : {options, config, controls, unavailable, platformio}) {
    hasNone(source, retired);
  }

  // Shared display bus options remain active and are unrelated to SD.
  hasAll(options, {"DSP_HSPI", "TFT_CS", "TFT_DC", "TFT_RST"});
  hasAll(config, {"RTC_SDA", "RTC_SCL"});
}
