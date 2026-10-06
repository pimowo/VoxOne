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
  const auto server = readFile("src/core/netserver.cpp");
  const auto graphics = readFile("src/displays/tools/commongfx.h");
  const auto title = readFile("src/displays/tools/tftinverttitle.h");
  const auto widgets = readFile("src/displays/widgets/widgets.cpp");
  const auto build = readFile("platformio.ini");

  for (const auto& source : {options, selectors, display, server, graphics,
                             title, widgets, build}) {
    absent(source, "DSP_NOKIA5110");
    absent(source, "displayN5110");
    absent(source, "Adafruit_PCD8544.h");
  }
  absent(server, "group_nokia");
  absent(widgets, "NOKIA");

  for (const char* path : {
           "src/displays/displayN5110.h",
           "src/displays/displayN5110.cpp",
           "src/displays/conf/displayN5110conf.h",
           "src/displays/fonts/TinyFont5.h",
           "src/displays/fonts/TinyFont6.h",
           "src/displays/fonts/bootlogo21x28.h",
           "src/displays/fonts/dsfont19.h",
           "src/displays/fonts/DS_DIGI15pt7b.h",
           "src/displays/fonts/DS_DIGI15pt7b_mono.h",
       }) {
    assert(!std::ifstream(path).good());
  }

  for (const char* token : {"DSP_DUMMY", "DSP_ST7789_76", "DSP_ST7796",
                            "DSP_ST7789", "DSP_SSD1306", "DSP_SH1106",
                            "DSP_SSD1305", "DSP_SSD1305I2C",
                            "DSP_SSD1322", "DSP_GC9A01A", "DSP_ST7920"}) {
    assert(options.find(token) != std::string::npos);
    assert(selectors.find(token) != std::string::npos);
  }
}
