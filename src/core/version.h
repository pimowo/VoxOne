#pragma once

#include "../hardware/hardware_descriptor.h"

#define VOXONE_VERSION "1.1.0"
#define VOXONE_BUILD_CHANNEL "dev"

#ifndef VOXONE_BUILD_SHA
#define VOXONE_BUILD_SHA "unknown"
#endif

#define VOXONE_BUILD_PROFILE \
  (voxone::hardware::currentHardware().board.name)
