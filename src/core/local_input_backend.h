#ifndef VOXONE_LOCAL_INPUT_BACKEND_H
#define VOXONE_LOCAL_INPUT_BACKEND_H

#include "../hardware/runtime_hardware_config.h"

namespace voxone {
namespace input {

enum class Backend : uint8_t { None, Ec11, Buttons3 };

struct Activation {
  Backend backend;
  bool ec11;
  bool buttons3;
};

constexpr Activation activationFor(hardware::RuntimeInput input) {
  return input == hardware::RuntimeInput::Ec11
             ? Activation{Backend::Ec11, true, false}
         : input == hardware::RuntimeInput::Buttons3
             ? Activation{Backend::Buttons3, false, true}
             : Activation{Backend::None, false, false};
}

constexpr bool configuresGpio(const Activation& activation) {
  return activation.ec11 || activation.buttons3;
}

constexpr bool attachesIsr(const Activation& activation) {
  return activation.ec11;
}

constexpr bool usesOneButton(const Activation& activation) {
  return activation.ec11 || activation.buttons3;
}

constexpr const char* backendName(Backend backend) {
  return backend == Backend::Ec11 ? "ec11" :
         backend == Backend::Buttons3 ? "buttons3" : "none";
}

}  // namespace input
}  // namespace voxone

#endif
