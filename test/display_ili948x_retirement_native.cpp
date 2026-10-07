#include "../src/core/config_format.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>

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
  const auto display = readFile("src/core/display.cpp");
  const auto build = readFile("platformio.ini");
  for (const auto& source : {options, selectors, display, build}) {
    for (const char* token : {"DSP_ILI9488", "DSP_ILI9486",
                              "displayILI9488", "ILI9486_SPI.h"}) {
      assert(source.find(token) == std::string::npos);
    }
  }

  struct stat info;
  assert(stat("src/ILI9488", &info) != 0);
  for (const char* path : {"src/displays/displayILI9488.h",
                           "src/displays/displayILI9488.cpp",
                           "src/displays/conf/displayILI9488conf.h"}) {
    assert(!std::ifstream(path).good());
  }

  for (const char* token : {"DSP_DUMMY", "DSP_ST7789_76", "DSP_ST7796",
                            "DSP_ST7789", "DSP_SSD1306", "DSP_SH1106",
                            "DSP_SSD1305", "DSP_SSD1305I2C",
                            "DSP_SSD1322", "DSP_GC9A01A", "DSP_ST7920"}) {
    assert(options.find(token) != std::string::npos);
    assert(selectors.find(token) != std::string::npos);
  }
  for (const char* path : {"src/displays/fonts/dsfont70.h",
                           "src/displays/fonts/bootlogo99x64.h",
                           "src/displays/displayST7796.h"}) {
    assert(std::ifstream(path).good());
  }
  const auto salon = readFile("src/displays/displayST7796.h");
  assert(salon.find("fonts/dsfont70.h") != std::string::npos);
  assert(salon.find("fonts/bootlogo99x64.h") != std::string::npos);
}
