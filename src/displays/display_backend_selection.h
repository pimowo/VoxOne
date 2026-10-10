#ifndef VOXONE_DISPLAY_BACKEND_SELECTION_H
#define VOXONE_DISPLAY_BACKEND_SELECTION_H

#include <stdint.h>

#include "display_profile.h"
#include "../hardware/runtime_hardware_config.h"

namespace voxone {
namespace display_backend {

using ControllerMask = uint32_t;

constexpr ControllerMask controllerSupport(
    display_profile::ControllerId controller) {
  return controller == display_profile::ControllerId::None
             ? 0u
             : (1u << static_cast<uint8_t>(controller));
}

struct CompiledBackends {
  ControllerMask controllers;
  bool runtimeSelection;

  constexpr bool contains(display_profile::ControllerId controller) const {
    return controller != display_profile::ControllerId::None &&
           (controllers & controllerSupport(controller)) != 0;
  }
};

struct Selection {
  hardware::RuntimeDisplay display;
  display_profile::ControllerId controller;
  display_profile::ProfileId profile;
  bool usedBuildDefault;
};

constexpr CompiledBackends compiledBackends() {
#if defined(VOXONE_RUNTIME_OLED_128X64_BACKENDS)
  return CompiledBackends{
      controllerSupport(display_profile::ControllerId::Ssd1306) |
          controllerSupport(display_profile::ControllerId::Ssd1309),
      true};
#elif defined(VOXONE_DISPLAY_CONTROLLER)
  return CompiledBackends{
      controllerSupport(static_cast<display_profile::ControllerId>(
          VOXONE_DISPLAY_CONTROLLER)),
      false};
#else
  return CompiledBackends{0u, false};
#endif
}

Selection select(const hardware::RuntimeHardwareConfig& requested,
                 const hardware::HardwareDescriptor& hardware,
                 CompiledBackends compiled);

const char* runtimeDisplayName(hardware::RuntimeDisplay display);
const char* controllerName(display_profile::ControllerId controller);
const char* profileName(display_profile::ProfileId profile);

}  // namespace display_backend
}  // namespace voxone

#endif
