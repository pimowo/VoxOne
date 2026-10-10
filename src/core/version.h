#pragma once

#define VOXONE_VERSION "0.2.0"
#define VOXONE_BUILD_CHANNEL "dev"

#ifndef VOXONE_BUILD_SHA
#define VOXONE_BUILD_SHA "unknown"
#endif

#if defined(VOXONE_PROFILE_W0)
#define VOXONE_BUILD_PROFILE "W0"
#elif defined(VOXONE_PROFILE_M0)
#define VOXONE_BUILD_PROFILE "M0"
#elif defined(VOXONE_PROFILE_S0)
#define VOXONE_BUILD_PROFILE "S0"
#elif defined(VOXONE_PROFILE_DIN0)
#define VOXONE_BUILD_PROFILE "DIN0"
#elif defined(VOXONE_PROFILE_L0)
#define VOXONE_BUILD_PROFILE "L0"
#else
#define VOXONE_BUILD_PROFILE "unknown"
#endif
