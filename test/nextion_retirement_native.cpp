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

void hasNoNextionRuntime(const char* path) {
  const auto source = readFile(path);
  for (const char* token : {"USE_NEXTION", "nextion.", "nextion.h",
                            "Nextion nextion", "saveWifiFromNextion",
                            "group_nextion", "const_DlgNextion"}) {
    assert(source.find(token) == std::string::npos);
  }
}
}  // namespace

int main() {
  static_assert(voxone::config_format::kConfigV7SerializedSize == 255,
                "Config v7 layout changed");

  assert(!std::ifstream("src/displays/nextion.cpp").good());
  assert(!std::ifstream("src/displays/nextion.h").good());

  for (const char* path : {
           "src/main.cpp", "src/core/audiohandlers.h", "src/core/config.cpp",
           "src/core/config.h", "src/core/controls.cpp", "src/core/display.cpp",
           "src/core/netserver.cpp", "src/core/options.h", "src/core/player.cpp",
           "src/core/timekeeper.cpp", "src/displays/widgets/widgets.cpp"}) {
    hasNoNextionRuntime(path);
  }
  for (const char* path : {"locale/displayL10n_en.h",
                           "locale/displayL10n_pl.h",
                           "locale/displayL10n_ru.h"}) {
    hasNoNextionRuntime(path);
  }

  const auto btLink = readFile("src/core/bt_link.cpp");
  assert(btLink.find("serial_(1)") != std::string::npos);
  assert(btLink.find("currentHardware().btUart") != std::string::npos);
  assert(btLink.find("static constexpr uint32_t BtLinkBaud = 921600") !=
         std::string::npos);
  assert(btLink.find("serial_.begin(BtLinkBaud, SERIAL_8N1, uart.rx, uart.tx)") !=
         std::string::npos);
  const auto a0 = readFile("profiles/a0.h");
  assert(a0.find("#define VOXONE_BT_UART_RX_PIN 15") != std::string::npos);
  assert(a0.find("#define VOXONE_BT_UART_TX_PIN 16") != std::string::npos);

  const auto options = readFile("src/core/options.h");
  const auto selectors = readFile("src/displays/dspcore.h");
  for (const char* token : {"DSP_DUMMY", "DSP_ST7789_76", "DSP_ST7796",
                            "DSP_ST7789", "DSP_SSD1306", "DSP_SH1106",
                            "DSP_SSD1305", "DSP_SSD1305I2C", "DSP_SSD1322",
                            "DSP_GC9A01A", "DSP_ST7920", "DSP_CUSTOM"}) {
    assert(options.find(token) != std::string::npos);
    assert(selectors.find(token) != std::string::npos);
  }
}
