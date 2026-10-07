#include "../src/core/config_format.h"

#include <cassert>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <sys/stat.h>

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
}  // namespace

int main() {
  using voxone::config_format::kConfigV7SerializedSize;
  static_assert(kConfigV7SerializedSize == 255,
                "Config v7 layout changed");
  static_assert(offsetof(voxone::config_format::config_v6_t, reservedInput0) == 27,
                "retired input byte 0 moved");
  static_assert(offsetof(voxone::config_format::config_v6_t, reservedInput1) == 28,
                "retired input byte 1 moved");

  assert(!std::ifstream("src/core/touchscreen.cpp").good());
  assert(!std::ifstream("src/core/touchscreen.h").good());
  struct stat info;
  assert(stat("src/GT911_Touchscreen", &info) != 0);

  const auto controls = readFile("src/core/controls.cpp");
  const auto controlsHeader = readFile("src/core/controls.h");
  const auto options = readFile("src/core/options.h");
  const auto mainSource = readFile("src/main.cpp");
  const auto config = readFile("src/core/config.cpp");
  const auto configHeader = readFile("src/core/config.h");
  const auto commands = readFile("src/core/commandhandler.cpp");
  const auto server = readFile("src/core/netserver.cpp");
  const auto unavailable = readFile("profiles/unavailable_hardware.h");
  const auto build = readFile("platformio.ini");

  for (const auto& source : {controls, controlsHeader, options, mainSource,
                             config, configHeader, commands, server,
                             unavailable, build}) {
    hasNone(source, {"touchscreen", "TouchScreen", "TS_MODEL_XPT2046",
                     "TS_MODEL_GT911", "XPT2046_Touchscreen", "TAMC_GT911",
                     "TS_MODEL", "TS_HSPI", "group_touch", "flipTS"});
  }
  hasNone(controls, {"touchscreen.loop", "touchEvent", "touchLoop",
                     "getPoint", ".touched()"});
  hasNone(build, {"XPT2046"});

  // Encoder 1, source cycling and Bluetooth transport remain wired as before.
  for (const char* token : {"yoEncoder encoder", "void encoder1Loop()",
                            "attachClick", "attachDoubleClick",
                            "attachLongPressStart", "attachLongPressStop"}) {
    assert(controls.find(token) != std::string::npos);
  }
  for (const char* token : {"cycleNextSource()", "sourceManagerTransport",
                            "BT_TRANSPORT", "btEncoderClickAction",
                            "btEncoderLongPressAction"}) {
    assert(controls.find(token) != std::string::npos);
  }

  const auto sourceManager = readFile("src/core/source_manager.cpp");
  assert(sourceManager.find("SourceManager") != std::string::npos);

  const auto selectors = readFile("src/displays/dspcore.h");
  for (const char* token : {"DSP_DUMMY", "DSP_ST7789_76", "DSP_ST7796",
                            "DSP_ST7789", "DSP_SSD1306", "DSP_SH1106",
                            "DSP_SSD1305", "DSP_SSD1305I2C",
                            "DSP_SSD1322", "DSP_GC9A01A", "DSP_ST7920",
                            "DSP_CUSTOM"}) {
    assert(options.find(token) != std::string::npos);
    assert(selectors.find(token) != std::string::npos);
  }
}
