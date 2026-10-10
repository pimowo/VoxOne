#ifndef VOXONE_PROFILE_H
#define VOXONE_PROFILE_H

#include "../src/hardware/hardware_descriptor.h"

namespace voxone {

using Mcu = hardware::McuFamily;
using Display = hardware::DisplayKind;
using AudioOutput = hardware::AudioOutputKind;

}  // namespace voxone

#if (defined(VOXONE_PROFILE_X0) + defined(VOXONE_PROFILE_B0) + \
     defined(VOXONE_PROFILE_C0) + \
     defined(VOXONE_PROFILE_A0) + defined(VOXONE_PROFILE_A0_DSP)) != 1
#error "Select exactly one VoxOne hardware profile"
#endif

#if defined(VOXONE_PROFILE_X0)
#include "x0.h"
#elif defined(VOXONE_PROFILE_B0)
#include "b0.h"
#elif defined(VOXONE_PROFILE_C0)
#include "c0.h"
#elif defined(VOXONE_PROFILE_A0)
#include "a0.h"
#elif defined(VOXONE_PROFILE_A0_DSP)
#include "a0_dsp.h"
#endif

#include "unavailable_hardware.h"

#if !VOXONE_PIN_MAP_COMPLETE
  #if defined(VOXONE_PROFILE_B0)
    #error "B0 profile is not buildable: pin map incomplete"
  #elif defined(VOXONE_PROFILE_A0)
    #error "A0 profile is not buildable: ST7796S, encoder and PCM5102A pin maps are not documented"
  #elif defined(VOXONE_PROFILE_A0_DSP)
    #error "A0_DSP profile is not buildable: ST7796S, encoder, PCM5102A and TDA7719 pin maps are not documented"
  #endif
#endif

#endif
