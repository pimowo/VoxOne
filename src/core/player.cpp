#include "options.h"
#include "player.h"
#include "config.h"
#include "serialcli.h"
#include "display.h"
#include "netserver.h"
#include "timekeeper.h"
#include "volume_map.h"
#include "line_out_volume.h"
#include "audio_runtime_config.h"
#include "system_operation_state.h"
#include "source_manager.h"
#include "network.h"
#include "bt_audio_input.h"
#include "i2s_output_lock.h"
#include "ui_state.h"
#include "playlist_mapping.h"
#include "playlist_store.h"
#include "station_format.h"
#include "sd_card.h"
#include "../hardware/runtime_hardware_config.h"
#include <cstddef>
#include <cstdlib>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include "../displays/tools/l10n.h"
Player player;
QueueHandle_t playerQueue;
portMUX_TYPE playerVolumeMux = portMUX_INITIALIZER_UNLOCKED;

namespace {

SourceId sourceIdForMedia(voxone::source_media::Source source) {
  return source == voxone::source_media::Source::Dlna
             ? SourceId::Dlna
         : source == voxone::source_media::Source::Local
             ? SourceId::Local
             : SourceId::HomeAssistant;
}

struct QueuedSourceMediaRequest {
  voxone::source_media::Source source;
  bool returnToPrevious;
  uint16_t urlBytes;
  uint16_t titleBytes;
  uint16_t artistBytes;
  char data[1];

  voxone::source_media::Request view() const {
    const char* title = data + urlBytes;
    const char* artist = title + titleBytes;
    return voxone::source_media::Request(data, title, artist, source,
                                         returnToPrevious);
  }
};

QueuedSourceMediaRequest* copySourceMediaRequest(
    const voxone::source_media::Request& request) {
  const char* url = request.url ? request.url : "";
  const char* title = request.title ? request.title : "";
  const char* artist = request.artist ? request.artist : "";
  const size_t urlBytes = strlen(url) + 1;
  const size_t titleBytes = strlen(title) + 1;
  const size_t artistBytes = strlen(artist) + 1;
  if (urlBytes > voxone::source_media::kUrlCapacity ||
      titleBytes > voxone::source_media::kMetadataCapacity ||
      artistBytes > voxone::source_media::kMetadataCapacity)
    return nullptr;
  const size_t allocationBytes = offsetof(QueuedSourceMediaRequest, data) +
      urlBytes + titleBytes + artistBytes;
  void* memory = heap_caps_malloc(
      allocationBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) memory = std::malloc(allocationBytes);
  if (!memory) return nullptr;
  auto* copy = static_cast<QueuedSourceMediaRequest*>(memory);
  copy->source = request.source;
  copy->returnToPrevious = request.returnToPrevious;
  copy->urlBytes = static_cast<uint16_t>(urlBytes);
  copy->titleBytes = static_cast<uint16_t>(titleBytes);
  copy->artistBytes = static_cast<uint16_t>(artistBytes);
  memcpy(copy->data, url, urlBytes);
  memcpy(copy->data + urlBytes, title, titleBytes);
  memcpy(copy->data + urlBytes + titleBytes, artist, artistBytes);
  return copy;
}

void releaseSourceMediaRequest(QueuedSourceMediaRequest* request) {
  std::free(request);
}

}  // namespace

Player::Player(): Audio(voxone::hardware::currentHardware().audioOutInternal,
                        3, I2S_NUM_0, false) {}


void Player::init() {
  const auto runtime = config.runtimeSnapshot();
  Serial.print("##[BOOT]#\tplayer.init\t");
  playerQueue=NULL;
  _hasError=false;
  _pendingVolume = runtime.volume;
  _pendingMode = 0;
  _volumePending = false;
  _maximumPending = false;
  _mute.setUserVolume(runtime.userVolume);
  _effectiveOutputUser.store(
      voxone::line_out::effectiveUserVolume(runtime),
      std::memory_order_release);
  _radioReconnect.reset();
  _pendingStationRefresh.reset();
  voxone::hardware::AudioOutputKind outputKind =
      voxone::hardware::AudioOutputKind::None;
  voxone::hardware::runtimeAudioOutputKind(
      voxone::hardware::runtimeHardwareConfig().audio, outputKind);
  const auto audioPlan = voxone::audio_runtime::outputActivationPlan(
      outputKind,
      static_cast<AudioPath>(runtime.audioPath),
      voxone::hardware::hardwareCapabilities().supportsDspMini);
  _localAudioAvailable = audioPlan.localPlaybackAvailable;
  playerQueue = xQueueCreate( 5, sizeof( playerRequestParams_t ) );
  setOutputPins(false);
  delay(50);
  _sourceMediaUrls = xQueueCreate(1, sizeof(QueuedSourceMediaRequest*));
  if(MUTE_PIN!=255) pinMode(MUTE_PIN, OUTPUT);
  const auto& audioPins = voxone::hardware::currentHardware().audioOut;
  if (audioPlan.initializeSerialI2s &&
      (!voxone::hardware::hasPin(audioPins.dout) || !initializeI2S() ||
       (!voxone::hardware::currentHardware().audioOutInternal &&
        !setPinout(audioPins.bclk, audioPins.ws, audioPins.dout)))) {
      _localAudioAvailable = false;
      Serial.println("failed: local I2S unavailable");
  }
  Serial.printf("audio path: %s%s\n",
                voxone::audio_path::apiName(
                    static_cast<AudioPath>(runtime.audioPath)),
                audioPlan.dspMiniSelected ? " (I2S prepared, I2C pending)" : "");
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
  _loadVol(voxone::line_out::effectiveRawVolume(runtime));
  setConnectionTimeout(CONNECTION_TIMEOUT, CONNECTION_TIMEOUT_SSL);
  const bool taskStarted = startAudioTask();
  const auto inputBuffer = voxone::audio_runtime::currentInputBufferPlan();
  Serial.printf("done (decode task: %s, core=%u, priority=%u, buffer=%u+%u KiB)\n",
                taskStarted ? "started" : "main-loop fallback",
                static_cast<unsigned>(voxone::audio_runtime::kTaskCore),
                static_cast<unsigned>(voxone::audio_runtime::kTaskPriority),
                static_cast<unsigned>(inputBuffer.inputBytes / 1024U),
                static_cast<unsigned>(inputBuffer.reserveBytes / 1024U));
}

bool Player::ready() const {
  return playerQueue != nullptr && _sourceMediaUrls != nullptr;
}

void Player::sendCommand(playerRequestParams_t request){
  if(playerQueue==NULL) return;
  if(systemUpdateAudioBlocked() && request.type != PR_UPDATE_STOP) return;
  if (!_localAudioAvailable &&
      (request.type == PR_PLAY || request.type == PR_SOURCE_MEDIA ||
       request.type == PR_RADIO_RESUME ||
       request.type == PR_RADIO_NETWORK_ONLINE)) return;
  if (request.type == PR_STATION_REFRESH) {
    _pendingStationRefresh.publishRefresh();
    return;
  }
  const bool stopsSourceMedia = request.type == PR_STOP && sourceMediaBusy();
  if(xQueueSend(playerQueue, &request, PLQ_SEND_DELAY) != pdPASS) return;
  switch (request.type) {
    case PR_PLAY:
    case PR_STOP:
    case PR_SOURCE_MEDIA:
    case PR_RADIO_SUSPEND:
    case PR_RADIO_RESUME:
    case PR_UPDATE_STOP:
    case PR_RADIO_NETWORK_OFFLINE:
      cancelPendingConnection();
      break;
    default:
      break;
  }
  if(request.type == PR_PLAY ||
     (request.type == PR_STOP && !stopsSourceMedia))
    sourceManagerRadioCommandQueued(request.type == PR_PLAY,
                                   request.type == PR_PLAY && request.payload > 0
                                       ? static_cast<uint16_t>(request.payload) : 0);
}

void Player::resetQueue(){
  if(playerQueue!=NULL) xQueueReset(playerQueue);
}

void Player::clearSourceMediaRequests() {
  if (!_sourceMediaUrls) return;
  QueuedSourceMediaRequest* request = nullptr;
  while (xQueueReceive(_sourceMediaUrls, &request, 0) == pdTRUE) {
    releaseSourceMediaRequest(request);
    request = nullptr;
  }
}

bool Player::suppressSourceMediaReturn() {
  const bool wasPending = _sourceMediaReturnToPrevious ||
                          _sourceMediaReturnPending;
  _sourceMediaReturnToPrevious = false;
  _sourceMediaReturnPending = false;
  return wasPending;
}

void Player::stopInfo() {
  config.setSmartStart(0);
  netserver.requestOnChange(MODE, 0);
}

void Player::setError(){
  _sourceMedia.signal(_callbackToken);
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
  _pcmOutputPending = false;
  cancelPcmOutput();
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
    if (voxone::hardware::hasPin(
            voxone::hardware::currentHardware().audioOut.dout)) {
      voxone::i2s_output::Guard outputGuard(pdMS_TO_TICKS(100));
      if (outputGuard) i2s_zero_dma_buffer(I2S_NUM_0);
    }
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
  if (sourceMediaBusy()) {
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
  clearSourceMediaRequests();
  if (sourceMediaBusy()) {
    const SourceId previous = _sourceMediaPrevious;
    const bool restoreAfterUserStop =
        consumeUserCommand && suppressSourceMediaReturn();
    if (!consumeUserCommand) suppressSourceMediaReturn();
    if (sourceMediaActive()) {
      finishSourceMedia();
    }
    else if (systemUpdateAudioBlocked())
      _stop(false, RadioStopReason::SourceSwitch);
    if (restoreAfterUserStop)
      sourceManagerReturnAfterUserMediaStop(previous);
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
  if (playerQueue == nullptr) return;
  servicePendingVolume();
  if (_shutdownSuspended || !processPlayerCommands()) return;
  if (uxQueueMessagesWaiting(playerQueue) == 0)
    _servicePendingStationRefresh();
  serviceSourceMedia();
  servicePcmOutputActivation();
  serviceRadioReconnect();
  serviceVolumePersistence();
}

void Player::servicePendingVolume() {
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
    const auto updated = config.runtimeSnapshot();
    Audio::setVolume(_mute.outputVolume(
        volToI2S(voxone::line_out::effectiveRawVolume(updated))));
  }
}

bool Player::processPlayerCommands() {
  playerRequestParams_t requestP;
  if(xQueueReceive(playerQueue, &requestP, isRunning()?PL_QUEUE_TICKS:PL_QUEUE_TICKS_ST)){
    if (systemUpdateAudioBlocked() && requestP.type != PR_UPDATE_STOP)
      return false;
    switch (requestP.type){
      case PR_UPDATE_STOP:
        _radioReconnect.cancel();
        clearSourceMediaRequests();
        suppressSourceMediaReturn();
        if (sourceMediaActive()) {
          finishSourceMedia();
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
        if (!sourceMediaBusy()) _stop(false, RadioStopReason::SourceSwitch);
        break;
      case PR_RADIO_RESUME:
        _radioReconnect.cancel();
        if (sourceMediaBusy()) break;
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
        if (!sourceMediaBusy()) _stop(false, RadioStopReason::SourceSwitch);
        scheduleRadioReconnect();
        serialCli.printf("##[RADIO]# network offline, reconnect suspended\n");
        break;
      case PR_RADIO_NETWORK_ONLINE:
        if (sourceMediaBusy() || systemUpdateAudioBlocked() ||
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
        if (sourceMediaBusy()) break;
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
        if (config.runtimeSnapshot().lineOutEnabled) break;
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
      case PR_SOURCE_MEDIA: {
        // Wake-up only. URL bytes are owned by the one-slot mailbox below.
        break;
      }
          
      default: break;
    }
  }
  return true;
}

void Player::serviceSourceMedia() {
  QueuedSourceMediaRequest* mediaRequest = nullptr;
  if (_sourceMediaUrls &&
      xQueueReceive(_sourceMediaUrls, &mediaRequest, 0) == pdTRUE &&
      mediaRequest) {
    if (!systemUpdateAudioBlocked()) playSourceMedia(mediaRequest->view());
    releaseSourceMediaRequest(mediaRequest);
  }
  // Backend EOF/error callbacks run synchronously inside these calls. They
  // signal this invocation's token; never reconnect from a decoder callback.
  _callbackToken = _sourceMedia.token();
  if (!_sourceMedia.terminal()) Audio::loop();
  _callbackToken = 0;
  if (_radioReconnect.failures() && audioStreamStarted()) {
    serialCli.printf("##[RADIO]# stream recovered\n");
    _radioReconnect.reset();
  }
  if (sourceMediaActive()) {
    voxone::source_media::serviceSourceMediaTransport(
        _sourceMedia, _sourceMediaProgress, millis(), transportProgressBytes(),
        isRunning(), transportConnected(), transportBodyComplete(),
        transportBufferedBytes(), transportBufferedFrameAvailable(),
        [this](uint32_t token) {
          serialCli.printf(
              "##[SOURCE-MEDIA]# transport stalled token=%lu timeout=%lu ms\n",
              static_cast<unsigned long>(token),
              static_cast<unsigned long>(
                  voxone::source_media::kInactivityTimeoutMs));
        },
        [this](uint32_t token) {
          serialCli.printf(
              "##[SOURCE-MEDIA]# transport disconnected before EOF token=%lu\n",
              static_cast<unsigned long>(token));
        },
        [this]() { finishSourceMedia(); });
  } else if (!isRunning() && _status==PLAYING) {
    _stop(true, RadioStopReason::SourceSwitch);
    scheduleRadioReconnect();
  }
  if (_sourceMediaReturnPending && uxQueueMessagesWaiting(playerQueue) == 0 &&
      (!_sourceMediaUrls || uxQueueMessagesWaiting(_sourceMediaUrls) == 0))
    restoreSourceMediaBase();
}

void Player::servicePcmOutputActivation() {
  if (!_pcmOutputPending || !takePcmOutputReady()) return;
  _pcmOutputPending = false;
  if (_status != PLAYING || systemUpdateAudioBlocked()) return;
  setOutputPins(true);
  if (player_on_start_play) player_on_start_play();
}

void Player::serviceVolumePersistence() {
  if (_volTimer && millis() - _volTicks > 3000) {
    config.saveVolume();
    _volTimer = false;
  }
}

void Player::stopForShutdown() {
  if (_shutdownSuspended) return;
  _shutdownResumeRadio = sourceManagerRadioResumeAllowed();
  _shutdownSuspended = true;
  _radioReconnect.cancel();
  cancelPendingConnection();
  resetQueue();
  clearSourceMediaRequests();
  if (sourceMediaBusy()) {
    suppressSourceMediaReturn();
    finishSourceMedia();
  }

  // Silence the DAC synchronously before the display task can begin the
  // goodbye animation. The normal pending-volume path keeps mute state and
  // Web/API reporting consistent on the following loop iteration.
  setMuted(true);
  Audio::setVolume(0);
  _stop(false, RadioStopReason::SourceSwitch, true);
  stopAudioTask();
  if (voxone::hardware::hasPin(
          voxone::hardware::currentHardware().audioOut.dout)) {
    voxone::i2s_output::Guard outputGuard(pdMS_TO_TICKS(100));
    if (outputGuard) i2s_zero_dma_buffer(I2S_NUM_0);
  }
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
  _loadVol(voxone::line_out::effectiveRawVolume(config.runtimeSnapshot()));
  _sourceMedia.allowRadioMetadata();
  
  bool isConnected = false;
  connproc = false;
  armPcmOutput();
  _pcmOutputPending = true;
  isConnected=connecttohost(config.station.url);
  connproc = true;
  if(isConnected){
    _status = PLAYING;
    config.configPostPlaying();
  }else{
    _pcmOutputPending = false;
    cancelPcmOutput();
    serialCli.printf("##ERROR#:\tError connecting to %.128s\n", config.station.url);
    snprintf(config.tmpBuf, sizeof(config.tmpBuf), "Error connecting to %.128s", config.station.url); setError();
    _stop(true, RadioStopReason::SourceSwitch);
    scheduleRadioReconnect();
  };
}

void Player::scheduleRadioReconnect() {
  if (sourceMediaBusy() || systemUpdateAudioBlocked() ||
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
  const bool allowed = !sourceMediaBusy() && !systemUpdateAudioBlocked() &&
                       sourceManagerRadioResumeAllowed();
  if (!_radioReconnect.takeDue(millis(), allowed, network.ready())) return;
  serialCli.printf("##[RADIO]# reconnect attempt=%u\n",
                   static_cast<unsigned>(_radioReconnect.failures()));
  _play(config.lastStation(), true);
}

void Player::requestSourceMedia(
    const voxone::source_media::Request& request) {
  const char* url = request.url ? request.url : "";
  const size_t length = strlen(url);
  const SourceId requestedSource = sourceIdForMedia(request.source);
  const bool validResource =
      request.source == voxone::source_media::Source::Local
          ? voxone::sd_path::audioFile(url)
          : voxone::source_media::isSafeUrl(url, length);
  if (!_sourceMediaUrls ||
      !validResource ||
      !sourceManagerSourceAvailable(requestedSource) ||
      systemUpdateAudioBlocked() || !_localAudioAvailable) return;
  QueuedSourceMediaRequest* pending = copySourceMediaRequest(request);
  if (!pending) {
    serialCli.printf("##[SOURCE-MEDIA]# request allocation failed\n");
    return;
  }
  // Producers may run on different tasks. The one-slot queue stores only an
  // owning pointer; when full, remove and release the older pending request.
  // A request already consumed by Player owns its allocation until playSourceMedia
  // returns, so replacement can never invalidate connecttohost() input.
  bool queued = false;
  for (uint8_t attempt = 0; attempt < 4 && !queued; ++attempt) {
    queued = xQueueSend(_sourceMediaUrls, &pending, 0) == pdPASS;
    if (queued) break;
    QueuedSourceMediaRequest* replaced = nullptr;
    if (xQueueReceive(_sourceMediaUrls, &replaced, 0) == pdTRUE)
      releaseSourceMediaRequest(replaced);
    else
      taskYIELD();
  }
  if (!queued) {
    releaseSourceMediaRequest(pending);
    serialCli.printf("##[SOURCE-MEDIA]# request mailbox busy\n");
    return;
  }
  sendCommand({PR_SOURCE_MEDIA, 0});
}

bool Player::sourceMediaEof() {
  if (!_callbackToken) return false;
  _sourceMedia.signal(_callbackToken);
  return true;
}

bool Player::interruptSourceMediaForNetwork() {
  const bool busy = sourceMediaBusy();
  const uint32_t token = _sourceMedia.token();
  _sourceMedia.signal(token);
  return busy;
}

void Player::finishSourceMedia() {
  const uint32_t token = _sourceMedia.token();
  if (!token) return;
  _callbackToken = 0;
  // Stop/clear Player while it still owns I2S0. This is not a user RADIO STOP.
  _stop(false, RadioStopReason::SourceSwitch, true);
  remoteStationName = false;
  if (!_sourceMedia.finish(token)) return;
  sourceManagerEndMedia();
  _sourceMediaProgress.finish(token);
  _sourceMediaReturnPending = _sourceMediaReturnToPrevious;
  _sourceMediaReturnToPrevious = false;
  // Publish stopped playback for the selected source. An explicitly requested
  // return is applied by the main loop after queued commands have drained.
  netserver.requestOnChange(MODE, 0);
  netserver.requestOnChange(WEBSTATUS, 0);
  display.putRequest(NEWSTATION);
  display.putRequest(NEWTITLE);
}

void Player::restoreSourceMediaBase() {
  if (!_sourceMediaReturnPending || sourceMediaActive()) return;
  if (systemUpdateAudioBlocked()) {
    _sourceMediaReturnPending = false;
    return;
  }
  if (_sourceMediaPrevious == SourceId::Web && !WiFi.isConnected()) return;
  const SourceId previous = _sourceMediaPrevious;
  _sourceMediaReturnPending = false;
  sourceManagerSelectSource(previous);
}

void Player::playSourceMedia(const voxone::source_media::Request& request){
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE && VOXONE_BT_I2S_RX_ENABLED
  if (!btAudioInput.acquirePlayerOutput()) {
    serialCli.printf("##ERROR#:\tSource media cannot acquire I2S0\n");
    return;
  }
#endif
  _callbackToken = 0;
  const bool replacing = sourceMediaActive();
  if (!replacing) _sourceMediaPrevious = sourceManagerActiveSource();
  _sourceMediaReturnPending = false;
  _sourceMediaReturnToPrevious = request.returnToPrevious;
  const uint32_t token = _sourceMedia.begin();
  sourceManagerBeginMedia(sourceIdForMedia(request.source), request.title,
                          request.artist);
  _sourceMediaProgress.begin(token, millis(), transportProgressBytes());
  stopSong();
  _pcmOutputPending = false;
  network.lostPlaying = false;
  _hasError=false;
  remoteStationName = true;
  config.setDspOn(1);
  display.putRequest(PSTOP);
  setOutputPins(false);
  armPcmOutput();
  _pcmOutputPending = true;
  _callbackToken = token;
  fs::FS* localFs = request.source == voxone::source_media::Source::Local
                        ? voxone::sd_card::filesystem() : nullptr;
  const bool connected = localFs ? connecttoFS(*localFs, request.url)
                                 : connecttohost(request.url);
  _callbackToken = 0;
  if (connected){
    _status = PLAYING;
    netserver.requestOnChange(MODE, 0);
    display.putRequest(PSTART);
  }else{
    _pcmOutputPending = false;
    cancelPcmOutput();
    serialCli.printf("##ERROR#:\tError connecting to %.128s\n", request.url);
    snprintf(config.tmpBuf, sizeof(config.tmpBuf),
             "Error connecting to %.128s", request.url); setError();
    _sourceMedia.signal(token);
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
  const auto runtime = config.runtimeSnapshot();
  if (runtime.lineOutEnabled)
    return volume > 254 ? 254 : volume;
  int vol = map(volume, 0, 254 - config.station.ovol * 3 , 0, 254);
  return volumeClampOutput(vol, runtime.maximumVolume);
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
  return _mute.active() ||
         _effectiveOutputUser.load(std::memory_order_acquire) == 0;
}

PlayerRuntimeVolumeSnapshot Player::runtimeVolumeSnapshot() const {
  Config::WriteLock configLock(config._persistMutex);
  return {config._runtime.read(), _mute.outputSnapshot()};
}

void Player::setVol(uint8_t volume) {
  if (systemUpdateAudioBlocked() ||
      config.runtimeSnapshot().lineOutEnabled) return;
  _volTicks = millis();
  _volTimer = true;
  portENTER_CRITICAL(&playerVolumeMux);
  _pendingVolume = volume;
  _pendingMode = 0;
  _volumePending = true;
  portEXIT_CRITICAL(&playerVolumeMux);
}

void Player::setUserVol(uint8_t user) {
  if (systemUpdateAudioBlocked() ||
      config.runtimeSnapshot().lineOutEnabled) return;
  if (user > 100) user = 100;
  _volTicks = millis();
  _volTimer = true;
  Config::WriteLock configLock(config._persistMutex);
  portENTER_CRITICAL(&playerVolumeMux);
  config.userVolume = user;
  config.store.lastUserVolume = user;
  _mute.setUserVolume(user);
  _effectiveOutputUser.store(user, std::memory_order_release);
  _pendingVolume = user;
  _pendingMode = 1;
  _volumePending = true;
  portEXIT_CRITICAL(&playerVolumeMux);
  config._publishRuntimeLocked();
}

void Player::stepUserVol(int8_t direction) {
  if (systemUpdateAudioBlocked() ||
      config.runtimeSnapshot().lineOutEnabled) return;
  Config::WriteLock configLock(config._persistMutex);
  portENTER_CRITICAL(&playerVolumeMux);
  const uint8_t user = _mute.stepUserVolume(
      _mute.outputSnapshot().userVolume, direction);
  config.userVolume = user;
  config.store.lastUserVolume = user;
  _effectiveOutputUser.store(user, std::memory_order_release);
  _pendingVolume = user;
  _pendingMode = 1;
  _volumePending = true;
  portEXIT_CRITICAL(&playerVolumeMux);
  config._publishRuntimeLocked();
  _volTicks = millis();
  _volTimer = true;
}

void Player::applyCurrentVolume() {
  const auto runtime = config.runtimeSnapshot();
  _effectiveOutputUser.store(
      voxone::line_out::effectiveUserVolume(runtime),
      std::memory_order_release);
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
