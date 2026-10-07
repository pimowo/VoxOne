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

void absent(const std::string& source, const char* token) {
  assert(source.find(token) == std::string::npos);
}
}  // namespace

int main() {
  static_assert(voxone::config_format::kConfigV7SerializedSize == 255,
                "Config v7 layout changed");

  const auto options = readFile("src/core/options.h");
  const auto selectors = readFile("src/displays/dspcore.h");
  const auto display = readFile("src/core/display.cpp");
  const auto stHeader = readFile("src/displays/displayST7789.h");
  const auto stSource = readFile("src/displays/displayST7789.cpp");
  const auto build = readFile("platformio.ini");

  for (const auto& source : {options, selectors, display, stHeader, stSource}) {
    for (const char* token : {"DSP_ST7735", "displayST7735", "Adafruit_ST7735",
                              "DTYPE", "INITR_BLACKTAB", "INITR_144GREENTAB",
                              "INITR_MINI160x80", "INITR_GREENTAB",
                              "INITR_REDTAB"}) {
      absent(source, token);
    }
  }

  for (const char* path : {"src/displays/displayST7735.cpp",
                           "src/displays/displayST7735.h",
                           "src/displays/conf/displayST7735_blackconf.h",
                           "src/displays/conf/displayST7735_144conf.h",
                           "src/displays/conf/displayST7735_miniconf.h"}) {
    assert(!std::ifstream(path).good());
  }

  for (const char* token : {"DSP_ST7789_76", "DSP_ST7789", "TFT_CS",
                            "TFT_DC", "TFT_RST", "DSP_HSPI"}) {
    assert(options.find(token) != std::string::npos);
  }
  assert(selectors.find("DSP_MODEL==DSP_ST7789") != std::string::npos);
  assert(selectors.find("DSP_MODEL==DSP_ST7789_76") != std::string::npos);
  assert(stHeader.find("Adafruit_ST7789.h") != std::string::npos);
  assert(stSource.find("init(76,284)") != std::string::npos);
  assert(stSource.find("init(240,320)") != std::string::npos);
  assert(build.find("Adafruit ST7735 and ST7789 Library@1.11.0") !=
         std::string::npos);

  for (const char* path : {"src/displays/displayST7789.cpp",
                           "src/displays/displayST7789.h",
                           "src/displays/conf/displayST7789_76conf.h",
                           "src/displays/conf/displayST7789conf.h",
                           "src/displays/fonts/bootlogo62x40.h",
                           "src/displays/fonts/dsfont35.h",
                           "src/displays/fonts/DS_DIGI28pt7b.h",
                           "src/displays/fonts/DS_DIGI28pt7b_mono.h"}) {
    assert(std::ifstream(path).good());
  }
}
