#ifndef VOXONE_DISPLAY_PROFILE_H
#define VOXONE_DISPLAY_PROFILE_H

#include <stdint.h>

#include "../hardware/hardware_descriptor.h"

// Numeric values are intentionally available to the preprocessor. Renderer
// code uses the layout profile, while dspcore and controller drivers keep
// using their technical backend compile gates.
#define VOXONE_DISPLAY_PROFILE_NONE 0
#define VOXONE_DISPLAY_PROFILE_OLED_128X64 1
#define VOXONE_DISPLAY_PROFILE_ST7789_284X76 2
#define VOXONE_DISPLAY_PROFILE_ST7796_480X320 3

#define VOXONE_DISPLAY_CONTROLLER_NONE 0
#define VOXONE_DISPLAY_CONTROLLER_SSD1306 1
#define VOXONE_DISPLAY_CONTROLLER_SSD1309 2
#define VOXONE_DISPLAY_CONTROLLER_ST7789 3
#define VOXONE_DISPLAY_CONTROLLER_ST7796 4

#if defined(VOXONE_DISPLAY_PROFILE) != defined(VOXONE_DISPLAY_CONTROLLER)
#error "Display profile and controller must be selected together"
#endif

namespace voxone {
namespace display_profile {

enum class ProfileId : uint8_t {
  None = VOXONE_DISPLAY_PROFILE_NONE,
  Oled128x64 = VOXONE_DISPLAY_PROFILE_OLED_128X64,
  St7789_284x76 = VOXONE_DISPLAY_PROFILE_ST7789_284X76,
  St7796_480x320 = VOXONE_DISPLAY_PROFILE_ST7796_480X320
};

enum class ControllerId : uint8_t {
  None = VOXONE_DISPLAY_CONTROLLER_NONE,
  Ssd1306 = VOXONE_DISPLAY_CONTROLLER_SSD1306,
  Ssd1309 = VOXONE_DISPLAY_CONTROLLER_SSD1309,
  St7789 = VOXONE_DISPLAY_CONTROLLER_ST7789,
  St7796 = VOXONE_DISPLAY_CONTROLLER_ST7796
};

struct Descriptor {
  ProfileId id;
  uint16_t width;
  uint16_t height;
  bool monochrome;
};

constexpr ProfileId profileFor(hardware::DisplayKind kind) {
  return kind == hardware::DisplayKind::None ? ProfileId::None :
      (kind == hardware::DisplayKind::Ssd1306_128x64 ||
       kind == hardware::DisplayKind::Ssd1309_128x64) ? ProfileId::Oled128x64 :
      kind == hardware::DisplayKind::St7789_284x76 ? ProfileId::St7789_284x76 :
      ProfileId::St7796_480x320;
}

constexpr ControllerId controllerFor(hardware::DisplayKind kind) {
  return kind == hardware::DisplayKind::None ? ControllerId::None :
      kind == hardware::DisplayKind::Ssd1306_128x64 ? ControllerId::Ssd1306 :
      kind == hardware::DisplayKind::Ssd1309_128x64 ? ControllerId::Ssd1309 :
      kind == hardware::DisplayKind::St7789_284x76 ? ControllerId::St7789 :
      ControllerId::St7796;
}

constexpr Descriptor descriptor(ProfileId id) {
  return id == ProfileId::None ? Descriptor{id, 0, 0, false} :
      id == ProfileId::Oled128x64 ? Descriptor{id, 128, 64, true} :
      id == ProfileId::St7789_284x76 ? Descriptor{id, 284, 76, false} :
      Descriptor{id, 480, 320, false};
}

static_assert(profileFor(hardware::DisplayKind::Ssd1306_128x64) ==
                  ProfileId::Oled128x64,
              "SSD1306 must use the shared OLED 128x64 layout");
static_assert(profileFor(hardware::DisplayKind::Ssd1309_128x64) ==
                  ProfileId::Oled128x64,
              "SSD1309 must use the shared OLED 128x64 layout");
static_assert(controllerFor(hardware::DisplayKind::Ssd1306_128x64) !=
                  controllerFor(hardware::DisplayKind::Ssd1309_128x64),
              "OLED controllers remain distinct physical backends");

#if defined(VOXONE_DISPLAY_PROFILE)
constexpr ProfileId activeProfile() {
  return static_cast<ProfileId>(VOXONE_DISPLAY_PROFILE);
}
constexpr ControllerId activeController() {
  return static_cast<ControllerId>(VOXONE_DISPLAY_CONTROLLER);
}
constexpr Descriptor activeDescriptor() { return descriptor(activeProfile()); }
#endif

}  // namespace display_profile
}  // namespace voxone

#endif
