#include "source_manager.h"
#include "utf8_text.h"
#include <Arduino.h>
#include "source_manager_state.h"
#include "temporary_audio_state.h"
#include "config.h"
#include "network.h"
#include "system_operation_state.h"
#include "display.h"
#include "netserver.h"
#include "player.h"
#include "serialcli.h"
#include "../hardware/hardware_descriptor.h"

#include <atomic>

namespace {
SourceManagerState sourceState;
portMUX_TYPE sourceMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint8_t> pendingSourceRequest{0};
std::atomic<uint8_t> pendingSourceChangedEvent{0};

struct DisplayText {
  char peerName[97];
  char artist[193];
  char title[193];
};
DisplayText displayText{};

const char* sourceName(ActiveSource source) {
  if (source == ActiveSource::Bluetooth) return "BT";
  if (source == ActiveSource::SOURCE_HA) return "HA";
  if (source == ActiveSource::SOURCE_DLNA) return "DLNA";
  if (source == ActiveSource::Aux) return "AUX";
  return "WEB";
}

SourceId sourceId(ActiveSource source) {
  if (source == ActiveSource::Bluetooth) return SourceId::Bluetooth;
  if (source == ActiveSource::SOURCE_HA) return SourceId::HomeAssistant;
  if (source == ActiveSource::SOURCE_DLNA) return SourceId::Dlna;
  if (source == ActiveSource::Aux) return SourceId::Aux;
  return SourceId::Web;
}

ActiveSource activeSource(SourceId source) {
  if (source == SourceId::Bluetooth) return ActiveSource::Bluetooth;
  if (source == SourceId::HomeAssistant) return ActiveSource::SOURCE_HA;
  if (source == SourceId::Dlna) return ActiveSource::SOURCE_DLNA;
  if (source == SourceId::Aux) return ActiveSource::Aux;
  return ActiveSource::Radio;
}

void publishSourceChanged(const SourceUpdate& update, ActiveSource active) {
  if (!update.activeChanged) return;
  pendingSourceChangedEvent.store(
      static_cast<uint8_t>(sourceId(active)) + 1U,
      std::memory_order_release);
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
  if (!update.activeChanged) return;
  // A temporary stream owns Player until its queued STOP is consumed. Its
  // cleanup publishes the base source selected here.
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
  network.lostPlaying = false;
  player.resetQueue();
  if (radio.userStop) player.sendCommand({PR_STOP, 0});
  if (radio.suspend) player.sendCommand({PR_RADIO_SUSPEND, 0});
  if (radio.resume) player.sendCommand({PR_RADIO_RESUME, resumeStation});
}

void applyManualSourceChange(const SourceUpdate& update, ActiveSource active,
                             bool temporaryAtChange) {
  if (!update.activeChanged) return;
  publishSourceChanged(update, active);
  stopOnSourceChange(update, temporaryAtChange);
  serialCli.printf("##[SOURCE]# active=%s reason=manual\n", sourceName(active));
  refreshDisplay(update);
  netserver.requestOnChange(WEBSTATUS, 0);
}
}

uint8_t sourceManagerSourceMask() {
  const auto& capabilities = voxone::hardware::hardwareCapabilities();
  return sourceMaskForCapabilities(
      capabilities.supportsVoxOneBt, capabilities.supportsHa,
      capabilities.supportsDlna, capabilities.supportsAux);
}

bool sourceManagerSourceAvailable(SourceId source) {
  return sourceIncluded(sourceManagerSourceMask(), source);
}

bool sourceManagerRequestSource(SourceId source) {
  if (systemUpdateAudioBlocked() || !sourceManagerSourceAvailable(source))
    return false;
  pendingSourceRequest.store(
      static_cast<uint8_t>(source) + 1U, std::memory_order_release);
  return true;
}

void sourceManagerServiceRequests() {
  const uint8_t request =
      pendingSourceRequest.exchange(0, std::memory_order_acq_rel);
  if (request == 0) return;
  const SourceId source = static_cast<SourceId>(request - 1U);
  if (!sourceManagerSelectSource(source))
    netserver.requestOnChange(WEBSTATUS, 0);
}

bool sourceManagerTakeEvent(SourceManagerEvent& event) {
  const uint8_t source =
      pendingSourceChangedEvent.exchange(0, std::memory_order_acq_rel);
  if (source == 0) return false;
  event.type = SourceManagerEventType::EVENT_SOURCE_CHANGED;
  event.source = static_cast<SourceId>(source - 1U);
  return true;
}

SourceId sourceManagerActiveSource() {
  portENTER_CRITICAL(&sourceMux);
  const SourceId active = sourceId(sourceState.active());
  portEXIT_CRITICAL(&sourceMux);
  return active;
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

void sourceManagerPlaylistSelectionCleared() {
  portENTER_CRITICAL(&sourceMux);
  sourceState.playlistSelectionCleared();
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

void sourceManagerBeginHomeAssistant(const char* title, const char* artist) {
  portENTER_CRITICAL(&sourceMux);
  const SourceUpdate update = sourceState.beginHomeAssistant(title, artist);
  portEXIT_CRITICAL(&sourceMux);
  publishSourceChanged(update, ActiveSource::SOURCE_HA);
  refreshDisplay(update);
  netserver.requestOnChange(WEBSTATUS, 0);
  serialCli.printf("##[SOURCE]# active=HA title=%.96s artist=%.96s\n",
                   title ? title : "", artist ? artist : "");
}

void sourceManagerEndHomeAssistant() {
  portENTER_CRITICAL(&sourceMux);
  const SourceUpdate update = sourceState.endHomeAssistant();
  const ActiveSource active = sourceState.active();
  portEXIT_CRITICAL(&sourceMux);
  if (!update.activeChanged) return;
  publishSourceChanged(update, active);
  refreshDisplay(update);
  netserver.requestOnChange(WEBSTATUS, 0);
  serialCli.printf("##[SOURCE]# active=%s reason=ha-finished\n",
                   sourceName(active));
}

bool sourceManagerHomeAssistantActive() {
  portENTER_CRITICAL(&sourceMux);
  const bool active = sourceState.homeAssistantActive();
  portEXIT_CRITICAL(&sourceMux);
  return active;
}

void sourceManagerBeginDlna(const char* title, const char* artist) {
  portENTER_CRITICAL(&sourceMux);
  const SourceUpdate update = sourceState.beginDlna(title, artist);
  portEXIT_CRITICAL(&sourceMux);
  publishSourceChanged(update, ActiveSource::SOURCE_DLNA);
  refreshDisplay(update);
  netserver.requestOnChange(WEBSTATUS, 0);
  serialCli.printf("##[SOURCE]# active=DLNA title=%.96s artist=%.96s\n",
                   title ? title : "", artist ? artist : "");
}

void sourceManagerEndTemporary() {
  portENTER_CRITICAL(&sourceMux);
  const ActiveSource finished = sourceState.active();
  const SourceUpdate update = sourceState.endTemporary();
  const ActiveSource active = sourceState.active();
  portEXIT_CRITICAL(&sourceMux);
  if (!update.activeChanged) return;
  publishSourceChanged(update, active);
  refreshDisplay(update);
  netserver.requestOnChange(WEBSTATUS, 0);
  serialCli.printf("##[SOURCE]# active=%s reason=%s-finished\n",
                   sourceName(active),
                   finished == ActiveSource::SOURCE_DLNA ? "dlna" : "ha");
}

bool sourceManagerDlnaActive() {
  portENTER_CRITICAL(&sourceMux);
  const bool active = sourceState.dlnaActive();
  portEXIT_CRITICAL(&sourceMux);
  return active;
}

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE

#include "bt_link.h"
#include "bt_audio_input.h"
#include "bt_volume.h"
#include "source_manager_state.h"
#include "source_vu_state.h"

namespace {
BtVolumeSessionGate volumeSessionGate;
std::atomic<bool> configuredBtVolumePending{false};
uint32_t configuredBtVolumeAttemptMs = 0;
constexpr uint32_t kConfiguredBtVolumeRetryMs = 500;

const char* sourceReason(SourceChangeReason reason) {
  switch (reason) {
    case SourceChangeReason::Manual: return "manual";
    case SourceChangeReason::BtConnect: return "bt-connect";
    case SourceChangeReason::BtDisconnect: return "bt-disconnect";
    case SourceChangeReason::BtOffline: return "bt-offline";
    default: return "unknown";
  }
}

bool sendBluetoothUserVolume(uint8_t user) {
  BtVolumeSessionCommand command;
  portENTER_CRITICAL(&sourceMux);
  const bool negotiated = volumeSessionGate.authorized();
  const bool prepared =
      negotiated && volumeSessionGate.prepareLocalCommand(user, command);
  portEXIT_CRITICAL(&sourceMux);
  if (negotiated) {
    // Never mix the session-aware and legacy writers for one negotiated peer.
    // Until BEGIN has been consumed, an explicit local change fails closed.
    if (!prepared) return false;
    if (btLink.setSessionVolume(command)) return true;
    portENTER_CRITICAL(&sourceMux);
    volumeSessionGate.localCommandFailed(command.commandSequence);
    portEXIT_CRITICAL(&sourceMux);
    return false;
  }
  // Legacy peers cannot authenticate inbound VOLUME events, but explicit
  // local user commands remain safe and retain their old wire format.
  return btLink.setVolume(btUserToAbsolute(user));
}

bool tryApplyConfiguredBluetoothVolume() {
  if (systemUpdateAudioBlocked()) return false;
  const auto runtime = config.runtimeSnapshot();
  const uint8_t configured = runtime.lineOutEnabled
                                 ? runtime.lineOutVolume
                                 : player.outputSnapshot().userVolume;
  portENTER_CRITICAL(&sourceMux);
  const bool available = sourceState.canControlBluetooth();
  portEXIT_CRITICAL(&sourceMux);
  if (!available || !sendBluetoothUserVolume(configured)) return false;
  serialCli.printf("##[BT-AUDIO]# configured volume=%u mute=%u\n",
                   static_cast<unsigned>(configured),
                   player.isMuted() ? 1U : 0U);
  return true;
}
}  // namespace

void sourceManagerBegin() {
  serialCli.printf("##[SOURCE]# active=RADIO\n");
}

void sourceManagerLoop() {
  if (systemUpdateAudioBlocked()) return;
  portENTER_CRITICAL(&sourceMux);
  const SourceUpdate update = sourceState.observe(btLink.state());
  const ActiveSource active = sourceState.active();
  const bool temporaryAtChange = player.temporaryBusy();
  if (temporaryBtLossRequiresRadioStop(update, temporaryAtChange))
    player.suppressTemporaryRadioRestore();
  portEXIT_CRITICAL(&sourceMux);
  stopOnSourceChange(update, temporaryAtChange);
  const bool sessionCapable = btLink.volumeSessionCapable();
  portENTER_CRITICAL(&sourceMux);
  volumeSessionGate.setAuthorized(sessionCapable && !update.btDisconnected);
  portEXIT_CRITICAL(&sourceMux);
  BtVolumeSessionEvent volumeEvent;
  while (btLink.takeVolumeSessionEvent(volumeEvent)) {
    if (!sessionCapable || update.btDisconnected) continue;
    const auto runtime = config.runtimeSnapshot();
    const uint8_t currentUser = runtime.lineOutEnabled
                                    ? runtime.lineOutVolume
                                    : player.outputSnapshot().userVolume;
    uint8_t peerUser = currentUser;
    portENTER_CRITICAL(&sourceMux);
    const BtVolumeDecision decision = volumeSessionGate.receive(
        volumeEvent, currentUser, active == ActiveSource::Bluetooth,
        peerUser);
    portEXIT_CRITICAL(&sourceMux);
    if (decision == BtVolumeDecision::PeerVolumeAccepted) {
      if (runtime.lineOutEnabled)
        sendBluetoothUserVolume(runtime.lineOutVolume);
      else
        player.setUserVol(peerUser);
    }
  }
  if ((update.btConnected || update.activeChanged) &&
      active == ActiveSource::Bluetooth)
    configuredBtVolumePending.store(true, std::memory_order_release);
  if (active != ActiveSource::Bluetooth) {
    configuredBtVolumePending.store(false, std::memory_order_release);
  } else if (configuredBtVolumePending.load(std::memory_order_acquire)) {
    const uint32_t nowMs = millis();
    if (nowMs - configuredBtVolumeAttemptMs >= kConfiguredBtVolumeRetryMs) {
      configuredBtVolumeAttemptMs = nowMs;
      if (tryApplyConfiguredBluetoothVolume())
        configuredBtVolumePending.store(false, std::memory_order_release);
    }
  }
  if (update.activeChanged)
    serialCli.printf("##[SOURCE]# active=%s reason=%s\n",
                     sourceName(active), sourceReason(update.reason));
  publishSourceChanged(update, active);
  refreshDisplay(update);
  if (update.activeChanged || update.stationChanged || update.titleChanged ||
      update.audioInfoChanged || update.btConnected || update.btDisconnected)
    netserver.requestOnChange(WEBSTATUS, 0);
}

void sourceManagerStopForUpdate() {
  portENTER_CRITICAL(&sourceMux);
  sourceState.stopForUpdate(btLink.state());
  volumeSessionGate.setAuthorized(false);
  portEXIT_CRITICAL(&sourceMux);
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
  return allowed && player.localAudioAvailable();
}

bool bluetoothPhysicallyConnected() {
  portENTER_CRITICAL(&sourceMux);
  const bool connected = sourceState.bluetoothPhysicallyConnected();
  portEXIT_CRITICAL(&sourceMux);
  return connected;
}

#if VOXONE_BT_I2S_RX_ENABLED
uint16_t sourceManagerGetVuLevel(uint16_t dimension, bool& playing) {
  if (systemUpdateAudioBlocked()) { playing = false; return 0; }
  bool bluetoothActive = false;
  bool bluetoothPlaying = false;
  uint16_t vuLeft = 0;
  uint16_t vuRight = 0;
  uint32_t lastDataMs = 0;
  portENTER_CRITICAL(&sourceMux);
  bluetoothActive = sourceState.active() == ActiveSource::Bluetooth;
  bluetoothPlaying = player.localAudioAvailable() &&
                     sourceState.bluetoothPlaying();
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
  if (systemUpdateAudioBlocked() || !player.localAudioAvailable()) return false;
  portENTER_CRITICAL(&sourceMux);
  const bool playerSource = sourceState.localPlayerOutputAllowed();
  portEXIT_CRITICAL(&sourceMux);
  if (!playerSource) return false;
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
  if (systemUpdateAudioBlocked() ||
      config.runtimeSnapshot().lineOutEnabled) return false;
  portENTER_CRITICAL(&sourceMux);
  const bool available = sourceState.canControlBluetooth();
  portEXIT_CRITICAL(&sourceMux);
  if (!available || delta == 0) return false;
  const uint8_t currentUser = player.outputSnapshot().userVolume;
  int next = static_cast<int>(currentUser) + delta;
  if (next < 0) next = 0;
  if (next > 100) next = 100;
  if (next == currentUser) return false;
  return sourceManagerSetBluetoothVolume(static_cast<uint8_t>(next));
}

bool sourceManagerSetBluetoothVolume(uint8_t user) {
  if (systemUpdateAudioBlocked() ||
      config.runtimeSnapshot().lineOutEnabled) return false;
  if (user > 100) user = 100;
  portENTER_CRITICAL(&sourceMux);
  const bool available = sourceState.canControlBluetooth();
  portEXIT_CRITICAL(&sourceMux);
  if (!available || user == player.outputSnapshot().userVolume) return false;
  if (!sendBluetoothUserVolume(user)) return false;
  player.setUserVol(user);
  return true;
}

void sourceManagerApplyConfiguredBluetoothVolume() {
  configuredBtVolumePending.store(true, std::memory_order_release);
}

void sourceManagerTransport(BtTransportInput input) {
  if (systemUpdateAudioBlocked()) return;
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

#endif

bool sourceManagerSelectSource(ActiveSource source) {
  if (systemUpdateAudioBlocked() ||
      !sourceManagerSourceAvailable(sourceId(source))) return false;
  BtLinkState bt{};
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  bt = btLink.state();
#endif
  const bool temporaryAtChange = player.temporaryBusy();
  portENTER_CRITICAL(&sourceMux);
  const SourceUpdate update = sourceState.select(source, bt);
  portEXIT_CRITICAL(&sourceMux);
  if (update.activeChanged && temporaryAtChange) {
    if (source != ActiveSource::Radio)
      player.suppressTemporaryRadioRestore();
    player.sendCommand({PR_STOP, 0});
  }
  applyManualSourceChange(update, source, temporaryAtChange);
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  if (update.activeChanged && source == ActiveSource::Bluetooth)
    sourceManagerApplyConfiguredBluetoothVolume();
#endif
  return update.activeChanged;
}

bool sourceManagerSelectSource(SourceId source) {
  return sourceManagerSelectSource(activeSource(source));
}

void sourceManagerWebSnapshot(SourceWebSnapshot& snapshot) {
  const bool playing = player.isRunning();
  portENTER_CRITICAL(&sourceMux);
  DisplaySourceView view{};
  sourceState.displayView(view, playing, player.localAudioAvailable());
  snapshot.kind = view.kind;
  snapshot.connected = view.connected;
  snapshot.playback = view.playback;
  snapshot.sampleRate = view.sampleRate;
  voxone::utf8::copy(snapshot.peerName, sizeof(snapshot.peerName),
                     view.peerName);
  voxone::utf8::copy(snapshot.artist, sizeof(snapshot.artist), view.artist);
  voxone::utf8::copy(snapshot.title, sizeof(snapshot.title), view.title);
  portEXIT_CRITICAL(&sourceMux);
}

bool getDisplaySourceView(DisplaySourceView& view) {
  const bool playing = player.isRunning();
  portENTER_CRITICAL(&sourceMux);
  sourceState.displayView(view, playing, player.localAudioAvailable());
  voxone::utf8::copy(displayText.peerName, sizeof(displayText.peerName),
                     view.peerName);
  voxone::utf8::copy(displayText.artist, sizeof(displayText.artist),
                     view.artist);
  voxone::utf8::copy(displayText.title, sizeof(displayText.title), view.title);
  view.peerName = displayText.peerName;
  view.artist = displayText.artist;
  view.title = displayText.title;
  portEXIT_CRITICAL(&sourceMux);
  return true;
}
