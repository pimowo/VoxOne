#include <cassert>
#include <cstdint>
#include <fstream>
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

  assertMissing(implementation, "encoder2");
  assertMissing(implementation, "ENC2_");
  assertMissing(implementation, "EVT_ENC2BTNB");
  assertMissing(interface, "encoder2");
  assertMissing(interface, "ENC2_");
  assertMissing(events, "EVT_ENC2BTNB");
}
