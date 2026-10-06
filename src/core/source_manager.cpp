#include "source_manager.h"
#include <Arduino.h>
#include "source_manager_state.h"
#include "temporary_audio_state.h"

namespace {
SourceManagerState sourceState;
portMUX_TYPE sourceMux = portMUX_INITIALIZER_UNLOCKED;
}

void sourceManagerRadioCommandQueued(bool play, uint16_t station) {
  portENTER_CRITICAL(&sourceMux);
  sourceState.recordRadioCommand(play, station);
  portEXIT_CRITICAL(&sourceMux);
}

void sourceManagerRadioPlayConsumed() {
  portENTER_CRITICAL(&sourceMux);
  sourceState.radioPlayConsumed();
  portEXIT_CRITICAL(&sourceMux);
}

void sourceManagerRadioStopConsumed() {
  portENTER_CRITICAL(&sourceMux);
  sourceState.radioStopConsumed();
  portEXIT_CRITICAL(&sourceMux);
}

bool sourceManagerRadioResumeAllowed() {
  portENTER_CRITICAL(&sourceMux);
  const bool allowed = sourceState.radioResumeAllowed();
  portEXIT_CRITICAL(&sourceMux);
  return allowed;
}

bool sourceManagerRadioPlayIntent() {
  portENTER_CRITICAL(&sourceMux);
  const bool play = sourceState.radioPlayIntent();
  portEXIT_CRITICAL(&sourceMux);
  return play;
}

bool sourceManagerTakeTemporaryRestore(TemporaryAudioState& temporary,
                                      bool networkReady, bool blocked,
                                      uint16_t& station) {
  portENTER_CRITICAL(&sourceMux);
  // Commit the restore decision together with the current base. A controls
  // task changing source cannot fall between reading intent and consuming it.
  const bool play = temporary.takeRadioRestore(sourceState.radioResumeAllowed(),
                                               networkReady, blocked);
  if (play) {
    station = sourceState.radioStationForResume(station);
    sourceState.radioPlayConsumed();
  }
  portEXIT_CRITICAL(&sourceMux);
  return play;
}

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE

#include "bt_link.h"
#include "bt_audio_input.h"
#include "bt_volume.h"
#include "config.h"
#include "display.h"
#include "network.h"
#include "netserver.h"
#include "player.h"
#include "serialcli.h"
#include "source_manager_state.h"
#include "source_vu_state.h"
#include "system_operation_state.h"

namespace {
BtVolumeSync volumeSync;

// Only the display task writes this copy. Widgets copy its text in setText().
struct DisplayText {
  char peerName[sizeof(BtLinkState::peerName)];
  char artist[sizeof(BtLinkState::artist)];
  char title[sizeof(BtLinkState::title)];
};
DisplayText displayText{};

const char* sourceName(ActiveSource source) {
  return source == ActiveSource::Bluetooth ? "BT" : "RADIO";
}

const char* sourceReason(SourceChangeReason reason) {
  switch (reason) {
    case SourceChangeReason::Manual: return "manual";
    case SourceChangeReason::BtConnect: return "bt-connect";
    case SourceChangeReason::BtDisconnect: return "bt-disconnect";
    case SourceChangeReason::BtOffline: return "bt-offline";
    default: return "unknown";
  }
}

void refreshDisplay(const SourceUpdate& update) {
#if VOXONE_HAS_DISPLAY
  if (update.activeChanged || update.audioInfoChanged)
    display.putRequest(DBITRATE);
  if (update.stationChanged) display.putRequest(NEWSTATION);
  if (update.titleChanged) display.putRequest(NEWTITLE);
  if (update.activeChanged || update.volumeChanged) display.putRequest(DRAWVOL);
#else
  (void)update;
#endif
}

void stopOnSourceChange(const SourceUpdate& update, bool temporaryAtChange) {
  if (update.activeChanged) {
    // Selection keeps following BT/user events; only physical work is deferred.
    if (temporaryAtChange || player.temporaryBusy()) {
      network.lostPlaying = false;
      return;
    }
    const bool radioActive = player.status() == PLAYING || player.isRunning();
    portENTER_CRITICAL(&sourceMux);
    const RadioSourceActions radio = sourceState.radioActions(update, radioActive);
    const uint16_t resumeStation =
        sourceState.radioStationForResume(config.lastStation());
    portEXIT_CRITICAL(&sourceMux);
    // Preserve the queued user intent, then replace stale physical work with
    // the ordered stop/suspend/resume actions for the newly selected source.
    network.lostPlaying = false;
    player.resetQueue();
    if (radio.userStop) player.sendCommand({PR_STOP, 0});
    if (radio.suspend) player.sendCommand({PR_RADIO_SUSPEND, 0});
    if (radio.resume)
      player.sendCommand({PR_RADIO_RESUME, resumeStation});
  }
}
}  // namespace

void sourceManagerBegin() {
  serialCli.printf("##[SOURCE]# active=RADIO\n");
}

void sourceManagerLoop() {
  if (systemUpdateAudioBlocked()) {
    sourceManagerStopForUpdate();
    return;
  }
  portENTER_CRITICAL(&sourceMux);
  const SourceUpdate update = sourceState.observe(btLink.state());
  const ActiveSource active = sourceState.active();
  const bool temporaryAtChange = player.temporaryBusy();
  portEXIT_CRITICAL(&sourceMux);
  stopOnSourceChange(update, temporaryAtChange);
  if (update.btDisconnected) volumeSync.disconnect();
  if (update.btConnected) {
    const uint8_t user = config.userVolume;
    if (btLink.setVolume(btUserToAbsolute(user)))
      volumeSync.connect(user, millis());
  }
  if (update.volumeCallback &&
      volumeSync.acceptPhoneVolume(update.absoluteVolume, millis())) {
    const uint8_t user = btAbsoluteToUser(update.absoluteVolume);
    if (user != config.userVolume) player.setUserVol(user);
  }
  if (volumeSync.needsUserCommand(config.userVolume)) {
    const uint8_t user = config.userVolume;
    if (btLink.setVolume(btUserToAbsolute(user)))
      volumeSync.sentUserCommand(user);
  }
  uint8_t retryAbsolute = 0;
  if (volumeSync.retryDue(millis(), retryAbsolute))
    btLink.setVolume(retryAbsolute);
  if (update.activeChanged)
    serialCli.printf("##[SOURCE]# active=%s reason=%s\n",
                     sourceName(active), sourceReason(update.reason));
#if VOXONE_HAS_DISPLAY
  if (update.activeChanged && active == ActiveSource::Radio &&
      display.mode() == BT_TRANSPORT)
    display.putRequest(NEWMODE, PLAYER);
#endif
  refreshDisplay(update);
  if (update.activeChanged || update.stationChanged || update.titleChanged ||
      update.audioInfoChanged)
    netserver.requestOnChange(WEBSTATUS, 0);
}

void sourceManagerStopForUpdate() {
  portENTER_CRITICAL(&sourceMux);
  sourceState.stopForUpdate(btLink.state());
  portEXIT_CRITICAL(&sourceMux);
  volumeSync.disconnect();
}

bool bluetoothSourceSelected() {
  portENTER_CRITICAL(&sourceMux);
  const bool selected = sourceState.active() == ActiveSource::Bluetooth;
  portEXIT_CRITICAL(&sourceMux);
  return selected;
}

bool bluetoothAudioOutputAllowed() {
  portENTER_CRITICAL(&sourceMux);
  const bool allowed = sourceState.bluetoothAudioOutputAllowed();
  portEXIT_CRITICAL(&sourceMux);
  return allowed;
}

bool bluetoothPhysicallyConnected() {
  portENTER_CRITICAL(&sourceMux);
  const bool connected = sourceState.bluetoothPhysicallyConnected();
  portEXIT_CRITICAL(&sourceMux);
  return connected;
}

#if VOXONE_BT_I2S_RX_ENABLED
uint16_t sourceManagerGetVuLevel(uint16_t dimension, bool& playing) {
  bool bluetoothActive = false;
  bool bluetoothPlaying = false;
  uint16_t vuLeft = 0;
  uint16_t vuRight = 0;
  uint32_t lastDataMs = 0;
  portENTER_CRITICAL(&sourceMux);
  bluetoothActive = sourceState.active() == ActiveSource::Bluetooth;
  bluetoothPlaying = sourceState.bluetoothPlaying();
  sourceState.bluetoothRawVu(vuLeft, vuRight, lastDataMs);
  portEXIT_CRITICAL(&sourceMux);

  if (!bluetoothActive) {
    const uint16_t legacy = player.get_VUlevel(dimension);
    const SourceVuResult result = sourceVuSelect(
        false, player.isRunning(), legacy, false, 0, 0, 0, millis(), dimension);
    playing = result.playing;
    return result.levels;
  }

  const SourceVuResult result = sourceVuSelect(
      true, player.isRunning(), 0, bluetoothPlaying, vuLeft, vuRight,
      lastDataMs, millis(), dimension);
  playing = result.playing;
  return result.levels;
}
#endif

bool radioI2SOutputEnabled() {
  if (systemUpdateAudioBlocked()) return false;
#if VOXONE_BT_I2S_RX_ENABLED
  return !bluetoothOwnsAudio(bluetoothSourceSelected(), player.temporaryActive()) &&
         btAudioInput.radioOutputReady();
#else
  return true;
#endif
}

#if VOXONE_BT_I2S_RX_ENABLED
void audio_process_extern(int16_t*, uint16_t, bool* continueI2S) {
  *continueI2S = radioI2SOutputEnabled();
}
#endif

bool bluetoothTransportAvailable() {
  portENTER_CRITICAL(&sourceMux);
  const bool available = sourceState.canControlBluetooth();
  portEXIT_CRITICAL(&sourceMux);
  return available;
}

bool sourceManagerStepBluetoothVolume(int8_t delta) {
  portENTER_CRITICAL(&sourceMux);
  const bool available = sourceState.canControlBluetooth();
  portEXIT_CRITICAL(&sourceMux);
  if (!available || delta == 0) return false;
  int next = static_cast<int>(config.userVolume) + delta;
  if (next < 0) next = 0;
  if (next > 100) next = 100;
  if (next == config.userVolume) return false;
  if (!btLink.setVolume(btUserToAbsolute(static_cast<uint8_t>(next)))) return false;
  player.setUserVol(static_cast<uint8_t>(next));
  volumeSync.sentUserCommand(static_cast<uint8_t>(next));
  return true;
}

void sourceManagerTransport(BtTransportInput input) {
  DisplaySourceView source{};
  portENTER_CRITICAL(&sourceMux);
  sourceState.displayView(source);
  const BtTransportAction action = btTransportAction(input, source);
  portEXIT_CRITICAL(&sourceMux);
  switch (action) {
    case BtTransportAction::Previous: btLink.prev(); break;
    case BtTransportAction::Next: btLink.next(); break;
    case BtTransportAction::Play: btLink.play(); break;
    case BtTransportAction::Pause: btLink.pause(); break;
    case BtTransportAction::None: break;
  }
}

void cycleNextSource() {
  portENTER_CRITICAL(&sourceMux);
  const SourceUpdate update = sourceState.cycle(btLink.state());
  const ActiveSource active = sourceState.active();
  const bool temporaryAtChange = player.temporaryBusy();
  portEXIT_CRITICAL(&sourceMux);
  if (!update.activeChanged) return;
  stopOnSourceChange(update, temporaryAtChange);
  serialCli.printf("##[SOURCE]# active=%s reason=manual\n", sourceName(active));
  refreshDisplay(update);
  netserver.requestOnChange(WEBSTATUS, 0);
}

void sourceManagerWebSnapshot(SourceWebSnapshot& snapshot) {
  const bool radioPlaying = player.isRunning();
  portENTER_CRITICAL(&sourceMux);
  DisplaySourceView view{};
  sourceState.displayView(view, radioPlaying);
  snapshot.kind = view.kind;
  snapshot.connected = view.connected;
  snapshot.playback = view.playback;
  snapshot.sampleRate = view.sampleRate;
  strlcpy(snapshot.peerName, view.peerName, sizeof(snapshot.peerName));
  strlcpy(snapshot.artist, view.artist, sizeof(snapshot.artist));
  strlcpy(snapshot.title, view.title, sizeof(snapshot.title));
  portEXIT_CRITICAL(&sourceMux);
}

bool getDisplaySourceView(DisplaySourceView& view) {
  const bool radioPlaying = player.isRunning();
  portENTER_CRITICAL(&sourceMux);
  sourceState.displayView(view, radioPlaying);
  if (view.kind == DisplaySourceKind::Bluetooth) {
    memcpy(displayText.peerName, view.peerName, sizeof(displayText.peerName));
    memcpy(displayText.artist, view.artist, sizeof(displayText.artist));
    memcpy(displayText.title, view.title, sizeof(displayText.title));
    view.peerName = displayText.peerName;
    view.artist = displayText.artist;
    view.title = displayText.title;
  }
  portEXIT_CRITICAL(&sourceMux);
  return true;
}

#endif
