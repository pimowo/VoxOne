#ifndef VOXONE_I2C_RUNTIME_H
#define VOXONE_I2C_RUNTIME_H

#include <Wire.h>

namespace voxone {
namespace hardware {

TwoWire& hardwareI2cBus();
bool initHardwareI2c();

}  // namespace hardware
}  // namespace voxone

#endif
