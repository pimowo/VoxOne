#include "i2c_runtime.h"

#include "hardware_descriptor.h"

namespace voxone {
namespace hardware {
namespace {
TwoWire bus(0);
bool initialized = false;
}  // namespace

TwoWire& hardwareI2cBus() { return bus; }

bool initHardwareI2c() {
  if (initialized) return true;
  const auto& config = currentHardware().i2c;
  if (!hasPin(config.sda) || !hasPin(config.scl)) return false;
  initialized = bus.begin(config.sda, config.scl, config.frequencyHz);
  return initialized;
}

}  // namespace hardware
}  // namespace voxone
