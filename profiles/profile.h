#ifndef VOXONE_PROFILE_H
#define VOXONE_PROFILE_H

#include "../src/hardware/hardware_descriptor.h"

namespace voxone {

using Mcu = hardware::McuFamily;
using Display = hardware::DisplayKind;
using AudioOutput = hardware::AudioOutputKind;

}  // namespace voxone

#if (defined(VOXONE_PROFILE_W0) + defined(VOXONE_PROFILE_M0) + \
     defined(VOXONE_PROFILE_S0) + \
     defined(VOXONE_PROFILE_DIN0) + defined(VOXONE_PROFILE_L0)) != 1
#error "Select exactly one VoxOne hardware profile"
#endif

#if defined(VOXONE_PROFILE_W0)
#include "w0.h"
#elif defined(VOXONE_PROFILE_M0)
#include "m0.h"
#elif defined(VOXONE_PROFILE_S0)
#include "s0.h"
#elif defined(VOXONE_PROFILE_DIN0)
#include "din0.h"
#elif defined(VOXONE_PROFILE_L0)
#include "l0.h"
#endif

#include "unavailable_hardware.h"
#include "../src/displays/display_profile.h"

#if !VOXONE_PIN_MAP_COMPLETE
#error "Selected hardware profile has an incomplete pin map"
#endif

#endif
