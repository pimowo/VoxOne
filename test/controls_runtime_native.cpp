#include <cassert>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>

#include "../src/core/common.h"

namespace {

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void assertMissing(const std::string& text, const char* token) {
  assert(text.find(token) == std::string::npos);
}

}  // namespace

int main() {
  static_assert(EVT_ENCBTNB == 3, "primary encoder event changed");
  static_assert(EVT_BTNMODE == 7, "legacy event IDs must remain stable");

  const std::string implementation = readFile("src/core/controls.cpp");
  const std::string interface = readFile("src/core/controls.h");
  const std::string events = readFile("src/core/common.h");
  const std::string player = readFile("src/core/player.cpp");
  const std::string display = readFile("src/core/display.cpp");

  assert(implementation.find("void encoder1Loop()") != std::string::npos);
  assert(implementation.find("encoder.readEncoder_ISR()") != std::string::npos);
  assert(implementation.find("controlsEvent(encoderDelta > 0, encoderDelta)") !=
         std::string::npos);
  assert(implementation.find("case EVT_ENCBTNB") != std::string::npos);
  assert(implementation.find("void onBtnClick(int id)") != std::string::npos);
  assert(implementation.find("void onBtnDoubleClick(int id)") !=
         std::string::npos);
  assert(implementation.find("void onBtnLongPressStart(int id)") !=
         std::string::npos);
  assert(implementation.find("cycleNextSource()") != std::string::npos);
  assert(implementation.find("sourceManagerTransport(") != std::string::npos);
  assert(implementation.find("btEncoderClickAction(") != std::string::npos);
  assert(implementation.find("player.stepUserVol(encoderDelta)") !=
         std::string::npos);
  assert(implementation.find("player.stepUserVol(volDelta)") !=
         std::string::npos);
  assertMissing(implementation, "config.store.volume+encoderDelta");
  assertMissing(implementation, "config.store.volume+volDelta");
  assertMissing(implementation, "VOXONE_PROFILE_X0) || defined(VOXONE_PROFILE_A0) || defined(VOXONE_PROFILE_C0)");
  assert(player.find("void Player::stepVol(bool up) {\n  stepUserVol(up ? 1 : -1);\n}") !=
         std::string::npos);
  assert(display.find("constexpr uint16_t kDisplayVolumeMax = 100;") !=
         std::string::npos);
  assert(display.find("static uint8_t displayedVolume() { return config.userVolume; }") !=
         std::string::npos);
  assertMissing(display, "kDisplayVolumeMax = 254");
  assertMissing(display, "displayedVolume() { return config.store.volume; }");

  for (const char* token : {"IRrecv", "decode_results", "irrecv", "irResults",
                            "irLoop", "irVolRepeat", "irBlink", "irNumber",
                            "irRecordEnable", "IRremoteESP8266/", "#if IR_PIN"}) {
    assertMissing(implementation, token);
  }
  assertMissing(interface, "irLoop");
  assertMissing(interface, "IR_PLAY");

  assertMissing(implementation, "encoder2");
  assertMissing(implementation, "ENC2_");
  assertMissing(implementation, "EVT_ENC2BTNB");
  assertMissing(interface, "encoder2");
  assertMissing(interface, "ENC2_");
  assertMissing(events, "EVT_ENC2BTNB");
}
