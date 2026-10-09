#pragma once

#define VOXONE_VERSION "0.2.0"
#define VOXONE_BUILD_CHANNEL "dev"

#ifndef VOXONE_BUILD_SHA
#define VOXONE_BUILD_SHA "unknown"
#endif

#if defined(VOXONE_PROFILE_X0)
#define VOXONE_BUILD_PROFILE "X0"
#elif defined(VOXONE_PROFILE_B0)
#define VOXONE_BUILD_PROFILE "B0"
#elif defined(VOXONE_PROFILE_C0)
#define VOXONE_BUILD_PROFILE "C0"
#elif defined(VOXONE_PROFILE_A0)
#define VOXONE_BUILD_PROFILE "A0"
#else
#define VOXONE_BUILD_PROFILE "unknown"
#endif
