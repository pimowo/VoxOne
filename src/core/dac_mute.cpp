#include "dac_mute.h"

#include "Arduino.h"
#include "options.h"

#ifndef VOXONE_DAC_XSMT_PIN
#define VOXONE_DAC_XSMT_PIN 255
#endif

DacMuteController dacMute;

void DacMuteController::begin() {
  high_.store(false, std::memory_order_relaxed);
#if VOXONE_DAC_XSMT_PIN != 255
  digitalWrite(VOXONE_DAC_XSMT_PIN, LOW);
  pinMode(VOXONE_DAC_XSMT_PIN, OUTPUT);
#endif
}

void DacMuteController::update(DacPlaybackState playback,
                               bool updateAudioBlocked) {
  const bool nextHigh = dacXsmtHigh(playback, updateAudioBlocked);
  if (nextHigh == high_.load(std::memory_order_relaxed)) return;
#if VOXONE_DAC_XSMT_PIN != 255
  digitalWrite(VOXONE_DAC_XSMT_PIN, nextHigh ? HIGH : LOW);
#endif
  high_.store(nextHigh, std::memory_order_relaxed);
}
