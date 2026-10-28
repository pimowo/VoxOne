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

struct ActivationPlan {
  bool initializeBackend;
  bool allocateFramebuffer;
  bool runRenderer;
  bool createDisplayTask;
};

enum class RendererKind : uint8_t {
  None,
  Oled128x32,
  Oled128x64,
  St7796
};

constexpr RendererKind rendererKind(const Selection& selection) {
  return selection.profile == display_profile::ProfileId::Oled128x32
             ? RendererKind::Oled128x32
             : selection.profile == display_profile::ProfileId::Oled128x64
                   ? RendererKind::Oled128x64
                   : selection.profile ==
                             display_profile::ProfileId::St7796_480x320
                         ? RendererKind::St7796
                         : RendererKind::None;
}

constexpr ActivationPlan activationPlan(const Selection& selection) {
  // DisplayTask also services timekeeper.loop0() and netserver.loop().
  return selection.controller == display_profile::ControllerId::None
             ? ActivationPlan{false, false, false, true}
             : ActivationPlan{true, true, true, true};
}

constexpr CompiledBackends compiledBackends() {
#if defined(VOXONE_RUNTIME_L0_DISPLAY_BACKENDS)
  return CompiledBackends{
      controllerSupport(display_profile::ControllerId::Ssd1306) |
          controllerSupport(display_profile::ControllerId::Ssd1309) |
          controllerSupport(display_profile::ControllerId::St7796) |
          controllerSupport(display_profile::ControllerId::Sh1106) |
          controllerSupport(display_profile::ControllerId::Ch1116g),
      true};
#elif defined(VOXONE_RUNTIME_OLED_128X64_BACKENDS)
  return CompiledBackends{
      controllerSupport(display_profile::ControllerId::Ssd1306) |
          controllerSupport(display_profile::ControllerId::Ssd1309) |
          controllerSupport(display_profile::ControllerId::Sh1106) |
          controllerSupport(display_profile::ControllerId::Ch1116g),
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
