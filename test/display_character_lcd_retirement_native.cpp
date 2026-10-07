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
  const auto widgets = readFile("src/displays/widgets/widgets.cpp");
  const auto netserver = readFile("src/core/netserver.cpp");
  const auto build = readFile("platformio.ini");
  for (const auto& source : {options, selectors, display, widgets, netserver, build}) {
    for (const char* token : {"DSP_1602", "DSP_1602I2C", "DSP_2002",
                              "DSP_2002I2C", "DSP_2004", "DSP_2004I2C",
                              "displayLC1602", "LiquidCrystal.h",
                              "LiquidCrystalI2CEx.h", "DSP_LCD", "LCD_I2C"}) {
      assert(source.find(token) == std::string::npos);
    }
  }

  struct stat info;
  assert(stat("src/LiquidCrystalI2C", &info) != 0);
  for (const char* path : {"src/displays/displayLC1602.h",
                           "src/displays/displayLC1602.cpp",
                           "src/displays/conf/displayLCD1602conf.h",
                           "src/displays/conf/displayLCD2004conf.h"}) {
    assert(!std::ifstream(path).good());
  }

  for (const char* token : {"DSP_DUMMY", "DSP_ST7789_76", "DSP_ST7796",
                            "DSP_ST7789", "DSP_SSD1306", "DSP_SH1106",
                            "DSP_SSD1305", "DSP_SSD1305I2C", "DSP_SSD1322",
                            "DSP_GC9A01A", "DSP_ST7920"}) {
    assert(options.find(token) != std::string::npos);
    assert(selectors.find(token) != std::string::npos);
  }
  for (const char* path : {"src/displays/tools/commongfx.h",
                           "src/displays/widgets/widgets.h",
                           "src/displays/widgets/pages.cpp",
                           "src/displays/displayST7796.h",
                           "src/displays/fonts/dsfont70.h",
                           "src/displays/fonts/bootlogo99x64.h"}) {
    assert(std::ifstream(path).good());
  }
}
