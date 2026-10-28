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
  static_assert(profileFor(DisplayKind::Ssd1306_128x32) ==
                    ProfileId::Oled128x32,
                "SSD1306 128x32 uses the compact OLED layout");
  static_assert(profileFor(DisplayKind::Sh1106_128x64) ==
                    ProfileId::Oled128x64,
                "SH1106 uses the shared OLED 128x64 layout");
  static_assert(profileFor(DisplayKind::Ch1116g_128x64) ==
                    ProfileId::Oled128x64,
                "CH1116G uses the shared OLED 128x64 layout");
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
  static_assert(controllerFor(DisplayKind::Ssd1306_128x32) ==
                    ControllerId::Ssd1306,
                "Both SSD1306 sizes use the SSD1306 controller");
  static_assert(controllerFor(DisplayKind::Sh1106_128x64) ==
                    ControllerId::Sh1106,
                "SH1106 keeps a distinct controller backend");
  static_assert(controllerFor(DisplayKind::Ch1116g_128x64) ==
                    ControllerId::Ch1116g,
                "CH1116G keeps a distinct controller backend");

  const Descriptor none = descriptor(ProfileId::None);
  const Descriptor oled = descriptor(ProfileId::Oled128x64);
  const Descriptor oled32 = descriptor(ProfileId::Oled128x32);
  const Descriptor st7789 = descriptor(ProfileId::St7789_284x76);
  const Descriptor st7796 = descriptor(ProfileId::St7796_480x320);
  assert(none.width == 0 && none.height == 0 && !none.monochrome);
  assert(controllerFor(DisplayKind::None) == ControllerId::None);
  assert(oled.width == 128 && oled.height == 64 && oled.monochrome);
  assert(oled32.width == 128 && oled32.height == 32 && oled32.monochrome);
  assert(st7789.width == 284 && st7789.height == 76 && !st7789.monochrome);
  assert(st7796.width == 480 && st7796.height == 320 && !st7796.monochrome);

  // Renderers select layouts, never PCB names. Backend drivers may retain
  // DSP_MODEL as the technical library/controller compile gate.
  const std::string display = readFile("src/core/display.cpp");
  const std::string displayHeader = readFile("src/core/display.h");
  const std::string widgets = readFile("src/displays/widgets/widgets.cpp");
  for (const std::string* source : {&display, &displayHeader, &widgets}) {
    assert(source->find("VOXONE_PROFILE_L0") == std::string::npos);
    assert(source->find("VOXONE_PROFILE_M0") == std::string::npos);
    assert(source->find("VOXONE_PROFILE_S0") == std::string::npos);
    assert(source->find("VOXONE_PROFILE_DIN0") == std::string::npos);
    assert(source->find("VOXONE_PROFILE_W0") == std::string::npos);
  }
  assert(display.find("VOXONE_DISPLAY_PROFILE_IS_SMALL_OLED") !=
         std::string::npos);
  assert(display.find("VOXONE_DISPLAY_PROFILE_ST7789_284X76") !=
         std::string::npos);
  assert(display.find("VOXONE_DISPLAY_PROFILE_ST7796_480X320") !=
         std::string::npos);
}
