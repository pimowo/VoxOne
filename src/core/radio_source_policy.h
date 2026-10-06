#ifndef VOXONE_RADIO_SOURCE_POLICY_H
#define VOXONE_RADIO_SOURCE_POLICY_H

#include <stdint.h>

enum class RadioStopReason : uint8_t { Normal, SourceSwitch };

inline bool radioStopUpdatesSmartStart(RadioStopReason reason, bool lockOutput) {
  return reason == RadioStopReason::Normal && !lockOutput;
}

inline bool radioPlayPreparationUpdatesSmartStart(bool sourceResume,
                                                  uint8_t smartstart) {
  return !sourceResume && smartstart != 2;
}

inline bool radioWifiReconnectShouldPlay(bool lostPlaying,
                                         bool radioSelected) {
  return lostPlaying && radioSelected;
}

#endif
