#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

#include "../src/core/first_boot_core.h"

namespace {
using voxone::first_boot::Connection;
using voxone::first_boot::Inputs;
using voxone::first_boot::Mode;

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

Inputs input(uint8_t profiles, Connection connection) {
  return {profiles, connection, false, false, "VoxOne-A1B2C3",
          "http://192.168.4.1/", 0};
}
}  // namespace

int main() {
#if !(defined(VOXONE_PROFILE_W0) || defined(VOXONE_PROFILE_M0) || \
      defined(VOXONE_PROFILE_L0) || defined(VOXONE_PROFILE_S0) || \
      defined(VOXONE_PROFILE_DIN0))
#error "FIRST-BOOT-CORE native test requires a VoxOne profile"
#endif

  Inputs clean = input(0, Connection::SetupAp);
  auto view = voxone::first_boot::makeView(clean);
  assert(view.mode == Mode::FirstBootSetup);
  assert(view.setupApOpen && view.wifiScanAvailable);
  assert(std::string(view.product) == "VoxOne");
  assert(std::string(view.title) == "KONFIGURACJA");
  assert(std::string(view.apSsid) == "VoxOne-A1B2C3");
  assert(std::string(view.address) == "http://192.168.4.1/");

  assert(voxone::first_boot::resolveMode(
             input(1, Connection::Connecting)) == Mode::WifiConnecting);
  Inputs configured = input(1, Connection::Connected);
  configured.credentialsJustSaved = true;
  assert(voxone::first_boot::resolveMode(configured) == Mode::WifiConfigured);
  assert(voxone::first_boot::resolveMode(
             input(1, Connection::Connected)) == Mode::Normal);
  assert(voxone::first_boot::resolveMode(
             input(1, Connection::RecoveryAp)) == Mode::WifiRecovery);
  assert(!voxone::first_boot::makeView(
              input(1, Connection::RecoveryAp)).setupApOpen);

  Inputs security = input(0, Connection::SetupAp);
  security.securityRecovery = true;
  assert(voxone::first_boot::resolveMode(security) == Mode::SecurityRecovery);
  assert(!voxone::first_boot::makeView(security).wifiScanAvailable);

  assert(voxone::first_boot::shouldStartOpenSetupAp(0));
  assert(!voxone::first_boot::shouldStartOpenSetupAp(1));

  const std::string network = readFile("src/core/network.cpp");
  const std::string server = readFile("src/core/netserver.cpp");
  const std::string display = readFile("src/core/display.cpp");
  const std::string main = readFile("src/main.cpp");
  assert(network.find("VOXONE_PROFILE_L0") == std::string::npos);
  assert(server.find("webserver.on(\"/api/wifi/scan\", HTTP_GET") !=
         std::string::npos);
  assert(server.find("first_boot_setup_html") != std::string::npos);
  assert(server.find("kWifiScanTimeoutMs") != std::string::npos);
  assert(server.find("kWifiScanResultLimit") != std::string::npos);
  assert(display.find("first_boot::makeView") != std::string::npos);
  assert(display.find("firstBootView.product") != std::string::npos);
  assert(display.find("firstBootView.apSsid") != std::string::npos);
  assert(display.find("firstBootView.address") != std::string::npos);
  assert(main.find("securityRecoveryApOwnsRuntime()") != std::string::npos);
  assert(main.find("display.configureRuntime") != std::string::npos);
}
