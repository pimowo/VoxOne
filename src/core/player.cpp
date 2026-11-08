#include "options.h"
#include "player.h"
#include "config.h"
#include "serialcli.h"
#include "display.h"
#include "netserver.h"
#include "timekeeper.h"
#include "volume_map.h"
#include "system_operation_state.h"
#include "source_manager.h"
#include "network.h"
#include "bt_audio_input.h"
#include "ui_state.h"
#include "playlist_mapping.h"
#include "playlist_store.h"
#include "station_format.h"
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
#include "tts_hil_diagnostics.h"
#endif
#include "../hardware/runtime_audio_output.h"
#include "../hardware/runtime_hardware_config.h"
#if I2S_DOUT!=255
#include <driver/i2s.h>
#endif
#include "../displays/tools/l10n.h"
Player player;
QueueHandle_t playerQueue;
portMUX_TYPE playerVolumeMux = portMUX_INITIALIZER_UNLOCKED;

#if !I2S_INTERNAL
Player::Player(): Audio(false, 3, I2S_NUM_0, false) {}
#else
Player::Player(): Audio(true, 3, I2S_NUM_0, false) {}
#endif


void Player::init() {
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
  // Construct the fixed diagnostic store during boot, never on the audio path.
  (void)voxone::tts_hil::diagnostics();
#endif
  const auto runtime = config.runtimeSnapshot();
  Serial.print("##[BOOT]#\tplayer.init\t");
  playerQueue=NULL;
  _hasError=false;
  _pendingVolume = runtime.volume;
  _pendingMode = 0;
  _volumePending = false;
  _maximumPending = false;
  _mute.setUserVolume(runtime.userVolume);
  _radioReconnect.reset();
  _pendingStationRefresh.reset();
  const auto audioPlan = voxone::hardware::runtimeAudioActivationPlan(
      voxone::hardware::runtimeHardwareConfig().audio);
  _localAudioAvailable = audioPlan.localPlaybackAvailable;
  playerQueue = xQueueCreate( 5, sizeof( playerRequestParams_t ) );
  setOutputPins(false);
  delay(50);
  _temporaryUrls = xQueueCreate(1, voxone::temporary_audio::kUrlCapacity);
  if(MUTE_PIN!=255) pinMode(MUTE_PIN, OUTPUT);
  #if I2S_DOUT!=255
    if (audioPlan.initializeSerialI2s &&
        (!initializeI2S()
         #if !I2S_INTERNAL
          || !setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT)
         #endif
        )) {
      _localAudioAvailable = false;
      Serial.println("failed: local I2S unavailable");
    }
  #endif
  setBalance(runtime.balance);
  setTone(runtime.bass, runtime.middle, runtime.treble);
  setVolume(0);
  _status = STOPPED;
  _volTimer=config.volumeBootDirty;
  if (_volTimer) _volTicks = millis();
  //randomSeed(analogRead(0));
  #if PLAYER_FORCE_MONO
    forceMono(true);
  #endif
  _loadVol(runtime.volume);
  setConnectionTimeout(CONNECTION_TIMEOUT, CONNECTION_TIMEOUT_SSL);
  const bool taskStarted = startAudioTask();
  Serial.printf("done (decode task: %s, core=%u, priority=%u, buffer=%u+%u KiB)\n",
                taskStarted ? "started" : "main-loop fallback",
                static_cast<unsigned>(voxone::audio_runtime::kTaskCore),
                static_cast<unsigned>(voxone::audio_runtime::kTaskPriority),
                static_cast<unsigned>(
                    voxone::audio_runtime::kPsramInputBufferBytes / 1024U),
                static_cast<unsigned>(
                    voxone::audio_runtime::kFlacFrameReserveBytes / 1024U));
}

bool Player::ready() const {
  return playerQueue != nullptr && _temporaryUrls != nullptr;
}

void Player::sendCommand(playerRequestParams_t request){
  if(playerQueue==NULL) return;
  if(systemUpdateAudioBlocked() && request.type != PR_UPDATE_STOP) return;
  if (!_localAudioAvailable &&
      (request.type == PR_PLAY || request.type == PR_BURL ||
       request.type == PR_RADIO_RESUME ||
       request.type == PR_RADIO_NETWORK_ONLINE)) return;
  if (request.type == PR_STATION_REFRESH) {
    _pendingStationRefresh.publishRefresh();
    return;
  }
  if(xQueueSend(playerQueue, &request, PLQ_SEND_DELAY) != pdPASS) return;
  switch (request.type) {
    case PR_PLAY:
    case PR_STOP:
    case PR_BURL:
    case PR_RADIO_SUSPEND:
    case PR_RADIO_RESUME:
    case PR_UPDATE_STOP:
    case PR_RADIO_NETWORK_OFFLINE:
      cancelPendingConnection();
      break;
    default:
      break;
  }
  if(request.type == PR_PLAY || request.type == PR_STOP)
    sourceManagerRadioCommandQueued(request.type == PR_PLAY,
                                   request.type == PR_PLAY && request.payload > 0
                                       ? static_cast<uint16_t>(request.payload) : 0);
}

void Player::resetQueue(){
  if(playerQueue!=NULL) xQueueReset(playerQueue);
}

void Player::stopInfo() {
  config.setSmartStart(0);
  netserver.requestOnChange(MODE, 0);
}

void Player::setError(){
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
  const bool temporarySignalled = _temporary.signal(_callbackToken);
  if (_callbackToken) {
    voxone::tts_hil::diagnostics().transportError(
        _callbackToken, millis(), transportProgressBytes(),
        httpBodyProgressBytes(), httpContentLength(),
        temporarySignalled);
  }
#else
  _temporary.signal(_callbackToken);
#endif
  if (radioMetadataAllowed()) {
    _hasError=true;
    config.setTitle(config.tmpBuf);
  }
  serialCli.printf("##ERROR#:\t%s\n", config.tmpBuf);
}

void Player::setError(const char *e){
  strlcpy(config.tmpBuf, e, sizeof(config.tmpBuf));
  setError();
}

void Player::_stop(bool alreadyStopped, RadioStopReason reason,
                   bool preserveRadioPresentation){
  log_i("%s called", __func__);
  _status = STOPPED;
  if (reason == RadioStopReason::Normal) _radioReconnect.cancel();
  setOutputPins(false);
  if (!preserveRadioPresentation) {
    if(!_hasError) config.setTitle((uiState.mode()==LOST || uiState.mode()==UPDATING)?"":LANG::const_PlStopped);
    config.station.bitrate = 0;
    config.setBitrateFormat(BF_UNKNOWN);
  }
  setDefaults();
  if(!alreadyStopped) stopSong();
  if (systemUpdateAudioBlocked()) {
#if I2S_DOUT!=255
    i2s_zero_dma_buffer(I2S_NUM_0);
#endif
    systemUpdateRadioStopped();
  }
  if (!preserveRadioPresentation) {
    netserver.requestOnChange(BITRATE, 0);
    display.putRequest(DBITRATE);
  }
  display.putRequest(PSTOP);
  //setDefaults();
  //if(!alreadyStopped) stopSong();
  if(radioStopUpdatesSmartStart(reason, lockOutput)) stopInfo();
  if (player_on_stop_play) player_on_stop_play();
}

void Player::_clearStation(bool resetOvol) {
  config.station.id = 0;
  config.station.metadataMode = STATION_META_NORMAL;
  config.station.name[0] = '\0';
  config.station.url[0] = '\0';
  config.station.title[0] = '\0';
  if (resetOvol) config.station.ovol = 0;
  display.putRequest(NEWSTATION);
  display.putRequest(NEWTITLE);
  netserver.requestOnChange(STATION, 0);
  netserver.requestOnChange(TITLE, 0);
}

bool Player::_refreshStation(uint16_t stationId, bool* identityChanged) {
  const uint64_t previousId = config.station.id;
  char previousUrl[sizeof(config.station.url)];
  strlcpy(previousUrl, config.station.url, sizeof(previousUrl));
  const int previousOvol = config.station.ovol;
  const uint8_t previousMode = config.station.metadataMode;
  if (!config.loadStation(stationId)) return false;
  if (identityChanged) {
    *identityChanged = previousId != config.station.id ||
        strcmp(previousUrl, config.station.url) != 0;
  }
  const StationRefreshChanges changes = stationRefreshChanges(
      previousOvol, previousMode, config.station.ovol,
      config.station.metadataMode);
  if (changes.volume) setVol(config.runtimeSnapshot().volume);
  display.putRequest(NEWSTATION);
  netserver.requestOnChange(STATION, 0);
  if (changes.metadataMode) {
    display.putRequest(NEWTITLE);
    netserver.requestOnChange(TITLE, 0);
  }
  return true;
}

void Player::_servicePendingStationRefresh() {
  if (systemUpdateAudioBlocked() || !_pendingStationRefresh.pending()) return;
  PlaylistGuard guard(0);
  if (!guard) return;
  const PlayerPendingRefreshRequest request = _pendingStationRefresh.take();
  if (!request) return;
  // Commands accepted before this reconciliation retain FIFO precedence.
  // A concurrent later command either appears here or runs after this method.
  if (uxQueueMessagesWaiting(playerQueue) != 0) {
    _pendingStationRefresh.republish(request);
    return;
  }
  const uint16_t stationId = config.lastStation();
  if (stationId == 0) {
    _clearStation(request.resetOvolIfEmpty);
    guard.release();
    // A newer playlist commit owns the next reconciliation. Commands queued
    // after the guarded empty-queue check are ordered after this stop.
    if (_pendingStationRefresh.pending()) return;
    sourceManagerPlaylistSelectionCleared();
    _applyStop(PR_STOP_KEEP_STATION, false);
    return;
  }
  bool identityChanged = false;
  if (!_refreshStation(stationId, &identityChanged)) {
    _pendingStationRefresh.republish(request);
    return;
  }
  guard.release();
  if (_pendingStationRefresh.pending()) return;
  if (temporaryBusy()) {
    _pendingStationRefresh.publishRefresh();
    return;
  }
  if (identityChanged && isRunning() && sourceManagerRadioResumeAllowed()) {
    _radioReconnect.cancel();
    _play(stationId);
    if (player_on_station_change) player_on_station_change();
  }
}

void Player::_applyStop(int payload, bool consumeUserCommand) {
  if (payload < PR_STOP_KEEP_STATION) {
    _clearStation(payload == PR_STOP_CLEAR_STATION_RESET_OVOL);
  }
  _radioReconnect.cancel();
  network.lostPlaying = false;
  if (_temporaryUrls) xQueueReset(_temporaryUrls);
  if (temporaryBusy()) {
    if (!consumeUserCommand) suppressTemporaryRadioRestore();
    if (temporaryActive()) {
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
      const uint32_t token = _temporary.token();
      if (consumeUserCommand) {
        voxone::tts_hil::diagnostics().userStop(
            token, millis(), transportProgressBytes(),
            httpBodyProgressBytes(), httpContentLength());
      } else {
        voxone::tts_hil::diagnostics().sourceSwitch(
            token, millis(), transportProgressBytes(),
            httpBodyProgressBytes(), httpContentLength());
      }
#endif
      finishTemporary();
    }
    else if (systemUpdateAudioBlocked())
      _stop(false, RadioStopReason::SourceSwitch);
    if (!sourceManagerRadioPlayIntent()) stopInfo();
  } else {
    _stop();
  }
  if (consumeUserCommand) sourceManagerRadioStopConsumed();
}

void resetPlayer(){
  if(!config.runtimeSnapshot().watchdog) return;
  player.resetQueue();
  player.sendCommand({PR_STOP, 0});
  player.loop();
}

#ifndef PL_QUEUE_TICKS
  #define PL_QUEUE_TICKS 0
#endif
#ifndef PL_QUEUE_TICKS_ST
  #define PL_QUEUE_TICKS_ST 15
#endif
void Player::loop() {
  if(playerQueue==NULL) return;
  uint8_t pendingMaximum = 0;
  bool maximumPending = false;
  portENTER_CRITICAL(&playerVolumeMux);
  if (_maximumPending) {
    pendingMaximum = _pendingMaximum;
    _maximumPending = false;
    maximumPending = true;
  }
  portEXIT_CRITICAL(&playerVolumeMux);
  if (maximumPending) config.setMaximumVolume(pendingMaximum);
  uint8_t pendingVolume = 0;
  uint8_t pendingMode = 0;
  bool volumePending = false;
  portENTER_CRITICAL(&playerVolumeMux);
  if(_volumePending){
    pendingVolume = _pendingVolume;
    pendingMode = _pendingMode;
    _volumePending = false;
    volumePending = true;
  }
  portEXIT_CRITICAL(&playerVolumeMux);
  if(volumePending){
    const auto runtime = config.runtimeSnapshot();
    if (pendingMode == 1) {
      const VolumeState state = volumeStateFromUser(
          pendingVolume, runtime.maximumVolume);
      config.setVolumeState(state.raw, state.user);
    } else if (pendingMode == 0) {
      const VolumeState state = volumeStateFromRaw(
          pendingVolume, runtime.maximumVolume);
      config.setVolumeState(state.raw, state.user);
    } else if (pendingMode == 2) {
      config.setVolumeState(runtime.volume, pendingVolume);
    }
    Audio::setVolume(_mute.outputVolume(
        volToI2S(config.runtimeSnapshot().volume)));
  }
  if (_shutdownSuspended) return;
  playerRequestParams_t requestP;
  if(xQueueReceive(playerQueue, &requestP, isRunning()?PL_QUEUE_TICKS:PL_QUEUE_TICKS_ST)){
    if (systemUpdateAudioBlocked() && requestP.type != PR_UPDATE_STOP) return;
    switch (requestP.type){
      case PR_UPDATE_STOP:
        _radioReconnect.cancel();
        if (_temporaryUrls) xQueueReset(_temporaryUrls);
        suppressTemporaryRadioRestore();
        if (temporaryActive()) {
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
          voxone::tts_hil::diagnostics().updateStop(
              _temporary.token(), millis(), transportProgressBytes(),
              httpBodyProgressBytes(), httpContentLength());
#endif
          finishTemporary();
        }
        else _stop(false, RadioStopReason::SourceSwitch);
        break;
      case PR_STOP:
        _applyStop(requestP.payload, true);
        break;
      case PR_STATION_REFRESH:
        if (requestP.payload > 0)
          _refreshStation(static_cast<uint16_t>(requestP.payload));
        break;
      case PR_RADIO_SUSPEND:
        _radioReconnect.cancel();
        if (!temporaryBusy()) _stop(false, RadioStopReason::SourceSwitch);
        break;
      case PR_RADIO_RESUME:
        _radioReconnect.cancel();
        if (temporaryBusy()) break;
        if (sourceManagerRadioResumeAllowed()) {
          if (requestP.payload > 0 &&
              config.lastStation() != static_cast<uint16_t>(requestP.payload))
            config.setLastStation(static_cast<uint16_t>(requestP.payload));
          sourceManagerRadioPlayConsumed();
          _play(static_cast<uint16_t>(requestP.payload), true);
        }
        break;
      case PR_RADIO_NETWORK_OFFLINE:
        if (!sourceManagerRadioResumeAllowed()) {
          _radioReconnect.cancel();
          break;
        }
        if (!temporaryBusy()) _stop(false, RadioStopReason::SourceSwitch);
        scheduleRadioReconnect();
        serialCli.printf("##[RADIO]# network offline, reconnect suspended\n");
        break;
      case PR_RADIO_NETWORK_ONLINE:
        if (temporaryBusy() || systemUpdateAudioBlocked() ||
            !sourceManagerRadioResumeAllowed()) {
          _radioReconnect.cancel();
          break;
        }
        scheduleRadioReconnect();
        serialCli.printf("##[RADIO]# network online, reconnect resumed\n");
        break;
      case PR_PLAY: {
        // Accepted commands already updated canonical intent/station. Let the
        // announcement finish, then use that current intent exactly once.
        if (temporaryBusy()) break;
        _radioReconnect.cancel();
        if (requestP.payload>0) {
          config.setLastStation((uint16_t)requestP.payload);
        }
        sourceManagerRadioPlayConsumed();
        _play((uint16_t)abs(requestP.payload)); 
        if (player_on_station_change) player_on_station_change(); 
        break;
      }
      case PR_TOGGLE: {
        toggle();
        break;
      }
      case PR_VOL: {
        const uint8_t requested = static_cast<uint8_t>(constrain(requestP.payload, 0, 254));
        const VolumeState state = volumeStateFromRaw(
            requested, config.runtimeSnapshot().maximumVolume);
        config.setVolumeState(state.raw, state.user);
        Audio::setVolume(_mute.outputVolume(volToI2S(state.raw)));
        _volTicks = millis();
        _volTimer = true;
        break;
      }
      case PR_VUTONUS: {
        config.reduceVuThreshold(10);
        break;
      }
      case PR_BURL: {
        // Wake-up only. URL bytes are owned by the one-slot mailbox below.
        break;
      }
          
      default: break;
    }
  }
  if (uxQueueMessagesWaiting(playerQueue) == 0)
    _servicePendingStationRefresh();
  char url[voxone::temporary_audio::kUrlCapacity];
  if (_temporaryUrls && xQueueReceive(_temporaryUrls, url, 0) == pdTRUE &&
      !systemUpdateAudioBlocked()) browseUrl(url);
  // Backend EOF/error callbacks run synchronously inside these calls. They
  // signal this invocation's token; never reconnect from a decoder callback.
  _callbackToken = _temporary.token();
  if (!_temporary.terminal()) Audio::loop();
  _callbackToken = 0;
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
  voxone::tts_hil::diagnostics().consumeNetworkInterrupted(
      millis(), transportProgressBytes(), httpBodyProgressBytes(),
      httpContentLength());
#endif
  if (_radioReconnect.failures() && audioStreamStarted()) {
    serialCli.printf("##[RADIO]# stream recovered\n");
    _radioReconnect.reset();
  }
  if (temporaryActive()) {
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
    voxone::tts_hil::diagnostics().observeProgress(
        _temporary.token(), millis(), transportProgressBytes(),
        httpBodyProgressBytes(), httpContentLength());
#endif
    voxone::temporary_audio::serviceTemporaryAudioTransport(
        _temporary, _temporaryProgress, millis(), transportProgressBytes(),
        isRunning(), transportConnected(), transportBodyComplete(),
        transportBufferedBytes(), transportBufferedFrameAvailable(),
        [this](uint32_t token) {
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
          voxone::tts_hil::diagnostics().watchdogTimeout(
              token, millis(), transportProgressBytes(),
              httpBodyProgressBytes(), httpContentLength());
#endif
          serialCli.printf(
              "##[TTS]# transport stalled token=%lu timeout=%lu ms\n",
              static_cast<unsigned long>(token),
              static_cast<unsigned long>(
                  voxone::temporary_audio::kInactivityTimeoutMs));
        },
        [this](uint32_t token) {
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
          voxone::tts_hil::diagnostics().transportError(
              token, millis(), transportProgressBytes(),
              httpBodyProgressBytes(), httpContentLength(), true);
#endif
          serialCli.printf(
              "##[TTS]# transport disconnected before EOF token=%lu\n",
              static_cast<unsigned long>(token));
        },
        [this]() { finishTemporary(); });
  } else if (!isRunning() && _status==PLAYING) {
    _stop(true, RadioStopReason::SourceSwitch);
    scheduleRadioReconnect();
  }
  if (uxQueueMessagesWaiting(playerQueue) == 0 &&
      (!_temporaryUrls || uxQueueMessagesWaiting(_temporaryUrls) == 0))
    restoreTemporaryBase();
  serviceRadioReconnect();
  if(_volTimer){
    if((millis()-_volTicks)>3000){
      config.saveVolume();
      _volTimer=false;
    }
  }
}

void Player::stopForShutdown() {
  if (_shutdownSuspended) return;
  _shutdownResumeRadio = sourceManagerRadioResumeAllowed();
  _shutdownSuspended = true;
  _radioReconnect.cancel();
  cancelPendingConnection();
  resetQueue();
  if (_temporaryUrls) xQueueReset(_temporaryUrls);
  if (temporaryBusy()) {
    suppressTemporaryRadioRestore();
    finishTemporary();
  }

  // Silence the DAC synchronously before the display task can begin the
  // goodbye animation. The normal pending-volume path keeps mute state and
  // Web/API reporting consistent on the following loop iteration.
  setMuted(true);
  Audio::setVolume(0);
  _stop(false, RadioStopReason::SourceSwitch, true);
  stopAudioTask();
#if I2S_DOUT!=255
  i2s_zero_dma_buffer(I2S_NUM_0);
#endif
  Serial.println("##[POWER]# audio stopped before shutdown screen");
}

void Player::resumeAfterShutdown() {
  if (!_shutdownSuspended) return;
  const bool resumeRadio = _shutdownResumeRadio;
  _shutdownResumeRadio = false;
  _shutdownSuspended = false;
  if (!startAudioTask())
    Serial.println("##[WARN]# AudioTask resume failed; using loop fallback");
  if (resumeRadio && sourceManagerRadioResumeAllowed())
    sendCommand({PR_RADIO_RESUME, config.lastStation()});
}

void Player::setOutputPins(bool isPlaying) {
  if(REAL_LEDBUILTIN!=255) digitalWrite(REAL_LEDBUILTIN, LED_INVERT?!isPlaying:isPlaying);
  bool _ml = MUTE_LOCK?!MUTE_VAL:(isPlaying?!MUTE_VAL:MUTE_VAL);
  if(MUTE_PIN!=255) digitalWrite(MUTE_PIN, _ml);
}

void Player::_play(uint16_t stationId, bool sourceResume) {
  log_i("%s called, stationId=%d", __func__, stationId);
  _hasError=false;
  setDefaults();
  _status = STOPPED;
  setOutputPins(false);
  remoteStationName = false;
  
  if(!config.prepareForPlaying(stationId, sourceResume)) return;
  _loadVol(config.runtimeSnapshot().volume);
  _temporary.allowRadioMetadata();
  
  bool isConnected = false;
  connproc = false;
  isConnected=connecttohost(config.station.url);
  connproc = true;
  if(isConnected){
    _status = PLAYING;
    config.configPostPlaying();
    setOutputPins(true);
    if (player_on_start_play) player_on_start_play();
  }else{
    serialCli.printf("##ERROR#:\tError connecting to %.128s\n", config.station.url);
    snprintf(config.tmpBuf, sizeof(config.tmpBuf), "Error connecting to %.128s", config.station.url); setError();
    _stop(true, RadioStopReason::SourceSwitch);
    scheduleRadioReconnect();
  };
}

void Player::scheduleRadioReconnect() {
  if (temporaryBusy() || systemUpdateAudioBlocked() ||
      !sourceManagerRadioResumeAllowed()) {
    _radioReconnect.cancel();
    return;
  }
  if (_radioReconnect.schedule(millis())) {
    serialCli.printf("##[RADIO]# reconnect pending attempt=%u delay=%lu ms\n",
                     static_cast<unsigned>(_radioReconnect.failures()),
                     static_cast<unsigned long>(voxone::radio::ReconnectPolicy::
                         delayForFailure(_radioReconnect.failures() - 1)));
  }
}

void Player::serviceRadioReconnect() {
  const bool allowed = !temporaryBusy() && !systemUpdateAudioBlocked() &&
                       sourceManagerRadioResumeAllowed();
  if (!_radioReconnect.takeDue(millis(), allowed, network.ready())) return;
  serialCli.printf("##[RADIO]# reconnect attempt=%u\n",
                   static_cast<unsigned>(_radioReconnect.failures()));
  _play(config.lastStation(), true);
}

void Player::requestTemporaryUrl(const char* url, size_t length) {
  if (!_temporaryUrls ||
      !voxone::temporary_audio::isSafeUrl(url, length) ||
      systemUpdateAudioBlocked() || !_localAudioAvailable) return;
  char request[voxone::temporary_audio::kUrlCapacity]{};
  memcpy(request, url, length);
  // Network commands run on another task. Replace the pending URL atomically, never a
  // buffer currently used by connecttohost(). Latest pending request wins.
  xQueueOverwrite(_temporaryUrls, request);
  sendCommand({PR_BURL, 0});
}

bool Player::temporaryEof() {
  if (!_callbackToken) return false;
  const bool accepted = _temporary.signal(_callbackToken);
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
  voxone::tts_hil::diagnostics().naturalEof(
      _callbackToken, millis(), transportProgressBytes(),
      httpBodyProgressBytes(), httpContentLength(), accepted);
#endif
  return true;
}

bool Player::interruptTemporaryForNetwork() {
  const bool busy = temporaryBusy();
  const uint32_t token = _temporary.token();
  const bool accepted = _temporary.signal(token);
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
  voxone::tts_hil::diagnostics().notifyNetworkInterrupted(token, accepted);
#endif
  return busy;
}

void Player::finishTemporary() {
  const uint32_t token = _temporary.token();
  if (!token) return;
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
  // Close the narrow race where Wi-Fi signals terminal state after loop()'s
  // first mailbox check but before the one-shot cleanup dispatcher runs.
  voxone::tts_hil::diagnostics().consumeNetworkInterrupted(
      millis(), transportProgressBytes(), httpBodyProgressBytes(),
      httpContentLength());
#endif
  _callbackToken = 0;
  // Stop/clear Player while it still owns I2S0. This is not a user RADIO STOP.
  _stop(false, RadioStopReason::SourceSwitch, true);
  remoteStationName = false;
  if (!_temporary.finish(token)) return;
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
  voxone::tts_hil::diagnostics().cleanup(
      token, millis(), transportProgressBytes(), httpBodyProgressBytes(),
      httpContentLength());
#endif
  _temporaryProgress.finish(token);
  // The station data never changed, but consumers may have observed temporary
  // PLAY. Publish the final source/playback view and redraw from canonical
  // RADIO/BT state without persisting configuration.
  netserver.requestOnChange(MODE, 0);
  netserver.requestOnChange(WEBSTATUS, 0);
  display.putRequest(NEWSTATION);
  display.putRequest(NEWTITLE);
}

void Player::restoreTemporaryBase() {
  if (!temporaryBusy() || temporaryActive()) return;
  uint16_t station = config.lastStation();
  if (!sourceManagerTakeTemporaryRestore(_temporary,
          WiFi.isConnected(),
          systemUpdateAudioBlocked(), station)) return;
  if (station != config.lastStation()) config.setLastStation(station);
  _play(station, true);
}

void Player::browseUrl(const char* url){
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE && VOXONE_BT_I2S_RX_ENABLED
  if (!btAudioInput.acquirePlayerOutput()) {
    serialCli.printf("##ERROR#:\tTemporary URL cannot acquire I2S0\n");
    return;
  }
#endif
  _callbackToken = 0;
  // Replacement invalidates the previous token without changing the base.
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
  const uint32_t replacedToken = _temporary.token();
  if (replacedToken) {
    voxone::tts_hil::diagnostics().replaced(
        replacedToken, millis(), transportProgressBytes(),
        httpBodyProgressBytes(), httpContentLength());
  }
#endif
  const uint32_t token = _temporary.begin();
  _temporaryProgress.begin(token, millis(), transportProgressBytes());
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
  voxone::tts_hil::diagnostics().start(
      token, millis(), transportProgressBytes(), httpBodyProgressBytes());
#endif
  stopSong();
  network.lostPlaying = false;
  _hasError=false;
  remoteStationName = true;
  config.setDspOn(1);
  display.putRequest(PSTOP);
  setOutputPins(false);
  _callbackToken = token;
  const bool connected = connecttohost(url);
  _callbackToken = 0;
  if (connected){
    _status = PLAYING;
    netserver.requestOnChange(MODE, 0);
    setOutputPins(true);
    display.putRequest(PSTART);
    if (player_on_start_play) player_on_start_play();
  }else{
    serialCli.printf("##ERROR#:\tError connecting to %.128s\n", url);
    snprintf(config.tmpBuf, sizeof(config.tmpBuf), "Error connecting to %.128s", url); setError();
    const bool accepted = _temporary.signal(token);
#if defined(VOXONE_TTS_HIL_DIAGNOSTICS)
    voxone::tts_hil::diagnostics().connectFailed(
        token, millis(), transportProgressBytes(), httpBodyProgressBytes(),
        httpContentLength(), accepted);
#endif
  }
}

void Player::prev() {
  if (systemUpdateAudioBlocked()) return;
  uint16_t lastStation = config.lastStation();
  const uint16_t count = config.playlistLength();
  if (count == 0) return;
  if (lastStation <= 1) config.lastStation(count); else config.lastStation(lastStation-1);
  sendCommand({PR_PLAY, config.lastStation()});
}

void Player::next() {
  if (systemUpdateAudioBlocked()) return;
  uint16_t lastStation = config.lastStation();
  const uint16_t count = config.playlistLength();
  if (count == 0) return;
  if (lastStation == 0 || lastStation >= count) config.lastStation(1);
  else config.lastStation(lastStation+1);
  sendCommand({PR_PLAY, config.lastStation()});
}

void Player::toggle() {
  if (systemUpdateAudioBlocked()) return;
  if (sourceManagerRadioPlayIntent()) {
    sendCommand({PR_STOP, 0});
  } else {
    const uint16_t selected = config.lastStation();
    if (selected == 0 && config.playlistLength() == 0) return;
    sendCommand({PR_PLAY, selected == 0 ? 1 : selected});
  }
}

void Player::stepVol(bool up) {
  stepUserVol(up ? 1 : -1);
}

uint8_t Player::volToI2S(uint8_t volume) {
  int vol = map(volume, 0, 254 - config.station.ovol * 3 , 0, 254);
  return volumeClampOutput(vol, config.runtimeSnapshot().maximumVolume);
}

void Player::_loadVol(uint8_t volume) {
  setVolume(_mute.outputVolume(volToI2S(volume)));
}

void Player::setMuted(bool muted) {
  if (systemUpdateAudioBlocked()) return;
  bool changed = false;
  {
    Config::WriteLock configLock(config._persistMutex);
    portENTER_CRITICAL(&playerVolumeMux);
    if (_mute.active() != muted) {
      _mute.set(muted);
      if (!_volumePending) {
        _pendingMode = 3; // Refresh output gain without changing saved USER volume.
        _volumePending = true;
      }
      changed = true;
    }
    portEXIT_CRITICAL(&playerVolumeMux);
  }
  if (!changed) return;
  display.putRequest(DRAWVOL);
  netserver.requestOnChange(VOLUME, 0);
}

bool Player::outputSilent() const {
  return _mute.outputSilent();
}

PlayerRuntimeVolumeSnapshot Player::runtimeVolumeSnapshot() const {
  Config::WriteLock configLock(config._persistMutex);
  return {config._runtime.read(), _mute.outputSnapshot()};
}

void Player::setVol(uint8_t volume) {
  if (systemUpdateAudioBlocked()) return;
  _volTicks = millis();
  _volTimer = true;
  portENTER_CRITICAL(&playerVolumeMux);
  _pendingVolume = volume;
  _pendingMode = 0;
  _volumePending = true;
  portEXIT_CRITICAL(&playerVolumeMux);
}

void Player::setUserVol(uint8_t user) {
  if (systemUpdateAudioBlocked()) return;
  if (user > 100) user = 100;
  _volTicks = millis();
  _volTimer = true;
  Config::WriteLock configLock(config._persistMutex);
  portENTER_CRITICAL(&playerVolumeMux);
  config.userVolume = user;
  config.store.lastUserVolume = user;
  _mute.setUserVolume(user);
  _pendingVolume = user;
  _pendingMode = 1;
  _volumePending = true;
  portEXIT_CRITICAL(&playerVolumeMux);
  config._publishRuntimeLocked();
}

void Player::stepUserVol(int8_t direction) {
  if (systemUpdateAudioBlocked()) return;
  Config::WriteLock configLock(config._persistMutex);
  portENTER_CRITICAL(&playerVolumeMux);
  const uint8_t user = _mute.stepUserVolume(
      _mute.outputSnapshot().userVolume, direction);
  config.userVolume = user;
  config.store.lastUserVolume = user;
  _pendingVolume = user;
  _pendingMode = 1;
  _volumePending = true;
  portEXIT_CRITICAL(&playerVolumeMux);
  config._publishRuntimeLocked();
  _volTicks = millis();
  _volTimer = true;
}

void Player::applyCurrentVolume() {
  portENTER_CRITICAL(&playerVolumeMux);
  _pendingVolume = _mute.outputSnapshot().userVolume;
  _pendingMode = 2;
  _volumePending = true;
  portEXIT_CRITICAL(&playerVolumeMux);
}

uint8_t Player::pendingRawAtMax(uint8_t maximum) {
  uint8_t value;
  uint8_t mode;
  bool pending;
  portENTER_CRITICAL(&playerVolumeMux);
  value = _pendingVolume;
  mode = _pendingMode;
  pending = _volumePending;
  portEXIT_CRITICAL(&playerVolumeMux);
  if (!pending || mode == 2) return config.runtimeSnapshot().volume;
  if (mode == 1) return volumeUserToRaw(value, maximum);
  const uint8_t ceiling = volumeRawMaximum(maximum);
  return value > ceiling ? ceiling : value;
}

uint8_t Player::rawNotLouderThan(uint8_t candidate, uint8_t maximum, uint8_t output) {
  while (candidate > 0) {
    const int adjusted = map(candidate, 0, 254 - config.station.ovol * 3, 0, 254);
    if (volumeClampOutput(adjusted, maximum) <= output) break;
    --candidate;
  }
  return candidate;
}

void Player::requestMaximumVolume(uint8_t maximum) {
  if (maximum < 1 || maximum > 100) return;
  portENTER_CRITICAL(&playerVolumeMux);
  _pendingMaximum = maximum;
  _maximumPending = true;
  portEXIT_CRITICAL(&playerVolumeMux);
}
