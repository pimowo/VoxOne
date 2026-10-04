#include "dac_mute.h"

#include "Arduino.h"
#include "../hardware/hardware_descriptor.h"

DacMuteController dacMute;

void DacMuteController::begin() {
  high_.store(false, std::memory_order_relaxed);
  const auto pin = voxone::hardware::currentHardware().dacXsmt;
  if (voxone::hardware::hasPin(pin)) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
}

void DacMuteController::update(DacPlaybackState playback,
                               bool updateAudioBlocked) {
  const bool nextHigh = dacXsmtHigh(playback, updateAudioBlocked);
  if (nextHigh == high_.load(std::memory_order_relaxed)) return;
  const auto pin = voxone::hardware::currentHardware().dacXsmt;
  if (voxone::hardware::hasPin(pin))
    digitalWrite(pin, nextHigh ? HIGH : LOW);
  high_.store(nextHigh, std::memory_order_relaxed);
}
