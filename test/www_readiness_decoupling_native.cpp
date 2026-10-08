#include "../src/core/config_format.h"
#include "../src/core/www_readiness.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

bool contains(const std::string& source, const char* token) {
  return source.find(token) != std::string::npos;
}
}  // namespace

int main() {
  static_assert(voxone::kCurrentWwwAssetCount == 6,
                "Current WWW asset contract changed");
  static_assert(voxone::config_format::kConfigV7SerializedSize == 255,
                "Config v7 layout changed");

  std::set<std::string> files;
  for (size_t i = 0; i < voxone::kCurrentWwwAssetCount; ++i)
    files.insert(voxone::kCurrentWwwAssetPaths[i]);
  const auto exists = [&files](const char* path) {
    return files.count(path) != 0;
  };
  assert(voxone::currentWwwAssetsReady(exists));

  // Legacy files neither make current WWW ready nor participate in its contract.
  files.clear();
  files.insert("/www/script.js.gz");
  files.insert("/www/style.css.gz");
  files.insert("/www/theme.css");
  files.insert("/www/dragpl.js.gz");
  files.insert("/www/logo.svg.gz");
  files.insert("/www/player.html.gz");
  files.insert("/www/options.html.gz");
  files.insert("/www/updform.html.gz");
  assert(!voxone::currentWwwAssetsReady(exists));
  for (size_t i = 0; i < voxone::kCurrentWwwAssetCount; ++i)
    files.insert(voxone::kCurrentWwwAssetPaths[i]);
  assert(voxone::currentWwwAssetsReady(exists));
  files.erase(voxone::kCurrentWwwAssetPaths[0]);
  assert(!voxone::currentWwwAssetsReady(exists));

  // Radio readiness depends only on the mounted filesystem and PlaylistStore.
  assert(voxone::radioPlaylistShouldInitialize(true, true));
  assert(!voxone::radioPlaylistShouldInitialize(false, true));
  assert(!voxone::radioPlaylistShouldInitialize(true, false));

  const auto config = readFile("src/core/config.cpp");
  const auto configHeader = readFile("src/core/config.h");
  const auto server = readFile("src/core/netserver.cpp");
  const auto serverHeader = readFile("src/core/netserver.h");
  const auto playlistStore = readFile("src/core/playlist_store.cpp");
  const auto currentWeb = readFile("web-src/voxone.js");

  assert(!contains(config, "_isFSempty"));
  assert(!contains(configHeader, "emptyFS"));
  assert(contains(config, "playlistStore.begin() && playlistStore.recover()"));
  assert(contains(config, "radioPlaylistShouldInitialize"));
  assert(contains(playlistStore, "Initialized empty VoxOne Stations v1 store"));
  assert(contains(server, "/api/stations/import"));
  assert(contains(server, "/api/stations/export"));

  assert(contains(server, "apWifiRecoveryAllowed"));
  assert(contains(server, "request->url()==\"/emergency\""));
  assert(contains(server, "request->url()==\"/update\""));
  assert(contains(serverHeader, "emptyfs_html"));

  // Current WWW returns directly to its page, so the legacy root query path
  // cannot dispatch the update marker through CommandHandler.
  assert(contains(currentWeb, "location.replace(\"/voxone.html?updated="));
  assert(!contains(currentWeb, "location.replace(\"/?updated="));

  // Legacy files remain in this stage, but none is part of readiness.
  for (const char* path : {"data/www/script.js.gz", "data/www/style.css.gz",
                           "data/www/theme.css", "data/www/dragpl.js.gz",
                           "data/www/logo.svg.gz", "data/www/player.html.gz",
                           "data/www/options.html.gz", "data/www/updform.html.gz"}) {
    std::ifstream legacy(path, std::ios::binary);
    assert(legacy.good());
  }
}
