#include "../profiles/profile.h"
#include "../src/core/first_boot_runtime_snapshot.h"
#include "../src/displays/display_backend_selection.h"
#include "../src/hardware/hardware_descriptor.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace {
using namespace voxone::display_backend;
using namespace voxone::display_profile;
using namespace voxone::first_boot;
using namespace voxone::hardware;

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string functionBody(const std::string& source, const char* signature,
                         const char* nextSignature) {
  const std::size_t begin = source.find(signature);
  const std::size_t end = source.find(nextSignature, begin + 1);
  assert(begin != std::string::npos && end != std::string::npos);
  return source.substr(begin, end - begin);
}

bool contains(const RuntimeSnapshot& snapshot, const char* value) {
  return std::string(snapshot.product).find(value) != std::string::npos ||
      std::string(snapshot.title).find(value) != std::string::npos ||
      std::string(snapshot.apSsid).find(value) != std::string::npos ||
      std::string(snapshot.apAddress).find(value) != std::string::npos ||
      std::string(snapshot.statusText).find(value) != std::string::npos;
}

void testSecurityRecoveryPublication() {
  RuntimeSnapshotStore runtime;
  runtime.initialize(1);
  RuntimeSnapshot snapshot = runtime.read();
  assert(snapshot.mode == Mode::Normal);
  assert(snapshot.screen == Screen::Player);
  assert(!snapshot.apActive);

  runtime.activateSecurityRecoveryAp(
      "VoxOne-Recovery-A1B2C3", "http://192.168.4.1/", true);
  snapshot = runtime.read();
  assert(snapshot.initialized);
  assert(snapshot.mode == Mode::SecurityRecovery);
  assert(snapshot.screen == Screen::SecurityRecovery);
  assert(snapshot.connection == Connection::RecoveryAp);
  assert(snapshot.apActive);
  assert(snapshot.apSecurity == ApSecurity::Open);
  assert(!snapshot.presentApCredential);
  assert(std::strcmp(snapshot.product, "VoxOne") == 0);
  assert(std::strcmp(snapshot.title, "SECURITY RECOVERY") == 0);
  assert(std::strcmp(snapshot.apSsid, "VoxOne-Recovery-A1B2C3") == 0);
  assert(std::strcmp(snapshot.apAddress, "http://192.168.4.1/") == 0);

  constexpr char secret[] = "never-publish-password-pin-cookie-token";
  assert(!contains(snapshot, secret));

  runtime.activateSecurityRecoveryAp(nullptr, nullptr, false);
  snapshot = runtime.read();
  assert(!snapshot.apActive);
  assert(snapshot.screen != Screen::SecurityRecovery);
}

void testUnsupportedDisplayDoesNotOwnRecovery() {
  const HardwareDescriptor& hardware = currentHardware();
  const RuntimeHardwareConfig defaults = defaultRuntimeHardwareConfig();

  RuntimeHardwareConfig none = defaults;
  none.display = RuntimeDisplay::None;
  const Selection noDisplay = select(none, hardware, compiledBackends());
  const ActivationPlan noDisplayPlan = activationPlan(noDisplay);
  assert(!noDisplayPlan.initializeBackend && !noDisplayPlan.runRenderer);

  RuntimeHardwareConfig unsupported = defaults;
#if defined(VOXONE_PROFILE_L0)
  unsupported.display = RuntimeDisplay::Ssd1309_128x64;
#else
  unsupported.display = RuntimeDisplay::St7796s_480x320;
#endif
  assert(validateRuntimeHardwareConfig(unsupported, hardware.capabilities) !=
         RuntimeHardwareValidation::Valid);
  const Selection safe = select(unsupported, hardware, compiledBackends());
  assert(safe.usedBuildDefault);
  DisplayKind safeKind = DisplayKind::None;
  assert(runtimeDisplayKind(safe.display, safeKind));
  assert(hardware.capabilities.supportsDisplay(safeKind));
  assert(safe.display == defaults.display);
}

void testProductionPathAndSecretBoundary() {
  const std::string main = readFile("src/main.cpp");
  const std::string runtime =
      readFile("src/core/security_recovery_ap_runtime.cpp");
  const std::string recoveryHttp =
      readFile("src/core/security_recovery_http.cpp");
  const std::string display = readFile("src/core/display.cpp");
  const std::string key =
      readFile("src/core/security_recovery_key_runtime.cpp");
  const std::string firstBoot = readFile("src/core/first_boot_core.h");

  const std::size_t setup = main.find("void setup()");
  const std::size_t loop = main.find("void loop()", setup);
  assert(setup != std::string::npos && loop != std::string::npos);
  const std::string setupBody = main.substr(setup, loop - setup);
  const std::size_t keySample = setupBody.find("detectBootKeyAtStartup()");
  const std::size_t apStart = setupBody.find("beginSecurityRecoveryAp(millis())");
  const std::size_t displayStart = setupBody.find("startSecurityRecoveryDisplay()");
  assert(keySample < apStart && apStart < displayStart);

  const std::size_t exclusive = setupBody.find(
      "if (voxone::security_recovery::securityRecoveryApOwnsRuntime())");
  const std::size_t exclusiveReturn = setupBody.find("return;", exclusive);
  assert(exclusive != std::string::npos &&
         exclusiveReturn != std::string::npos);
  const std::string exclusiveBranch =
      setupBody.substr(exclusive, exclusiveReturn - exclusive);
  assert(exclusiveBranch.find("RecoveryApStartResult::Active") !=
         std::string::npos);
  assert(exclusiveBranch.find("player.init") == std::string::npos);
  assert(exclusiveBranch.find("network.begin") == std::string::npos);
  assert(exclusiveBranch.find("btRuntime.start") == std::string::npos);
  assert(exclusiveBranch.find("config.init") == std::string::npos);

  assert(runtime.find("network.publishSecurityRecoveryAp(") !=
         std::string::npos);
  assert(runtime.find("result == RecoveryApStartResult::Active") !=
         std::string::npos);
  assert(runtime.find("WiFi.softAP(ssid_)") != std::string::npos);
  assert(runtime.find("credentialConfigured") == std::string::npos);
  assert(runtime.find("Recovery PIN") == std::string::npos);
  assert(runtime.find("voxone_recovery=") == std::string::npos);
  assert(runtime.find("Referrer-Policy: same-origin") != std::string::npos);
  assert(runtime.find("Referrer-Policy: no-referrer") == std::string::npos);
  assert(runtime.find("renderRecoveryRootPage(") != std::string::npos);
  assert(recoveryHttp.find("/password") != std::string::npos);
  assert(recoveryHttp.find("/factory-reset") != std::string::npos);
  assert(main.find("display.serviceSecurityRecovery()") != std::string::npos);

  const std::string displayInit = functionBody(
      display, "bool Display::initSecurityRecovery()",
      "void Display::serviceSecurityRecovery()");
  assert(displayInit.find("_createDspTask") == std::string::npos);
  assert(displayInit.find("player") == std::string::npos);
  assert(displayInit.find("netserver") == std::string::npos);
  assert(displayInit.find("config.init") == std::string::npos);

  const std::size_t securityBranch = display.find("if (securityRecovery)");
  const std::size_t securityReturn = display.find("return;", securityBranch);
  assert(securityBranch != std::string::npos &&
         securityReturn != std::string::npos);
  const std::string renderer =
      display.substr(securityBranch, securityReturn - securityBranch);
  assert(display.find("securityRecovery ? presentation.product") !=
         std::string::npos);
  assert(renderer.find("presentation.title") != std::string::npos);
  assert(renderer.find("presentation.apSsid") != std::string::npos);
  assert(renderer.find("presentation.apAddress") != std::string::npos);
  assert(renderer.find("kOledLineCharacters") != std::string::npos);
  assert(renderer.find("apPassConf") != std::string::npos);
  assert(renderer.find("apPassword") == std::string::npos);
  assert(renderer.find("PIN") == std::string::npos);
  assert(renderer.find("cookie") == std::string::npos);
  assert(renderer.find("token") == std::string::npos);

  assert(key.find("kDefaultKeyTimings") == std::string::npos);
  assert(key.find("delay(1)") != std::string::npos);
  assert(firstBoot.find("case Mode::SecurityRecovery: return Screen::SecurityRecovery") !=
         std::string::npos);
}
}  // namespace

int main() {
#if !(defined(VOXONE_PROFILE_W0) || defined(VOXONE_PROFILE_M0) || \
      defined(VOXONE_PROFILE_L0) || defined(VOXONE_PROFILE_S0) || \
      defined(VOXONE_PROFILE_DIN0))
#error "SECURITY-RECOVERY-DISPLAY native test requires a VoxOne profile"
#endif
  testSecurityRecoveryPublication();
  testUnsupportedDisplayDoesNotOwnRecovery();
  testProductionPathAndSecretBoundary();
}
