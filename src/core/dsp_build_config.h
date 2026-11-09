#ifndef VOXONE_DSP_BUILD_CONFIG_H
#define VOXONE_DSP_BUILD_CONFIG_H

// Firmware profiles declare this compile-time counterpart of
// HardwareCapabilities::supportsDsp. Native DSP unit tests deliberately use
// the complete implementation without selecting an embedded profile.
#ifndef VOXONE_INCLUDE_DSP
#if defined(ARDUINO)
#include "options.h"
#else
#define VOXONE_INCLUDE_DSP 1
#endif
#endif

#if VOXONE_INCLUDE_DSP != 0 && VOXONE_INCLUDE_DSP != 1
#error "VOXONE_INCLUDE_DSP must be 0 or 1"
#endif

#endif
