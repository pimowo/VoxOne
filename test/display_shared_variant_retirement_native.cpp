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
}  // namespace

int main() {
  static_assert(voxone::config_format::kConfigV7SerializedSize == 255,
                "Config v7 layout changed");

  const auto options = readFile("src/core/options.h");
  const auto selectors = readFile("src/displays/dspcore.h");
  const auto coreDisplay = readFile("src/core/display.cpp");
  const auto ssdHeader = readFile("src/displays/displaySSD1306.h");
  const auto ssdSource = readFile("src/displays/displaySSD1306.cpp");
  const auto shHeader = readFile("src/displays/displaySH1106.h");
  const auto shSource = readFile("src/displays/displaySH1106.cpp");
  const auto stHeader = readFile("src/displays/displayST7789.h");
  const auto stSource = readFile("src/displays/displayST7789.cpp");
  const auto common = readFile("src/displays/tools/commongfx.h");
  for (const auto& source : {options, selectors, coreDisplay, ssdHeader,
                             ssdSource, shHeader, shSource, stHeader, stSource,
                             common}) {
    for (const char* token : {"DSP_SSD1306x32", "DSP_SH1107",
                              "DSP_ST7789_240", "DSP_ST7789_170",
                              "displaySSD1306x32conf", "displayST7789_240conf",
                              "displayST7789_170conf", "Adafruit_SH1107"}) {
      assert(source.find(token) == std::string::npos);
    }
  }
  for (const char* path : {"src/displays/conf/displaySSD1306x32conf.h",
                           "src/displays/conf/displayST7789_240conf.h",
                           "src/displays/conf/displayST7789_170conf.h"}) {
    assert(!std::ifstream(path).good());
  }

  for (const char* token : {"DSP_DUMMY", "DSP_ST7789_76", "DSP_ST7796",
                            "DSP_ST7789", "DSP_SSD1306", "DSP_SH1106",
                            "DSP_SSD1305", "DSP_SSD1305I2C", "DSP_SSD1322",
                            "DSP_GC9A01A", "DSP_ST7920", "DSP_CUSTOM"}) {
    assert(options.find(token) != std::string::npos);
    assert(selectors.find(token) != std::string::npos);
  }
  assert(ssdHeader.find("Adafruit_SSD1306.h") != std::string::npos);
  assert(shHeader.find("Adafruit_SH110X.h") != std::string::npos);
  assert(shHeader.find("Adafruit_SH1106G") != std::string::npos);
  assert(stHeader.find("Adafruit_ST7789.h") != std::string::npos);
  assert(stSource.find("init(76,284)") != std::string::npos);
  assert(stSource.find("init(240,320)") != std::string::npos);
  for (const char* path : {"src/displays/conf/displaySSD1306conf.h",
                           "src/displays/conf/displaySH1106conf.h",
                           "src/displays/conf/displayST7789_76conf.h",
                           "src/displays/conf/displayST7789conf.h",
                           "src/displays/displayST7796.h",
                           "src/displays/displaySSD1322.h",
                           "src/displays/displayGC9A01A.h",
                           "src/displays/displayST7920.h"}) {
    assert(std::ifstream(path).good());
  }
}
