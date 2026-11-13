#pragma once

#include <cstddef>
#include <cstdint>

#include "dsp_build_config.h"

namespace voxone {

constexpr uint16_t kWwwManifestSchema = 1;
constexpr uint16_t kFirmwareWwwContract = 1;
constexpr uint16_t kCurrentBundleWwwContract = 1;
constexpr uint16_t kCurrentBundleFirmwareContractMin = 1;
constexpr uint16_t kCurrentBundleFirmwareContractMax = 1;
constexpr const char* kWwwAssetManifestPath = "/www/assets.vxwm";

static const char* const kCurrentWwwAssetPaths[] = {
    "/www/voxone.html.gz",
    "/www/voxone.css.gz",
    "/www/voxone.js.gz",
#if VOXONE_INCLUDE_DSP
    "/www/advanced-audio.js.gz",
    "/www/dsp-client.js.gz",
#endif
    "/www/sprite.svg.gz",
    "/www/icon-192.png.gz",
    "/www/icon-512.png.gz",
    "/www/manifest.json.gz",
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
