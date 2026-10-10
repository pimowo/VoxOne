#include "display_backend_selection.h"

namespace voxone {
namespace display_backend {
namespace {

Selection buildDefault(const hardware::HardwareDescriptor& hardware) {
  const hardware::RuntimeHardwareConfig config =
      hardware::defaultRuntimeHardwareConfig(hardware);
  return Selection{config.display,
                   hardware::runtimeDisplayController(config.display),
                   hardware::runtimeDisplayProfile(config.display), true};
}

}  // namespace

Selection select(const hardware::RuntimeHardwareConfig& requested,
                 const hardware::HardwareDescriptor& hardware,
                 CompiledBackends compiled) {
  const Selection fallback = buildDefault(hardware);
  if (hardware::validateRuntimeHardwareConfig(requested,
                                               hardware.capabilities) !=
      hardware::RuntimeHardwareValidation::Valid)
    return fallback;

  const display_profile::ControllerId controller =
      hardware::runtimeDisplayController(requested.display);
  if (controller == display_profile::ControllerId::None)
    return Selection{hardware::RuntimeDisplay::None, controller,
                     display_profile::ProfileId::None, false};
  if (!compiled.runtimeSelection) return fallback;
  if (!compiled.contains(controller)) return fallback;

  return Selection{requested.display, controller,
                   hardware::runtimeDisplayProfile(requested.display), false};
}

const char* runtimeDisplayName(hardware::RuntimeDisplay display) {
  switch (display) {
    case hardware::RuntimeDisplay::None: return "NONE";
    case hardware::RuntimeDisplay::Ssd1306_128x64: return "SSD1306_128X64";
    case hardware::RuntimeDisplay::Ssd1309_128x64: return "SSD1309_128X64";
    case hardware::RuntimeDisplay::St7789_284x76: return "ST7789_284X76";
    case hardware::RuntimeDisplay::St7796s_480x320: return "ST7796S_480X320";
  }
  return "UNKNOWN";
}

const char* controllerName(display_profile::ControllerId controller) {
  switch (controller) {
    case display_profile::ControllerId::None: return "NONE";
    case display_profile::ControllerId::Ssd1306: return "SSD1306";
    case display_profile::ControllerId::Ssd1309: return "SSD1309";
    case display_profile::ControllerId::St7789: return "ST7789";
    case display_profile::ControllerId::St7796: return "ST7796";
  }
  return "UNKNOWN";
}

const char* profileName(display_profile::ProfileId profile) {
  switch (profile) {
    case display_profile::ProfileId::None: return "NONE";
    case display_profile::ProfileId::Oled128x64: return "OLED_128X64";
    case display_profile::ProfileId::St7789_284x76: return "ST7789_284X76";
    case display_profile::ProfileId::St7796_480x320: return "ST7796_480X320";
  }
  return "UNKNOWN";
}

}  // namespace display_backend
}  // namespace voxone
