#include "../src/core/config_format.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <type_traits>

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void absent(const std::string& source, const char* text) {
  assert(source.find(text) == std::string::npos);
}

void configIoUsesReservedBoundary(const std::string& source, const char* call) {
  std::size_t position = 0;
  unsigned count = 0;
  while ((position = source.find(call, position)) != std::string::npos) {
    const std::size_t argument = position + std::char_traits<char>::length(call);
    const std::size_t lineStart = source.rfind('\n', position) + 1;
    if (source.substr(lineStart, position - lineStart).find("//") !=
        std::string::npos) {
      position = argument;
      continue;
    }
    assert(source.compare(argument, 12, "EEPROM_START") == 0);
    position = argument;
    ++count;
  }
  assert(count != 0);
}
}  // namespace

int main() {
  using namespace voxone::config_format;
  static_assert(std::is_same<decltype(config_v5_t::irtlp), uint8_t>::value,
                "v5 IR compatibility byte removed");
  static_assert(std::is_same<decltype(config_v6_t::irtlp), uint8_t>::value,
                "v6/v7 IR compatibility byte removed");
  static_assert(kConfigV7SerializedSize == 255, "v7 wire size changed");

  const auto configHeader = readFile("src/core/config.h");
  const auto configCode = readFile("src/core/config.cpp");
  const auto commands = readFile("src/core/commandhandler.cpp");
  const auto serverHeader = readFile("src/core/netserver.h");
  const auto serverCode = readFile("src/core/netserver.cpp");
  const auto controls = readFile("src/core/controls.cpp");
  const auto platformio = readFile("platformio.ini");
  const auto verify = readFile("tools/verify.ps1");
  const auto assetBuilder = readFile("scripts/build_web_assets.py");

  assert(configHeader.find("#define EEPROM_START      500") != std::string::npos);
  assert(configHeader.find("#define EEPROM_SIZE       768") != std::string::npos);
  assert(configHeader.find("uint8_t   irtlp;") != std::string::npos);
  configIoUsesReservedBoundary(configCode, "EEPROM.read(");
  configIoUsesReservedBoundary(configCode, "EEPROM.write(");

  for (const char* token : {"ircodes_t", "ircodes", "irVals", "irindex",
                            "irchck", "setIrBtn", "saveIR", "eepromRead(",
                            "EEPROM_START_IR", "EEPROM_START_2", "IR_PIN"}) {
    absent(configHeader, token);
    absent(configCode, token);
  }
  for (const char* command : {"\"irbtn\"", "\"chkid\"", "\"irclr\"",
                              "\"irtlp\"", "\"getcontrols\""}) {
    absent(commands, command);
  }
  for (const char* token : {"irRecordEnable", "irToWs", "irValsToWs",
                            "GETCONTROLS", "group_ir", "\\\"ircode\\\"",
                            "\\\"irvals\\\"", "\\\"irtl\\\""}) {
    absent(serverHeader, token);
    absent(serverCode, token);
  }
  absent(controls, "IRrecv");
  absent(controls, "irLoop");
  absent(serverCode, "\"/ir.html\"");
  absent(serverCode, "\"/settings.html\"");
  for (const char* token : {"irrecord.html.gz", "ir.js.gz", "ir.css.gz"}) {
    absent(configCode, token);
    absent(verify, token);
    absent(assetBuilder, token);
    assert(!std::ifstream((std::string("data/www/") + token).c_str()).good());
  }
  absent(platformio, "IRremoteESP8266");
  struct stat irTree;
  assert(stat("src/IRremoteESP8266", &irTree) != 0);

  const auto modernHtml = readFile("web-src/voxone.html");
  const auto modernJs = readFile("web-src/voxone.js");
  for (const char* token : {"irbtn", "chkid", "irclr", "irtlp", "irrecord"}) {
    absent(modernHtml, token);
    absent(modernJs, token);
  }
}
