#include "../src/displays/display_profile.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

using namespace voxone::display_profile;
using voxone::hardware::DisplayKind;

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}
}

int main() {
  static_assert(profileFor(DisplayKind::None) == ProfileId::None,
                "Headless boards use the NONE profile");
  static_assert(profileFor(DisplayKind::Ssd1306_128x64) ==
                    ProfileId::Oled128x64,
                "SSD1306 uses the shared OLED layout");
  static_assert(profileFor(DisplayKind::Ssd1309_128x64) ==
                    ProfileId::Oled128x64,
                "SSD1309 uses the shared OLED layout");
  static_assert(profileFor(DisplayKind::St7789_284x76) ==
                    ProfileId::St7789_284x76,
                "ST7789 284x76 uses its own layout");
  static_assert(profileFor(DisplayKind::St7796_480x320) ==
                    ProfileId::St7796_480x320,
                "ST7796 480x320 uses its own layout");
  static_assert(controllerFor(DisplayKind::Ssd1306_128x64) ==
                    ControllerId::Ssd1306,
                "SSD1306 controller remains distinct");
  static_assert(controllerFor(DisplayKind::Ssd1309_128x64) ==
                    ControllerId::Ssd1309,
                "SSD1309 controller remains distinct");

  const Descriptor oled = descriptor(ProfileId::Oled128x64);
  const Descriptor st7789 = descriptor(ProfileId::St7789_284x76);
  const Descriptor st7796 = descriptor(ProfileId::St7796_480x320);
  assert(oled.width == 128 && oled.height == 64 && oled.monochrome);
  assert(st7789.width == 284 && st7789.height == 76 && !st7789.monochrome);
  assert(st7796.width == 480 && st7796.height == 320 && !st7796.monochrome);

  // Renderers select layouts, never PCB names. Backend drivers may retain
  // DSP_MODEL as the technical library/controller compile gate.
  const std::string display = readFile("src/core/display.cpp");
  const std::string displayHeader = readFile("src/core/display.h");
  const std::string widgets = readFile("src/displays/widgets/widgets.cpp");
  for (const std::string* source : {&display, &displayHeader, &widgets}) {
    assert(source->find("VOXONE_PROFILE_A0") == std::string::npos);
    assert(source->find("VOXONE_PROFILE_B0") == std::string::npos);
    assert(source->find("VOXONE_PROFILE_C0") == std::string::npos);
    assert(source->find("VOXONE_PROFILE_X0") == std::string::npos);
  }
  assert(display.find("VOXONE_DISPLAY_PROFILE_OLED_128X64") !=
         std::string::npos);
  assert(display.find("VOXONE_DISPLAY_PROFILE_ST7789_284X76") !=
         std::string::npos);
  assert(display.find("VOXONE_DISPLAY_PROFILE_ST7796_480X320") !=
         std::string::npos);
}
