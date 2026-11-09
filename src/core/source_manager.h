#ifndef VOXONE_SOURCE_MANAGER_H
#define VOXONE_SOURCE_MANAGER_H

#include "options.h"
#include "display.h"
#include "source_catalog.h"
#include <stdint.h>

// Radio intent also exists on targets without Bluetooth (temporary URL restore).
void sourceManagerRadioCommandQueued(bool play, uint16_t station);
void sourceManagerRadioPlayConsumed();
void sourceManagerRadioStopConsumed();
void sourceManagerPlaylistSelectionCleared();
bool sourceManagerRadioResumeAllowed();
bool sourceManagerRadioPlayIntent();
void sourceManagerBeginHomeAssistant(const char* title, const char* artist);
void sourceManagerEndHomeAssistant();
bool sourceManagerHomeAssistantActive();
void sourceManagerBeginDlna(const char* title, const char* artist);
void sourceManagerEndTemporary();
bool sourceManagerDlnaActive();
class TemporaryAudioState;
enum class ActiveSource : uint8_t;
bool sourceManagerSelectSource(ActiveSource source);
bool sourceManagerSelectSource(SourceId source);
bool sourceManagerSourceAvailable(SourceId source);
uint8_t sourceManagerSourceMask();
SourceId sourceManagerActiveSource();
bool sourceManagerTakeTemporaryRestore(TemporaryAudioState& temporary,
                                      bool networkReady, bool blocked,
                                      uint16_t& station);

struct SourceWebSnapshot {
  DisplaySourceKind kind = DisplaySourceKind::Radio;
  bool connected = false;
  DisplayPlaybackState playback = DisplayPlaybackState::Stopped;
  uint32_t sampleRate = 0;
  char peerName[97]{};
  char artist[193]{};
  char title[193]{};
};

void sourceManagerWebSnapshot(SourceWebSnapshot& snapshot);

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
#include "bt_transport.h"
#include "bt_link_protocol.h"

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
bool sourceManagerSetBluetoothVolume(uint8_t user);
void sourceManagerTransport(BtTransportInput input);
#endif

#endif
