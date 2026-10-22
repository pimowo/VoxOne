#include "security_recovery_key_runtime.h"

#include "Arduino.h"
#include "../hardware/hardware_descriptor.h"

namespace voxone {
namespace security_recovery {
namespace {
bool runtimeKeySuppressedUntilRelease = false;
}

KeyState detectBootKeyAtStartup() {
  const auto keyPin = hardware::currentHardware().localInput.key;
  pinMode(keyPin, INPUT);

  BootKeyDetector detector;
  const uint32_t startedMs = millis();
  detector.begin(digitalRead(keyPin) == LOW, startedMs);
  while (!detector.finished()) {
    delay(1);  // Yield during the short contact-debounce window.
    detector.update(digitalRead(keyPin) == LOW, millis());
  }

  return detector.state();
}

void suppressRuntimeKeyUntilRelease() {
  runtimeKeySuppressedUntilRelease = true;
}

bool runtimeKeyInputReady() {
  if (!runtimeKeySuppressedUntilRelease) return true;
  const auto keyPin = hardware::currentHardware().localInput.key;
  if (digitalRead(keyPin) == LOW) return false;
  runtimeKeySuppressedUntilRelease = false;
  return true;
}

}  // namespace security_recovery
}  // namespace voxone
