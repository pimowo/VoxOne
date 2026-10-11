#if defined(BUILD_ID_TEST_CLEAN)
#define VOXONE_BUILD_SHA "192a400"
#elif defined(BUILD_ID_TEST_DIRTY)
#define VOXONE_BUILD_SHA "192a400-dirty"
#endif
#include "../src/core/version.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
  assert(std::strcmp(VOXONE_VERSION, "0.8.0") == 0);
  assert(std::strcmp(VOXONE_BUILD_CHANNEL, "dev") == 0);
#if defined(BUILD_ID_TEST_CLEAN)
  const char* expectedBuild = "192a400";
#elif defined(BUILD_ID_TEST_DIRTY)
  const char* expectedBuild = "192a400-dirty";
#else
  const char* expectedBuild = "unknown";
#endif
  assert(std::strcmp(VOXONE_BUILD_SHA, expectedBuild) == 0);

#if defined(VOXONE_PROFILE_W0)
  const char* expectedProfile = "W0";
#elif defined(VOXONE_PROFILE_M0)
  const char* expectedProfile = "M0";
#elif defined(VOXONE_PROFILE_S0)
  const char* expectedProfile = "S0";
#elif defined(VOXONE_PROFILE_DIN0)
  const char* expectedProfile = "DIN0";
#elif defined(VOXONE_PROFILE_L0)
  const char* expectedProfile = "L0";
#else
  const char* expectedProfile = "unknown";
#endif
  assert(std::strcmp(VOXONE_BUILD_PROFILE, expectedProfile) == 0);
  char identity[80];
  const int length = std::snprintf(identity, sizeof(identity),
      "VoxOne v%s-%s\nbuild: %s\nprofile: %s", VOXONE_VERSION,
      VOXONE_BUILD_CHANNEL, VOXONE_BUILD_SHA, VOXONE_BUILD_PROFILE);
  assert(length > 0 && static_cast<size_t>(length) < sizeof(identity));
  char expectedPrefix[65];
  std::snprintf(expectedPrefix, sizeof(expectedPrefix),
      "VoxOne v0.8.0-dev\nbuild: %s\nprofile: ", expectedBuild);
  assert(std::strstr(identity, expectedPrefix) == identity);
}
