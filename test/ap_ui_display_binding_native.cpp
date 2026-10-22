#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

#include "../src/core/first_boot_runtime_snapshot.h"

namespace {
using voxone::first_boot::ApSecurity;
using voxone::first_boot::Connection;
using voxone::first_boot::Mode;
using voxone::first_boot::RuntimeSnapshot;
using voxone::first_boot::RuntimeSnapshotStore;

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string apScreenSource(const std::string& display) {
  const std::size_t begin = display.find("void Display::_apScreen()");
  const std::size_t end = display.find("void Display::_start()", begin);
  assert(begin != std::string::npos && end != std::string::npos);
  return display.substr(begin, end - begin);
}

std::size_t count(const std::string& text, const std::string& value) {
  std::size_t result = 0;
  for (std::size_t position = text.find(value);
       position != std::string::npos;
       position = text.find(value, position + value.size())) {
    ++result;
  }
  return result;
}

void assertNoSecret(const RuntimeSnapshot& snapshot,
                    const std::string& secret) {
  assert(std::string(snapshot.apSsid).find(secret) == std::string::npos);
  assert(std::string(snapshot.apAddress).find(secret) == std::string::npos);
  assert(std::string(snapshot.title).find(secret) == std::string::npos);
  assert(std::string(snapshot.statusText).find(secret) == std::string::npos);
}

void testSnapshotTransitions() {
  RuntimeSnapshotStore runtime;
  RuntimeSnapshot snapshot = runtime.read();
  assert(!snapshot.initialized);
  assert(!snapshot.apActive);
  assert(snapshot.apSsid[0] == '\0' && snapshot.apAddress[0] == '\0');

  runtime.initialize(0);
  runtime.activateAp("VoxOne-SETUP1", "192.168.4.1", true);
  snapshot = runtime.read();
  assert(snapshot.mode == Mode::FirstBootSetup);
  assert(snapshot.connection == Connection::SetupAp);
  assert(snapshot.apActive && snapshot.apSecurity == ApSecurity::Open);
  assert(!snapshot.presentApCredential);
  assert(std::string(snapshot.apSsid) == "VoxOne-SETUP1");
  assert(std::string(snapshot.apAddress) == "192.168.4.1");
  assert(std::string(snapshot.statusText) == "SIEĆ OTWARTA");

  runtime.initialize(1);
  runtime.beginStaAttempt();
  runtime.recordDisconnect(15);
  runtime.activateAp("VoxOne-RECOV1", "192.168.4.1", true);
  snapshot = runtime.read();
  assert(snapshot.mode == Mode::WifiRecovery);
  assert(snapshot.connection == Connection::RecoveryAp);
  assert(snapshot.errorCode == 15);
  assert(snapshot.apSecurity == ApSecurity::Protected);
  assert(snapshot.presentApCredential);
  assert(std::string(snapshot.title) == "KONFIGURACJA WI-FI");
  assert(std::string(snapshot.apSsid) == "VoxOne-RECOV1");
  assert(std::string(snapshot.apAddress) == "192.168.4.1");
  assertNoSecret(snapshot, "never-publish-this-password");

  runtime.beginStaAttempt();
  snapshot = runtime.read();
  assert(snapshot.mode == Mode::WifiConnecting);
  assert(!snapshot.apActive && !snapshot.presentApCredential);
  assert(snapshot.apSsid[0] == '\0' && snapshot.apAddress[0] == '\0');
  assert(snapshot.errorCode == 0);

  runtime.connected();
  snapshot = runtime.read();
  assert(snapshot.mode == Mode::Normal);
  assert(!snapshot.apActive && snapshot.apSsid[0] == '\0');
}

void testDisplayBindingContract() {
  const std::string display = readFile("src/core/display.cpp");
  const std::string apScreen = apScreenSource(display);
  const std::string main = readFile("src/main.cpp");
  const std::string core = readFile("src/core/first_boot_core.h");
  const std::string runtime =
      readFile("src/core/first_boot_runtime_snapshot.h");

  assert(count(apScreen, "network.presentationSnapshot()") == 1);
  assert(apScreen.find("config.ssidsCount") == std::string::npos);
  assert(apScreen.find("shouldStartOpenSetupAp") == std::string::npos);
  assert(apScreen.find("first_boot::makeView") == std::string::npos);
  assert(apScreen.find("WiFi.softAPIP()") == std::string::npos);
  assert(apScreen.find("presentation.initialized") != std::string::npos);
  assert(apScreen.find("presentation.apActive") != std::string::npos);
  assert(apScreen.find("presentation.mode") != std::string::npos);
  assert(apScreen.find("presentation.title") != std::string::npos);
  assert(apScreen.find("presentation.apSsid") != std::string::npos);
  assert(apScreen.find("presentation.apAddress") != std::string::npos);
  assert(apScreen.find("presentation.statusText") != std::string::npos);
  assert(apScreen.find("presentation.presentApCredential") !=
         std::string::npos);
  assert(apScreen.find("Serial.") == std::string::npos);
  assert(apScreen.find("BOOTLOG") == std::string::npos);

  // A local-only compile-time AP credential is used only behind the CORE
  // permission bit; it never becomes part of the runtime snapshot.
  const std::size_t credentialGuard =
      apScreen.find("if (presentation.presentApCredential)");
  const std::size_t localCredential =
      apScreen.find("recoveryPassword->setText(apPassword", credentialGuard);
  assert(credentialGuard != std::string::npos);
  assert(localCredential != std::string::npos);
  assert(runtime.find("char password") == std::string::npos);
  assert(runtime.find("char pin") == std::string::npos);
  assert(runtime.find("char token") == std::string::npos);

  assert(core.find("KONFIGURACJA WI-FI") != std::string::npos);
  assert(core.find("SIEĆ OTWARTA") != std::string::npos);

  const std::size_t displayInit = main.find("display.init();");
  const std::size_t networkBegin = main.find("network.begin();");
  const std::size_t displayStart = main.find("display.putRequest(DSP_START);");
  assert(displayInit < networkBegin && networkBegin < displayStart);
  const std::size_t securityExclusive =
      main.find("securityRecoveryApOwnsRuntime()");
  assert(securityExclusive != std::string::npos &&
         securityExclusive < displayInit);
}

void testProfileLayoutContract() {
  const std::string display = readFile("src/core/display.cpp");
  const std::string config =
      readFile("src/displays/conf/displayST7796conf.h");

#if defined(VOXONE_PROFILE_L0)
  assert(config.find("firstBootLogoTop 68") != std::string::npos);
  assert(config.find("firstBootSsidConf   PROGMEM = { 0, 176, 3, WA_CENTER }") !=
         std::string::npos);
  assert(config.find("firstBootStatusConf PROGMEM = { 0, 216, 2, WA_CENTER }") !=
         std::string::npos);
  assert(config.find("firstBootIpConf     PROGMEM = { 0, 294, 2, WA_CENTER }") !=
         std::string::npos);
  assert(config.find("wifiRecoverySsidConf     PROGMEM = { 0, 176, 3, WA_CENTER }") !=
         std::string::npos);
  assert(config.find("wifiRecoveryPasswordConf PROGMEM = { 0, 224, 2, WA_CENTER }") !=
         std::string::npos);
  assert(config.find("wifiRecoveryIpConf       PROGMEM = { 0, 294, 2, WA_CENTER }") !=
         std::string::npos);
  assert(display.find("dsp.drawFirstBootLogo(firstBootLogoTop)") !=
         std::string::npos);
#elif defined(VOXONE_PROFILE_W0) || defined(VOXONE_PROFILE_M0) || \
      defined(VOXONE_PROFILE_S0) || defined(VOXONE_PROFILE_DIN0)
  const std::string apScreen = apScreenSource(display);
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0) || \
    defined(VOXONE_PROFILE_DIN0)
  assert(apScreen.find("setup128x64::title") != std::string::npos);
  assert(apScreen.find("setup128x64::product") != std::string::npos);
  assert(apScreen.find("setup128x64::ssid") != std::string::npos);
  assert(apScreen.find("setup128x64::detailLabel") != std::string::npos);
  assert(apScreen.find("setup128x64::detailValue") != std::string::npos);
  assert(apScreen.find("setup128x64::footer") != std::string::npos);
  assert(apScreen.find("new ScrollWidget(\"*\", setup128x64::ssid") !=
         std::string::npos);
#else
  assert(apScreen.find("apNameConf") != std::string::npos);
  assert(apScreen.find("apName2Conf") != std::string::npos);
  assert(apScreen.find("apPassConf") != std::string::npos);
  assert(apScreen.find("apPass2Conf") != std::string::npos);
  assert(apScreen.find("apSettConf") != std::string::npos);
#endif
#else
#error "AP-UI-DISPLAY-BINDING native test requires a VoxOne profile"
#endif
}
}  // namespace

int main() {
  testSnapshotTransitions();
  testDisplayBindingContract();
  testProfileLayoutContract();
}
