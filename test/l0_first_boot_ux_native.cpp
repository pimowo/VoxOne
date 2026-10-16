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
}

int main() {
  const std::string driver = readFile("src/displays/displayST7796.cpp");
  const std::string network = readFile("src/core/network.cpp");
  const std::string server = readFile("src/core/netserver.cpp");
  const std::string page = readFile("src/core/netserver.h");
  const std::string display = readFile("src/core/display.cpp");

  assert(driver.find("config.store.flipscreen?1:3") != std::string::npos);
  assert(network.find("firstBootSetup ? WiFi.softAP(apSsid)") !=
         std::string::npos);
  assert(network.find("WiFi.softAP(apSsid, apPassword)") !=
         std::string::npos);
  assert(server.find("WiFi.scanNetworks(true, false)") != std::string::npos);
  assert(server.find("webserver.on(\"/api/wifi/scan\", HTTP_GET") !=
         std::string::npos);
  assert(server.find("completeWifiProvisioningOpen()") != std::string::npos);
  assert(page.find("id=\"ssid-list\"") != std::string::npos);
  assert(page.find("id=\"manual-ssid\"") != std::string::npos);
  assert(page.find("src=\"/voxone-logo.svg\"") != std::string::npos);
  assert(display.find("firstBootView.title") !=
         std::string::npos);
  assert(display.find("if (firstBootSetup) dsp.drawLogo(bootLogoTop)") !=
         std::string::npos);
}
