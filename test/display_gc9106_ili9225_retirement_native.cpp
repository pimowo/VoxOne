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
  const auto widgets = readFile("src/displays/widgets/widgets.cpp");
  const auto build = readFile("platformio.ini");
  for (const auto& source : {options, selectors, display, widgets, build}) {
    for (const char* token : {"DSP_GC9106", "DSP_ILI9225",
                              "displayGC9106", "displayILI9225",
                              "Adafruit_GC9106Ex", "Adafruit_ILI9225"}) {
      absent(source, token);
    }
  }

  struct stat entry;
  assert(stat("src/Adafruit_GC9106Ex", &entry) != 0);
  assert(stat("src/Adafruit_ILI9225", &entry) != 0);
  for (const char* path : {"src/displays/displayGC9106.h",
                           "src/displays/displayGC9106.cpp",
                           "src/displays/conf/displayGC9106conf.h",
                           "src/displays/displayILI9225.h",
                           "src/displays/displayILI9225.cpp",
                           "src/displays/conf/displayILI9225conf.h"}) {
    assert(!std::ifstream(path).good());
  }

  for (const char* token : {"DSP_DUMMY", "DSP_ST7789_76", "DSP_ST7796",
                            "DSP_ST7789", "DSP_SSD1306", "DSP_SH1106",
                            "DSP_SSD1305", "DSP_SSD1305I2C",
                            "DSP_SSD1322", "DSP_GC9A01A", "DSP_ST7920"}) {
    assert(options.find(token) != std::string::npos);
    assert(selectors.find(token) != std::string::npos);
  }
  for (const char* path : {"src/displays/fonts/bootlogo99x64.h",
                           "src/displays/fonts/bootlogo21x32.h",
                           "src/displays/fonts/bootlogo62x40.h",
                           "src/displays/fonts/dsfont35.h",
                           "src/displays/fonts/dsfont70.h",
                           "src/displays/fonts/yofont5x7.c",
                           "src/displays/fonts/yofont10x14.c"}) {
    assert(std::ifstream(path).good());
  }
}
