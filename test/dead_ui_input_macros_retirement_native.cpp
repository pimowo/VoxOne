#include "../src/core/config_format.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

bool contains(const std::string& source, const char* token) {
  return source.find(token) != std::string::npos;
}
}  // namespace

int main() {
  static_assert(voxone::config_format::kConfigV7SerializedSize == 255,
                "Config v7 layout changed");

  std::string activeDefinitions = readFile("src/core/options.h");
  for (const char* path : {"profiles/unavailable_hardware.h",
                           "profiles/x0.h", "profiles/b0.h",
                           "profiles/a0.h", "profiles/a0_dsp.h"}) {
    activeDefinitions += readFile(path);
  }

  for (const char* token : {
           "NEXTION_RX", "NEXTION_TX",
           "LCD_RS", "LCD_E", "LCD_D4", "LCD_D5", "LCD_D6", "LCD_D7",
           "ENC2_BTNL", "ENC2_BTNB", "ENC2_BTNR",
           "ENC2_INTERNALPULLUP", "ENC2_HALFQUARD"}) {
    assert(!contains(activeDefinitions, token));
  }

  for (const char* token : {"ENC_BTNL", "ENC_BTNB", "ENC_BTNR",
                            "ENC_INTERNALPULLUP", "ENC_HALFQUARD",
                            "WAKE_PIN", "VOXONE_BT_UART_RX_PIN",
                            "VOXONE_BT_UART_TX_PIN", "DSP_ST7920"}) {
    assert(contains(activeDefinitions, token));
  }
}
