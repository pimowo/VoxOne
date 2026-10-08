#include "../src/core/config_format.h"

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

bool contains(const std::string& source, const char* token) {
  return source.find(token) != std::string::npos;
}
}  // namespace

int main() {
  static_assert(voxone::config_format::kConfigV7SerializedSize == 255,
                "Config v7 layout changed");

  const auto server = readFile("src/core/netserver.cpp");
  const auto serverHeader = readFile("src/core/netserver.h");
  const auto config = readFile("src/core/config.cpp");
  const auto currentHtml = readFile("web-src/voxone.html");

  // The legacy Webboard route and its arbitrary per-file upload are retired.
  assert(!contains(server, "/webboard"));
  assert(!contains(serverHeader, "/webboard"));
  assert(!contains(server, "String spath = \"/www/\""));
  assert(!contains(server, "filename==\"wifi.csv\""));
  assert(!contains(server, "request->_tempFile"));
  assert(!contains(server, "formAction"));

  // Wi-Fi can still be provisioned from the embedded root recovery page.
  assert(contains(server, "apWifiRecoveryAllowed"));
  assert(contains(server, "config.saveWifiCredentials"));
  assert(contains(serverHeader, "emptyfs_html"));
  assert(contains(serverHeader, "form action=\"/\" method=\"post\""));

  // Recovery remains embedded and supports both firmware and full SPIFFS images.
  assert(contains(server, "request->url()==\"/emergency\""));
  assert(contains(server, "request->url()==\"/update\""));
  assert(contains(server, "handleWebUpdateUpload"));
  assert(contains(server, "target == \"firmware\""));
  assert(contains(server, "target == \"spiffs\""));
  assert(contains(server, "U_FLASH"));
  assert(contains(server, "U_SPIFFS"));
  assert(contains(serverHeader, "emergency_form"));
  assert(contains(serverHeader, "value=\"firmware\""));
  assert(contains(serverHeader, "value=\"spiffs\""));

  // Web Update preserves Wi-Fi and station data around a full SPIFFS update.
  assert(contains(server, "{SSIDS_PATH, \"wifi\"}"));
  assert(contains(server, "backupWebUpdateData"));
  assert(contains(server, "restoreWebUpdateData"));
  assert(contains(config, "saveWifiCredentials"));

  // Current WWW and the station API remain available.
  assert(contains(server, "serveStatic(\"/voxone.html\""));
  assert(contains(currentHtml, "VoxOne"));
  assert(contains(server, "/api/stations/import"));
  assert(contains(server, "/api/stations/export"));
}
