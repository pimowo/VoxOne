#include "../src/core/security_recovery_key.h"
#include "../src/hardware/hardware_descriptor.h"
#include "../src/hardware/runtime_hardware_config.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

using voxone::security_recovery::BootKeyDetector;
using voxone::security_recovery::KeyState;

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void advanceHeld(BootKeyDetector& detector, uint32_t& now, uint32_t amount) {
  now += amount;
  detector.update(true, now);
}
}  // namespace

int main() {
  // HIGH at the one permitted initial sample exits immediately: normal boot
  // does not wait for debounce or hold time.
  BootKeyDetector high;
  assert(high.begin(false, 10) == KeyState::Cancelled);
  assert(high.finished());
  assert(high.update(true, 20000) == KeyState::Cancelled);

  BootKeyDetector accepted;
  uint32_t now = 100;
  assert(accepted.begin(true, now) == KeyState::Debounce);
  advanceHeld(accepted, now, 74);
  assert(accepted.state() == KeyState::Debounce);
  advanceHeld(accepted, now, 1);
  assert(accepted.state() == KeyState::Hold);
  advanceHeld(accepted, now, 2999);
  assert(accepted.state() == KeyState::Hold);
  advanceHeld(accepted, now, 1);
  assert(accepted.state() == KeyState::AwaitRelease);
  assert(accepted.update(false, now + 1) == KeyState::Accepted);

  BootKeyDetector earlyRelease;
  now = 0;
  earlyRelease.begin(true, now);
  advanceHeld(earlyRelease, now, 75);
  advanceHeld(earlyRelease, now, 2999);
  assert(earlyRelease.update(false, now + 1) == KeyState::Cancelled);

  BootKeyDetector bounce;
  bounce.begin(true, 0);
  assert(bounce.update(false, 20) == KeyState::Cancelled);
  assert(bounce.update(true, 80) == KeyState::Cancelled);

  BootKeyDetector holdBounce;
  holdBounce.begin(true, 0);
  assert(holdBounce.update(true, 75) == KeyState::Hold);
  assert(holdBounce.update(false, 100) == KeyState::Cancelled);

  BootKeyDetector stuck;
  now = 0;
  stuck.begin(true, now);
  advanceHeld(stuck, now, 75);
  advanceHeld(stuck, now, 3000);
  advanceHeld(stuck, now, 9999);
  assert(stuck.state() == KeyState::AwaitRelease);
  advanceHeld(stuck, now, 1);
  assert(stuck.state() == KeyState::StuckLow);
  assert(stuck.update(false, now + 1) == KeyState::StuckLow);

  // A new press after the startup window cannot restart a terminal detector.
  BootKeyDetector oneShot;
  oneShot.begin(false, 500);
  assert(oneShot.update(true, 600) == KeyState::Cancelled);

  // Unsigned subtraction keeps every deadline correct across millis() wrap.
  BootKeyDetector wrap;
  now = UINT32_MAX - 40U;
  wrap.begin(true, now);
  advanceHeld(wrap, now, 75);
  assert(wrap.state() == KeyState::Hold);
  advanceHeld(wrap, now, 3000);
  assert(wrap.state() == KeyState::AwaitRelease);
  assert(wrap.update(false, now + 1) == KeyState::Accepted);

  // Compile-time hardware identity, not RuntimeInput, owns the boot KEY.
  const auto& hardware = voxone::hardware::currentHardware();
#if defined(VOXONE_PROFILE_W0)
  assert(hardware.localInput.key == 32);
#elif defined(VOXONE_PROFILE_L0)
  assert(hardware.localInput.key == 39);
#else
  assert(hardware.localInput.key == 4);
#endif
  auto runtime = voxone::hardware::defaultRuntimeHardwareConfig(hardware);
  runtime.input = voxone::hardware::RuntimeInput::None;
  assert(runtime.input == voxone::hardware::RuntimeInput::None);
  assert(hardware.localInput.key >= 0);

  const std::string runtimeSource =
      readFile("src/core/security_recovery_key_runtime.cpp");
  assert(runtimeSource.find("currentHardware().localInput.key") !=
         std::string::npos);
  assert(runtimeSource.find("pinMode(keyPin, INPUT)") != std::string::npos);
  assert(runtimeSource.find("INPUT_PULLUP") == std::string::npos);
  assert(runtimeSource.find("delay(1)") != std::string::npos);
  assert(runtimeSource.find("OneButton") == std::string::npos);
  assert(runtimeSource.find("localInput.a") == std::string::npos);
  assert(runtimeSource.find("localInput.b") == std::string::npos);

  const std::string controlsSource = readFile("src/core/controls.cpp");
  assert(controlsSource.find("encoder1Loop();") != std::string::npos);
  assert(controlsSource.find("localInputButtons[0].tick()") !=
         std::string::npos);
  assert(controlsSource.find("localInputButtons[1].tick()") !=
         std::string::npos);
  assert(controlsSource.find("runtimeKeyInputReady()") != std::string::npos);

  const std::string mainSource = readFile("src/main.cpp");
  const size_t health = mainSource.find("beginBootHealthMonitor()");
  const size_t detection = mainSource.find("detectBootKeyGesture()");
  const size_t restore = mainSource.find("recoverAtBoot()");
  assert(health < detection && detection < restore);

  const std::string detectorSource =
      readFile("src/core/security_recovery_key.cpp");
  const std::string combined = detectorSource + runtimeSource;
  assert(combined.find("Preferences") == std::string::npos);
  assert(combined.find("nvs_") == std::string::npos);
  assert(combined.find("WiFi") == std::string::npos);
  assert(combined.find("softAP") == std::string::npos);
  assert(combined.find("SecuritySettings") == std::string::npos);
  assert(combined.find("ESP.restart") == std::string::npos);
}
