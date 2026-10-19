#pragma once

#include <cstddef>
#include <cstdint>

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
    "/www/advanced-audio.js.gz",
    "/www/dsp-client.js.gz",
    "/www/voxone-logo.svg.gz",
    "/www/voxone-symbol.svg.gz",
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
