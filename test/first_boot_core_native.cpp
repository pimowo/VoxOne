#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

#include "../src/core/first_boot_core.h"

namespace {
using voxone::first_boot::Connection;
using voxone::first_boot::Inputs;
using voxone::first_boot::Mode;
using voxone::first_boot::ApSecurity;
using voxone::first_boot::Screen;

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

bool contains(const char* text, const char* value) {
  return text && value && std::string(text).find(value) != std::string::npos;
}

bool viewContains(const voxone::first_boot::View& view, const char* value) {
  return contains(view.product, value) || contains(view.title, value) ||
      contains(view.apSsid, value) || contains(view.address, value) ||
      contains(view.statusText, value);
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
  assert(view.screen == Screen::WifiSetup);
  assert(view.apActive && view.apSecurity == ApSecurity::Open);
  assert(!view.presentApCredential && !view.hasError);
  assert(view.setupApOpen && view.wifiScanAvailable);
  assert(std::string(view.product) == "VoxOne");
  assert(std::string(view.title) == "KONFIGURACJA");
  assert(std::string(view.apSsid) == "VoxOne-A1B2C3");
  assert(std::string(view.address) == "http://192.168.4.1/");

  auto connecting = voxone::first_boot::makeView(
      input(1, Connection::Connecting));
  assert(connecting.mode == Mode::WifiConnecting);
  assert(connecting.screen == Screen::WifiProgress);
  assert(!connecting.apActive &&
         connecting.apSecurity == ApSecurity::NotActive);
  assert(std::string(connecting.apSsid).empty());
  assert(std::string(connecting.address).empty());

  // No saved profile is always first boot, even for contradictory transient
  // connection input. CORE must not invent a configured STA in this case.
  assert(voxone::first_boot::resolveMode(
             input(0, Connection::Connecting)) == Mode::FirstBootSetup);

  Inputs configured = input(1, Connection::Connected);
  configured.wifiConfigurationConfirmed = true;
  auto configuredView = voxone::first_boot::makeView(configured);
  assert(configuredView.mode == Mode::WifiConfigured);
  assert(configuredView.screen == Screen::WifiProgress);
  assert(!configuredView.apActive && !configuredView.presentApCredential);

  auto normal = voxone::first_boot::makeView(
      input(1, Connection::Connected));
  assert(normal.mode == Mode::Normal);
  assert(normal.screen == Screen::Player);
  assert(std::string(normal.title) == "VoxOne");
  assert(!normal.apActive && !normal.hasError);

  // Persistence or HTTP success alone cannot claim WIFI_CONFIGURED.
  Inputs unconfirmed = input(1, Connection::Idle);
  unconfirmed.wifiConfigurationConfirmed = true;
  assert(voxone::first_boot::resolveMode(unconfirmed) == Mode::Normal);

  Inputs wpaFailure = input(1, Connection::Failed);
  wpaFailure.errorCode = 15;
  auto failed = voxone::first_boot::makeView(wpaFailure);
  assert(failed.mode == Mode::WifiRecovery);
  assert(failed.screen == Screen::WifiRecovery);
  assert(failed.hasError && failed.errorCode == 15);
  assert(!failed.apActive && failed.apSecurity == ApSecurity::NotActive);
  assert(std::string(failed.apSsid).empty());
  assert(std::string(failed.address).empty());

  Inputs fallbackInput = input(1, Connection::RecoveryAp);
  fallbackInput.errorCode = 15;
  auto fallback = voxone::first_boot::makeView(fallbackInput);
  assert(fallback.mode == Mode::WifiRecovery);
  assert(fallback.apActive &&
         fallback.apSecurity == ApSecurity::Protected);
  assert(fallback.presentApCredential && !fallback.setupApOpen);
  assert(!fallback.wifiScanAvailable && fallback.hasError);
  assert(std::string(fallback.title) == "KONFIGURACJA WI-FI");

  Inputs security = input(1, Connection::RecoveryAp);
  security.securityRecovery = true;
  auto securityView = voxone::first_boot::makeView(security);
  assert(securityView.mode == Mode::SecurityRecovery);
  assert(securityView.screen == Screen::SecurityRecovery);
  assert(securityView.apActive &&
         securityView.apSecurity == ApSecurity::Protected);
  assert(!securityView.presentApCredential);
  assert(!securityView.wifiScanAvailable);
  Inputs securityWithoutProfiles = input(0, Connection::RecoveryAp);
  securityWithoutProfiles.securityRecovery = true;
  assert(voxone::first_boot::resolveMode(securityWithoutProfiles) ==
         Mode::SecurityRecovery);

  // The presentation contract carries no credential value. A secret that is
  // not an input cannot appear in any public semantic field.
  constexpr char secret[] = "never-publish-this-secret";
  assert(!viewContains(view, secret));
  assert(!viewContains(connecting, secret));
  assert(!viewContains(configuredView, secret));
  assert(!viewContains(normal, secret));
  assert(!viewContains(fallback, secret));
  assert(!viewContains(securityView, secret));

  assert(voxone::first_boot::shouldStartOpenSetupAp(0));
  assert(!voxone::first_boot::shouldStartOpenSetupAp(1));

  const std::string network = readFile("src/core/network.cpp");
  const std::string server = readFile("src/core/netserver.cpp");
  const std::string transaction =
      readFile("src/core/first_boot_transaction_runtime.cpp");
  const std::string serial = readFile("src/core/serialcli.cpp");
  const std::string display = readFile("src/core/display.cpp");
  const std::string main = readFile("src/main.cpp");
  assert(network.find("VOXONE_PROFILE_L0") == std::string::npos);
  assert(network.find("Connect to protected AP %s") != std::string::npos);
  assert(network.find("with password %s") == std::string::npos);
  assert(server.find("webserver.on(\"/api/wifi/scan\", HTTP_GET") !=
         std::string::npos);
  assert(server.find("first_boot_setup_html") != std::string::npos);
  assert(server.find("firstBootTemplateValue") != std::string::npos);
  assert(server.find("makeRuntimeHardwareWebCandidate") != std::string::npos);
  assert(server.find("Missing hardware selection") != std::string::npos);
  assert(server.find("Unsupported hardware selection") != std::string::npos);
  assert(server.find("commitDeviceFirstBootTransaction") !=
         std::string::npos);
  assert(server.find("Owner::HardwareApi") != std::string::npos);
  assert(server.find("Owner::ConfigRestore") != std::string::npos);
  assert(server.find("Owner::FirstBoot") != std::string::npos);
  assert(server.find("Owner::WifiRecovery") != std::string::npos);
  assert(serial.find("Owner::SerialHardware") != std::string::npos);
  assert(serial.find("Owner::SerialWifi") != std::string::npos);
  const std::size_t restoreLease = server.find("Owner::ConfigRestore");
  const std::size_t restoreSecurity =
      server.find("SecurityRuntimeLock securityLock", restoreLease);
  const std::size_t restoreQueueMutex =
      server.find("xSemaphoreTake(configurationMutationMutex", restoreSecurity);
  assert(restoreLease < restoreSecurity);
  assert(restoreSecurity < restoreQueueMutex);
  const std::size_t firstBootLease = server.find("Owner::FirstBoot");
  const std::size_t firstBootSecurity =
      server.find("SecurityRuntimeLock securityLock", firstBootLease);
  assert(firstBootLease < firstBootSecurity);
  assert(transaction.find("eraseRuntimeHardwarePersistent") !=
         std::string::npos);
  assert(server.find("kWifiScanTimeoutMs") != std::string::npos);
  assert(server.find("kWifiScanResultLimit") != std::string::npos);
  assert(display.find("network.presentationSnapshot()") != std::string::npos);
  assert(display.find("first_boot::makeView") == std::string::npos);
  assert(main.find("securityRecoveryApOwnsRuntime()") != std::string::npos);
  assert(main.find("display.configureRuntime") != std::string::npos);
  const std::size_t setup = main.find("void setup()");
  const std::size_t transactionRecovery =
      main.find("recoverDeviceFirstBootTransactionAtBoot()", setup);
  const std::size_t recoveryAp =
      main.find("beginSecurityRecoveryAp(millis())", setup);
  const std::size_t runtimeHardware =
      main.find("hardware::loadRuntimeHardwareConfig()", setup);
  assert(setup != std::string::npos);
  assert(transactionRecovery != std::string::npos);
  assert(transactionRecovery < recoveryAp);
  assert(transactionRecovery < runtimeHardware);
  assert(main.find("USB restore required") != std::string::npos);
}
