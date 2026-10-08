#pragma once

#define VOXONE_VERSION "0.2.0"
#define VOXONE_BUILD_CHANNEL "dev"

#ifndef VOXONE_BUILD_SHA
#define VOXONE_BUILD_SHA "unknown"
#endif

#if defined(VOXONE_PROFILE_DESK)
#define VOXONE_BUILD_PROFILE "DESK"
#elif defined(VOXONE_PROFILE_DIN)
#define VOXONE_BUILD_PROFILE "DIN"
#elif defined(VOXONE_PROFILE_SALON)
#define VOXONE_BUILD_PROFILE "SALON"
#else
#define VOXONE_BUILD_PROFILE "unknown"
#endif
