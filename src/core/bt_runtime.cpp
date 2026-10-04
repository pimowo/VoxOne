#include "bt_runtime.h"
#include "../hardware/hardware_descriptor.h"

BtRuntime btRuntime(
    voxone::hardware::currentHardware().capabilities.supportsVoxOneBt);
