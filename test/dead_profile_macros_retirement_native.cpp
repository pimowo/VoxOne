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

  std::string activeSurface = readFile("src/core/options.h");
  for (const char* path : {"profiles/unavailable_hardware.h",
                           "profiles/x0.h", "profiles/b0.h",
                           "profiles/a0.h", "profiles/a0_dsp.h",
                           "profiles/profile.h", "profiles/profile_checks.h",
                           "src/hardware/hardware_descriptor.h",
                           "src/hardware/hardware_descriptor.cpp",
                           "src/core/optionschecker.h", "platformio.ini"}) {
    activeSurface += readFile(path);
  }

  for (const char* token : {
           "NEXTION_RX", "NEXTION_TX",
           "LCD_RS", "LCD_E", "LCD_D4", "LCD_D5", "LCD_D6", "LCD_D7",
           "ENC2_BTNL", "ENC2_BTNB", "ENC2_BTNR",
           "ENC2_INTERNALPULLUP", "ENC2_HALFQUARD",
           "VS1053_CS", "VS1053_DCS", "VS1053_DREQ", "VS1053_RST",
           "VS_HSPI", "IR_PIN", "IR_TIMEOUT", "IR_BUFSIZE", "ESP_S3C3"}) {
    assert(!contains(activeSurface, token));
  }

  for (const char* token : {"VOXONE_PROFILE_X0", "VOXONE_PROFILE_B0",
                            "VOXONE_PROFILE_A0", "BOARD_HAS_PSRAM",
                            "ARDUINO_ESP32_DEV", "ARDUINO_ESP32S3_DEV",
                            "ARDUINO_ESP32C3_DEV", "CONFIG_IDF_TARGET_ESP32",
                            "CONFIG_IDF_TARGET_ESP32S3", "DSP_HSPI",
                            "#include <SPI.h>", "#define VSPI FSPI"}) {
    assert(contains(activeSurface, token));
  }

  for (const char* field : {"reservedInput0", "reservedInput1",
                            "reservedSdStation", "reservedSdFlags",
                            "reservedPlayMode", "irtlp"}) {
    assert(contains(readFile("src/core/config_format.h"), field));
  }
}
