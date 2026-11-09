#define VOXONE_INCLUDE_DSP 0
#include "../src/core/www_readiness.h"

#include <cassert>
#include <cstring>

int main() {
  static_assert(voxone::kCurrentWwwAssetCount == 5,
                "Non-DSP WWW contract must contain five assets");
  for (std::size_t index = 0; index < voxone::kCurrentWwwAssetCount; ++index) {
    assert(std::strstr(voxone::kCurrentWwwAssetPaths[index],
                       "advanced-audio.js") == nullptr);
    assert(std::strstr(voxone::kCurrentWwwAssetPaths[index],
                       "dsp-client.js") == nullptr);
  }
}
