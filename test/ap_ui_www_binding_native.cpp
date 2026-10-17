#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

#include "../src/core/ap_ui_www_binding.h"

namespace {
using voxone::first_boot::ApSecurity;
using voxone::first_boot::Connection;
using voxone::first_boot::Mode;
using voxone::first_boot::RuntimeSnapshot;
using voxone::first_boot::RuntimeSnapshotStore;
using voxone::first_boot::Screen;
using voxone::first_boot::WebPage;

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string functionSource(const std::string& source, const char* beginToken,
                           const char* endToken) {
  const std::size_t begin = source.find(beginToken);
  const std::size_t end = source.find(endToken, begin);
  assert(begin != std::string::npos && end != std::string::npos);
  return source.substr(begin, end - begin);
}

std::size_t count(const std::string& text, const std::string& value) {
  std::size_t result = 0;
  for (std::size_t position = text.find(value);
       position != std::string::npos;
       position = text.find(value, position + value.size()))
    ++result;
  return result;
}

void assertUnavailable(const RuntimeSnapshot& snapshot) {
  const auto binding = voxone::first_boot::webBindingFor(snapshot);
  assert(binding.page == WebPage::Unavailable);
  assert(!binding.wifiProvisioning && !binding.normalPanel);
}

void testRuntimeMapping() {
  RuntimeSnapshotStore runtime;
  RuntimeSnapshot snapshot = runtime.read();
  assertUnavailable(snapshot);

  runtime.initialize(0);
  assertUnavailable(runtime.read());
  runtime.activateAp("VoxOne-SETUP1", "192.168.4.1", true);
  snapshot = runtime.read();
  auto binding = voxone::first_boot::webBindingFor(snapshot);
  assert(binding.page == WebPage::FirstBootSetup);
  assert(binding.wifiProvisioning && !binding.normalPanel);

  runtime.initialize(1);
  runtime.beginStaAttempt();
  snapshot = runtime.read();
  binding = voxone::first_boot::webBindingFor(snapshot);
  assert(binding.page == WebPage::WifiTransition);
  assert(!binding.wifiProvisioning && !binding.normalPanel);

  runtime.recordDisconnect(15);
  runtime.activateAp("VoxOne-RECOV1", "192.168.4.1", true);
  snapshot = runtime.read();
  binding = voxone::first_boot::webBindingFor(snapshot);
  assert(binding.page == WebPage::WifiRecovery);
  assert(binding.wifiProvisioning && !binding.normalPanel);
  assert(snapshot.errorCode == 15);

  runtime.beginStaAttempt();
  binding = voxone::first_boot::webBindingFor(runtime.read());
  assert(binding.page == WebPage::WifiTransition);
  runtime.connected();
  binding = voxone::first_boot::webBindingFor(runtime.read());
  assert(binding.page == WebPage::NormalPanel);
  assert(!binding.wifiProvisioning && binding.normalPanel);

  // A real runtime reconnect is different from initial association because
  // this exact store has already accepted a validated GOT_IP.
  runtime.recordDisconnect(15);
  binding = voxone::first_boot::webBindingFor(runtime.read());
  assert(binding.page == WebPage::NormalPanel);
  assert(!binding.wifiProvisioning && binding.normalPanel);
  runtime.beginStaAttempt();
  binding = voxone::first_boot::webBindingFor(runtime.read());
  assert(binding.page == WebPage::NormalPanel);
  runtime.connected();
  binding = voxone::first_boot::webBindingFor(runtime.read());
  assert(binding.page == WebPage::NormalPanel);

  // Further reconnects do not regress to FIRST BOOT or provisioning access.
  runtime.recordDisconnect(15);
  runtime.beginStaAttempt();
  binding = voxone::first_boot::webBindingFor(runtime.read());
  assert(binding.page == WebPage::NormalPanel);
  assert(!binding.wifiProvisioning && binding.normalPanel);

  // An actually activated Recovery AP remains authoritative even after a
  // successful STA connection earlier in the same boot.
  runtime.activateAp("VoxOne-RECOV1", "192.168.4.1", true);
  binding = voxone::first_boot::webBindingFor(runtime.read());
  assert(binding.page == WebPage::WifiRecovery);
  assert(binding.wifiProvisioning && !binding.normalPanel);

  // Reinitialization represents a new Network lifecycle, so a cold start
  // cannot inherit normal-panel eligibility from the preceding one.
  runtime.initialize(1);
  runtime.beginStaAttempt();
  binding = voxone::first_boot::webBindingFor(runtime.read());
  assert(binding.page == WebPage::WifiTransition);
  assert(!binding.wifiProvisioning && !binding.normalPanel);
}

void testFailClosedSnapshots() {
  RuntimeSnapshotStore runtime;
  runtime.initialize(0);
  runtime.activateAp("VoxOne-SETUP1", "192.168.4.1", true);
  RuntimeSnapshot snapshot = runtime.read();

  snapshot.apAddress[0] = '\0';
  assertUnavailable(snapshot);

  snapshot = runtime.read();
  snapshot.apSecurity = ApSecurity::Protected;
  assertUnavailable(snapshot);

  snapshot = runtime.read();
  snapshot.mode = Mode::Normal;
  snapshot.screen = Screen::Player;
  snapshot.connection = Connection::Connected;
  assertUnavailable(snapshot);

  snapshot = runtime.read();
  snapshot.generation = 0;
  assertUnavailable(snapshot);

  snapshot = RuntimeSnapshot{};
  snapshot.initialized = true;
  snapshot.generation = 1;
  snapshot.mode = Mode::WifiConfigured;
  snapshot.screen = Screen::WifiProgress;
  snapshot.connection = Connection::Connected;
  snapshot.staValidatedInThisBoot = true;
  auto binding = voxone::first_boot::webBindingFor(snapshot);
  assert(binding.page == WebPage::NormalPanel && binding.normalPanel);

  snapshot.connection = Connection::Idle;
  assertUnavailable(snapshot);

  snapshot.mode = Mode::Normal;
  snapshot.screen = Screen::Player;
  snapshot.connection = Connection::Connected;
  snapshot.staValidatedInThisBoot = false;
  assertUnavailable(snapshot);

  snapshot.mode = Mode::SecurityRecovery;
  binding = voxone::first_boot::webBindingFor(snapshot);
  assert(binding.page == WebPage::SecurityRecoveryExclusive);
  assert(!binding.wifiProvisioning && !binding.normalPanel);
}

void testNoSecretProjection() {
  const auto binding = voxone::first_boot::webBindingFor(RuntimeSnapshot{});
  (void)binding;
  const std::string header = readFile("src/core/ap_ui_www_binding.h");
  assert(header.find("char password") == std::string::npos);
  assert(header.find("char pin") == std::string::npos);
  assert(header.find("char token") == std::string::npos);
  assert(header.find("const char*") == std::string::npos);
}

void testProductionBinding() {
  const std::string server = readFile("src/core/netserver.cpp");
  const std::string index = functionSource(
      server, "void handleIndex(AsyncWebServerRequest * request) {",
      "namespace {\nconstexpr uint32_t kWifiScanTimeoutMs");
  const std::string authorization = functionSource(
      server,
      "voxone::security::SecuritySessionBinding* websocketBinding) {",
      "void securityApiResultReply(");
  const std::string variables = functionSource(
      server, "if (request->url() == \"/variables.js\") {",
      "Serial.print(\"Not Found: \")");

  assert(count(index, "network.presentationSnapshot()") == 1);
  assert(index.find("config.ssidsCount") == std::string::npos);
  assert(index.find("network.status") == std::string::npos);
  assert(index.find("webBinding.wifiProvisioning") != std::string::npos);
  assert(index.find("WebPage::FirstBootSetup") != std::string::npos);
  assert(index.find("first_boot_setup_html") != std::string::npos);
  assert(index.find("emptyfs_html") != std::string::npos);
  assert(index.find("webBinding.normalPanel") != std::string::npos);
  assert(index.find("config.saveWifiCredentials") != std::string::npos);
  assert(index.find("completeWifiProvisioningOpen()") != std::string::npos);
  assert(index.find("scheduleSystemRestart(\"SYSTEM\")") !=
         std::string::npos);
  assert(index.find("Cache-Control") != std::string::npos);
  assert(index.find("Retry-After") != std::string::npos);

  assert(count(authorization, "network.presentationSnapshot()") == 1);
  assert(authorization.find("webBinding.wifiProvisioning") !=
         std::string::npos);
  assert(authorization.find("apWifiRecoveryAllowed") == std::string::npos);
  assert(authorization.find("network.status") == std::string::npos);

  assert(count(variables, "network.presentationSnapshot()") == 1);
  assert(variables.find("webBinding.normalPanel") != std::string::npos);
  assert(variables.find("network.status") == std::string::npos);

  // The generic page selector neither logs nor serializes snapshot fields.
  assert(index.find("presentation.apSsid") == std::string::npos);
  assert(index.find("presentation.apAddress") == std::string::npos);
  assert(index.find("apPassword") == std::string::npos);
  assert(index.find("Serial.printf") == std::string::npos);
}
}  // namespace

int main() {
#if !(defined(VOXONE_PROFILE_W0) || defined(VOXONE_PROFILE_M0) || \
      defined(VOXONE_PROFILE_L0) || defined(VOXONE_PROFILE_S0) || \
      defined(VOXONE_PROFILE_DIN0))
#error "AP-UI-WWW-BINDING native test requires a VoxOne profile"
#endif
  testRuntimeMapping();
  testFailClosedSnapshots();
  testNoSecretProjection();
  testProductionBinding();
}
