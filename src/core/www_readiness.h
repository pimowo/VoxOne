#pragma once

#include <cstddef>

namespace voxone {

static const char* const kCurrentWwwAssetPaths[] = {
    "/www/voxone.html.gz",
    "/www/voxone.css.gz",
    "/www/voxone.js.gz",
    "/www/advanced-audio.js.gz",
    "/www/dsp-client.js.gz",
    "/www/voxone-logo.svg.gz",
};

constexpr size_t kCurrentWwwAssetCount =
    sizeof(kCurrentWwwAssetPaths) / sizeof(kCurrentWwwAssetPaths[0]);

template <typename Exists>
bool currentWwwAssetsReady(Exists exists) {
  for (size_t i = 0; i < kCurrentWwwAssetCount; ++i)
    if (!exists(kCurrentWwwAssetPaths[i])) return false;
  return true;
}

inline bool radioPlaylistShouldInitialize(bool spiffsMounted,
                                          bool radioPlaylistReady) {
  return spiffsMounted && radioPlaylistReady;
}

}  // namespace voxone
