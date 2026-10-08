#include "../src/core/config_format.h"
#include "../src/core/www_readiness.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

bool exists(const char* path) {
  std::ifstream input(path, std::ios::binary);
  return input.good();
}

bool contains(const std::string& source, const char* token) {
  return source.find(token) != std::string::npos;
}
}  // namespace

int main() {
  static_assert(voxone::config_format::kConfigV7SerializedSize == 255,
                "Config v7 layout changed");
  static_assert(voxone::kCurrentWwwAssetCount == 6,
                "Current WWW asset contract changed");

  for (const char* path : {"data/www/script.js.gz", "data/www/style.css.gz",
                           "data/www/theme.css", "data/www/dragpl.js.gz",
                           "data/www/logo.svg.gz", "data/www/player.html.gz",
                           "data/www/options.html.gz", "data/www/updform.html.gz"})
    assert(!exists(path));

  for (const char* path : {"data/www/voxone.html.gz", "data/www/voxone.css.gz",
                           "data/www/voxone.js.gz", "data/www/advanced-audio.js.gz",
                           "data/www/dsp-client.js.gz", "data/www/voxone-logo.svg.gz"})
    assert(exists(path));

  const auto config = readFile("src/core/config.cpp");
  const auto configHeader = readFile("src/core/config.h");
  const auto server = readFile("src/core/netserver.cpp");
  const auto serverHeader = readFile("src/core/netserver.h");
  const auto readiness = readFile("src/core/www_readiness.h");

  assert(!contains(serverHeader, "index_html"));
  assert(!contains(config, "/www/settings.html"));
  assert(!contains(config, "_removeObsoleteWwwFiles"));
  assert(!contains(configHeader, "_removeObsoleteWwwFiles"));

  assert(contains(configHeader, "currentWwwReady"));
  assert(contains(config, "currentWwwReady = _hasCurrentWwwAssets()"));
  for (const char* path : {"/www/voxone.html.gz", "/www/voxone.css.gz",
                           "/www/voxone.js.gz", "/www/advanced-audio.js.gz",
                           "/www/dsp-client.js.gz", "/www/voxone-logo.svg.gz"})
    assert(contains(readiness, path));

  assert(contains(server, "serveStatic(\"/\", SPIFFS, \"/www/\")"));
  assert(contains(serverHeader, "emptyfs_html"));
  assert(contains(serverHeader, "emergency_form"));
  assert(contains(server, "request->url()==\"/update\""));
  assert(contains(server, "request->url()==\"/emergency\""));
}
