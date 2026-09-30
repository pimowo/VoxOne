#include "source_manager.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE

#include "bt_link.h"
#include "bt_audio_input.h"
#include "bt_volume.h"
#include "config.h"
#include "display.h"
#include "player.h"
#include "serialcli.h"
#include "source_manager_state.h"
#include "source_vu_state.h"

namespace {
SourceManagerState sourceState;
BtVolumeSync volumeSync;
portMUX_TYPE sourceMux = portMUX_INITIALIZER_UNLOCKED;

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
}  // namespace

void sourceManagerBegin() {
  serialCli.printf("##[SOURCE]# active=RADIO\n");
}

void sourceManagerLoop() {
  portENTER_CRITICAL(&sourceMux);
  const SourceUpdate update = sourceState.observe(btLink.state());
  const ActiveSource active = sourceState.active();
  portEXIT_CRITICAL(&sourceMux);
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
}

bool bluetoothSourceSelected() {
  portENTER_CRITICAL(&sourceMux);
  const bool selected = sourceState.active() == ActiveSource::Bluetooth;
  portEXIT_CRITICAL(&sourceMux);
  return selected;
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
#if VOXONE_BT_I2S_RX_ENABLED
  return !bluetoothSourceSelected() && btAudioInput.radioOutputReady();
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
  portEXIT_CRITICAL(&sourceMux);
  if (!update.activeChanged) return;
  serialCli.printf("##[SOURCE]# active=%s reason=manual\n", sourceName(active));
  refreshDisplay(update);
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
