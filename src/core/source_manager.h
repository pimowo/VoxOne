#ifndef VOXONE_SOURCE_MANAGER_H
#define VOXONE_SOURCE_MANAGER_H

#include "options.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
#include "bt_transport.h"
#include "bt_link_protocol.h"

struct SourceWebSnapshot {
  DisplaySourceKind kind = DisplaySourceKind::Radio;
  bool connected = false;
  DisplayPlaybackState playback = DisplayPlaybackState::Stopped;
  uint32_t sampleRate = 0;
  char peerName[sizeof(BtLinkState::peerName)]{};
  char artist[sizeof(BtLinkState::artist)]{};
  char title[sizeof(BtLinkState::title)]{};
};

void sourceManagerBegin();
void sourceManagerLoop();
void sourceManagerStopForUpdate();
void cycleNextSource();
bool bluetoothSourceSelected();
bool bluetoothAudioOutputAllowed();
bool bluetoothPhysicallyConnected();
bool radioI2SOutputEnabled();
#if VOXONE_BT_I2S_RX_ENABLED
uint16_t sourceManagerGetVuLevel(uint16_t dimension, bool& playing);
#endif
bool bluetoothTransportAvailable();
bool sourceManagerStepBluetoothVolume(int8_t delta);
void sourceManagerTransport(BtTransportInput input);
void sourceManagerWebSnapshot(SourceWebSnapshot& snapshot);
#endif

#endif
