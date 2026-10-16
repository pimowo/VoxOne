#include "security_recovery_key_runtime.h"

#include "Arduino.h"
#include "../hardware/hardware_descriptor.h"

namespace voxone {
namespace security_recovery {
namespace {
bool suppressRuntimeKeyUntilRelease = false;
}

KeyState detectBootKeyGesture() {
  const auto keyPin = hardware::currentHardware().localInput.key;
  pinMode(keyPin, INPUT);

  BootKeyDetector detector;
  const uint32_t startedMs = millis();
  detector.begin(digitalRead(keyPin) == LOW, startedMs);
  while (!detector.finished()) {
    delay(1);  // Yield to the RTOS and watchdog while KEY is held.
    detector.update(digitalRead(keyPin) == LOW, millis());
  }

  suppressRuntimeKeyUntilRelease = detector.state() == KeyState::StuckLow;
  return detector.state();
}

bool runtimeKeyInputReady() {
  if (!suppressRuntimeKeyUntilRelease) return true;
  const auto keyPin = hardware::currentHardware().localInput.key;
  if (digitalRead(keyPin) == LOW) return false;
  suppressRuntimeKeyUntilRelease = false;
  return true;
}

}  // namespace security_recovery
}  // namespace voxone
