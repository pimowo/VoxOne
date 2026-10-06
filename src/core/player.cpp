#include "options.h"
#include "player.h"
#include "config.h"
#include "serialcli.h"
#include "display.h"
#include "sdmanager.h"
#include "netserver.h"
#include "timekeeper.h"
#include "volume_map.h"
#include "system_operation_state.h"
#include "source_manager.h"
#include "network.h"
#include "bt_audio_input.h"
#if I2S_DOUT!=255 && VS1053_CS==255
#include <driver/i2s.h>
#endif
#include "../displays/tools/l10n.h"
#include "../pluginsManager/pluginsManager.h"
#ifdef USE_NEXTION
#include "../displays/nextion.h"
#endif
Player player;
QueueHandle_t playerQueue;
portMUX_TYPE playerVolumeMux = portMUX_INITIALIZER_UNLOCKED;

#if VS1053_CS!=255 && !I2S_INTERNAL
  #if VS_HSPI
    Player::Player(): Audio(VS1053_CS, VS1053_DCS, VS1053_DREQ, &SPI2) {}
  #else
    Player::Player(): Audio(VS1053_CS, VS1053_DCS, VS1053_DREQ, &SPI) {}
  #endif
  void ResetChip(){
    pinMode(VS1053_RST, OUTPUT);
    digitalWrite(VS1053_RST, LOW);
    delay(30);
    digitalWrite(VS1053_RST, HIGH);
    delay(100);
  }
#else
  #if !I2S_INTERNAL
    Player::Player() {}
  #else
    Player::Player(): Audio(true, I2S_DAC_CHANNEL_BOTH_EN)  {}
  #endif
#endif


void Player::init() {
  Serial.print("##[BOOT]#\tplayer.init\t");
  playerQueue=NULL;
  _resumeFilePos = 0;
  _hasError=false;
  _pendingVolume = config.store.volume;
  _pendingMode = 0;
  _volumePending = false;
  _maximumPending = false;
  playerQueue = xQueueCreate( 5, sizeof( playerRequestParams_t ) );
  setOutputPins(false);
  delay(50);
  _temporaryUrls = xQueueCreate(1, MQTT_BURL_SIZE);
  if(MUTE_PIN!=255) pinMode(MUTE_PIN, OUTPUT);
  #if I2S_DOUT!=255
    #if !I2S_INTERNAL
      setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    #endif
  #else
    SPI.begin();
    if(VS1053_RST>0) ResetChip();
    begin();
  #endif
  setBalance(config.store.balance);
  setTone(config.store.bass, config.store.middle, config.store.trebble);
  setVolume(0);
  _status = STOPPED;
  _volTimer=config.volumeBootDirty;
  if (_volTimer) _volTicks = millis();
  //randomSeed(analogRead(0));
  #if PLAYER_FORCE_MONO
    forceMono(true);
  #endif
  _loadVol(config.store.volume);
  setConnectionTimeout(CONNECTION_TIMEOUT, CONNECTION_TIMEOUT_SSL);
  Serial.println("done");
}

void Player::sendCommand(playerRequestParams_t request){
  if(playerQueue==NULL) return;
  if(systemUpdateAudioBlocked() && request.type != PR_STOP &&
     request.type != PR_RADIO_SUSPEND) return;
  if(xQueueSend(playerQueue, &request, PLQ_SEND_DELAY) != pdPASS) return;
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
  _hasError=true;
  _temporary.signal(_callbackToken);
  config.setTitle(config.tmpBuf);
  serialCli.printf("##ERROR#:\t%s\n", config.tmpBuf);
}

void Player::setError(const char *e){
  strlcpy(config.tmpBuf, e, sizeof(config.tmpBuf));
  setError();
}

void Player::_stop(bool alreadyStopped, RadioStopReason reason){
  log_i("%s called", __func__);
  if(config.getMode()==PM_SDCARD && !alreadyStopped && !temporaryActive())
    config.sdResumePos = player.getFilePos();
  _status = STOPPED;
  setOutputPins(false);
  if(!_hasError) config.setTitle((display.mode()==LOST || display.mode()==UPDATING)?"":LANG::const_PlStopped);
  config.station.bitrate = 0;
  config.setBitrateFormat(BF_UNKNOWN);
  #ifdef USE_NEXTION
    nextion.bitrate(config.station.bitrate);
  #endif
  setDefaults();
  if(!alreadyStopped) stopSong();
  if (systemUpdateAudioBlocked()) {
#if I2S_DOUT!=255 && VS1053_CS==255
    i2s_zero_dma_buffer(I2S_NUM_0);
#endif
    systemUpdateRadioStopped();
  }
  netserver.requestOnChange(BITRATE, 0);
  display.putRequest(DBITRATE);
  display.putRequest(PSTOP);
  //setDefaults();
  //if(!alreadyStopped) stopSong();
  if(radioStopUpdatesSmartStart(reason, lockOutput)) stopInfo();
  if (player_on_stop_play) player_on_stop_play();
  pm.on_stop_play();
}

void Player::initHeaders(const char *file) {
  if(strlen(file)==0 || true) return; //TODO Read TAGs
  connecttoFS(sdman,file);
  eofHeader = false;
  while(!eofHeader) Audio::loop();
  //netserver.requestOnChange(SDPOS, 0);
  setDefaults();
}
void resetPlayer(){
  if(!config.store.watchdog) return;
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
    if (pendingMode == 1) {
      const VolumeState state = volumeStateFromUser(pendingVolume, config.store.maximumVolume);
      config.setVolumeState(state.raw, state.user);
    } else if (pendingMode == 0) {
      const VolumeState state = volumeStateFromRaw(pendingVolume, config.store.maximumVolume);
      config.setVolumeState(state.raw, state.user);
    } else if (pendingMode == 2) config.setVolumeState(config.store.volume, config.userVolume);
    Audio::setVolume(_mute.outputVolume(volToI2S(config.store.volume)));
  }
  playerRequestParams_t requestP;
  if(xQueueReceive(playerQueue, &requestP, isRunning()?PL_QUEUE_TICKS:PL_QUEUE_TICKS_ST)){
    if (systemUpdateAudioBlocked() && requestP.type != PR_STOP &&
        requestP.type != PR_RADIO_SUSPEND) return;
    switch (requestP.type){
      case PR_STOP:
        if (_temporaryUrls) xQueueReset(_temporaryUrls);
        if (temporaryBusy()) {
          if (temporaryActive()) finishTemporary();
          else if (systemUpdateAudioBlocked())
            _stop(false, RadioStopReason::SourceSwitch);
          if (!sourceManagerRadioPlayIntent()) stopInfo();
        } else {
          _stop();
        }
        sourceManagerRadioStopConsumed();
        break;
      case PR_RADIO_SUSPEND:
        if (!temporaryBusy()) _stop(false, RadioStopReason::SourceSwitch);
        break;
      case PR_RADIO_RESUME:
        if (temporaryBusy()) break;
        if (sourceManagerRadioResumeAllowed()) {
          if (requestP.payload > 0 &&
              config.lastStation() != static_cast<uint16_t>(requestP.payload))
            config.setLastStation(static_cast<uint16_t>(requestP.payload));
          sourceManagerRadioPlayConsumed();
          _play(static_cast<uint16_t>(requestP.payload), true);
        }
        break;
      case PR_PLAY: {
        // Accepted commands already updated canonical intent/station. Let the
        // announcement finish, then use that current intent exactly once.
        if (temporaryBusy()) break;
        if (requestP.payload>0) {
          config.setLastStation((uint16_t)requestP.payload);
        }
        sourceManagerRadioPlayConsumed();
        _play((uint16_t)abs(requestP.payload)); 
        if (player_on_station_change) player_on_station_change(); 
        pm.on_station_change();
        break;
      }
      case PR_TOGGLE: {
        toggle();
        break;
      }
      case PR_VOL: {
        const uint8_t requested = static_cast<uint8_t>(constrain(requestP.payload, 0, 254));
        const VolumeState state = volumeStateFromRaw(requested, config.store.maximumVolume);
        config.setVolumeState(state.raw, state.user);
        Audio::setVolume(_mute.outputVolume(volToI2S(state.raw)));
        _volTicks = millis();
        _volTimer = true;
        break;
      }
      #ifdef USE_SD
      case PR_CHECKSD: {
        if(config.getMode()==PM_SDCARD){
          if(!sdman.cardPresent()){
            sdman.stop();
            config.changeMode(PM_WEB);
          }
        }
        break;
      }
      #endif
      case PR_VUTONUS: {
        if(config.vuThreshold>10) config.vuThreshold -=10;
        break;
      }
      case PR_BURL: {
        // Wake-up only. URL bytes are owned by the one-slot mailbox below.
        break;
      }
          
      default: break;
    }
  }
  char url[MQTT_BURL_SIZE];
  if (_temporaryUrls && xQueueReceive(_temporaryUrls, url, 0) == pdTRUE &&
      !systemUpdateAudioBlocked()) browseUrl(url);
  // Backend EOF/error callbacks run synchronously inside these calls. They
  // signal this invocation's token; never reconnect from a decoder callback.
  _callbackToken = _temporary.token();
  if (!_temporary.terminal()) Audio::loop();
  _callbackToken = 0;
  if (temporaryActive()) {
    if (_temporary.terminal() || !isRunning()) finishTemporary();
  } else if (!isRunning() && _status==PLAYING) {
    _stop(true);
  }
  if (uxQueueMessagesWaiting(playerQueue) == 0 &&
      (!_temporaryUrls || uxQueueMessagesWaiting(_temporaryUrls) == 0))
    restoreTemporaryBase();
  if(_volTimer){
    if((millis()-_volTicks)>3000){
      config.saveVolume();
      _volTimer=false;
    }
  }
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
  _loadVol(config.store.volume);
  
  bool isConnected = false;
  if(config.getMode()==PM_SDCARD && SDC_CS!=255){
    isConnected=connecttoFS(sdman,config.station.url,config.sdResumePos==0?_resumeFilePos:config.sdResumePos-player.sd_min);
  }else {
    config.saveValue(&config.store.play_mode, static_cast<uint8_t>(PM_WEB));
  }
  connproc = false;
  if(config.getMode()==PM_WEB) isConnected=connecttohost(config.station.url);
  connproc = true;
  if(isConnected){
    _status = PLAYING;
    config.configPostPlaying(stationId);
    setOutputPins(true);
    if (player_on_start_play) player_on_start_play();
    pm.on_start_play();
  }else{
    serialCli.printf("##ERROR#:\tError connecting to %.128s\n", config.station.url);
    snprintf(config.tmpBuf, sizeof(config.tmpBuf), "Error connecting to %.128s", config.station.url); setError();
    _stop(true, sourceResume ? RadioStopReason::SourceSwitch
                             : RadioStopReason::Normal);
  };
}

void Player::requestTemporaryUrl(const char* url, size_t length) {
  if (!_temporaryUrls || !url || !length || length >= MQTT_BURL_SIZE ||
      systemUpdateAudioBlocked()) return;
  char request[MQTT_BURL_SIZE]{};
  memcpy(request, url, length);
  // MQTT runs on another task. Replace the pending URL atomically, never a
  // buffer currently used by connecttohost(). Latest pending request wins.
  xQueueOverwrite(_temporaryUrls, request);
  sendCommand({PR_BURL, 0});
}

bool Player::temporaryEof() {
  if (!_callbackToken) return false;
  _temporary.signal(_callbackToken);
  return true;
}

bool Player::interruptTemporaryForNetwork() {
  const bool busy = temporaryBusy();
  _temporary.signal(_temporary.token());
  return busy;
}

void Player::finishTemporary() {
  const uint32_t token = _temporary.token();
  if (!token) return;
  _callbackToken = 0;
  // Stop/clear Player while it still owns I2S0. This is not a user RADIO STOP.
  _stop(false, RadioStopReason::SourceSwitch);
  remoteStationName = false;
  _temporary.finish(token);
}

void Player::restoreTemporaryBase() {
  if (!temporaryBusy() || temporaryActive()) return;
  uint16_t station = config.lastStation();
  if (!sourceManagerTakeTemporaryRestore(_temporary,
          WiFi.isConnected() || config.getMode() == PM_SDCARD,
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
  const bool first = !temporaryActive();
  _callbackToken = 0;
  if (first && config.getMode() == PM_SDCARD && isRunning()) {
    const uint32_t position = getFilePos();
    _resumeFilePos = position >= sd_min ? position - sd_min : 0;
    config.sdResumePos = 0;
  }
  // Replacement invalidates the previous token without changing the base.
  const uint32_t token = _temporary.begin();
  stopSong();
  network.lostPlaying = false;
  _hasError=false;
  remoteStationName = true;
  config.setDspOn(1);
  display.putRequest(PSTOP);
  setOutputPins(false);
  config.setTitle(LANG::const_PlConnect);
  _callbackToken = token;
  const bool connected = connecttohost(url);
  _callbackToken = 0;
  if (connected){
    _status = PLAYING;
    config.setTitle("");
    netserver.requestOnChange(MODE, 0);
    setOutputPins(true);
    display.putRequest(PSTART);
    if (player_on_start_play) player_on_start_play();
    pm.on_start_play();
  }else{
    serialCli.printf("##ERROR#:\tError connecting to %.128s\n", url);
    snprintf(config.tmpBuf, sizeof(config.tmpBuf), "Error connecting to %.128s", url); setError();
    _temporary.signal(token);
  }
}

void Player::prev() {
  uint16_t lastStation = config.lastStation();
  const uint16_t count = config.playlistLength();
  if (count == 0) return;
  if(config.getMode()==PM_WEB || !config.store.sdsnuffle){
    if (lastStation <= 1) config.lastStation(count); else config.lastStation(lastStation-1);
  }
  sendCommand({PR_PLAY, config.lastStation()});
}

void Player::next() {
  uint16_t lastStation = config.lastStation();
  const uint16_t count = config.playlistLength();
  if (count == 0) return;
  if(config.getMode()==PM_WEB || !config.store.sdsnuffle){
    if (lastStation == 0 || lastStation >= count) config.lastStation(1);
    else config.lastStation(lastStation+1);
  }else{
    config.lastStation(random(1, count));
  }
  sendCommand({PR_PLAY, config.lastStation()});
}

void Player::toggle() {
  if (temporaryBusy() ? sourceManagerRadioPlayIntent() : _status == PLAYING) {
    sendCommand({PR_STOP, 0});
  } else {
    const uint16_t selected = config.lastStation();
    if (selected == 0 && config.playlistLength() == 0) return;
    sendCommand({PR_PLAY, selected == 0 ? 1 : selected});
  }
}

void Player::stepVol(bool up) {
  if (up) {
    if (config.store.volume <= 254 - config.store.volsteps) {
      setVol(config.store.volume + config.store.volsteps);
    }else{
      setVol(254);
    }
  } else {
    if (config.store.volume >= config.store.volsteps) {
      setVol(config.store.volume - config.store.volsteps);
    }else{
      setVol(0);
    }
  }
}

uint8_t Player::volToI2S(uint8_t volume) {
  int vol = map(volume, 0, 254 - config.station.ovol * 3 , 0, 254);
  return volumeClampOutput(vol, config.store.maximumVolume);
}

void Player::_loadVol(uint8_t volume) {
  setVolume(_mute.outputVolume(volToI2S(volume)));
}

void Player::setMuted(bool muted) {
  if (_mute.active() == muted) return;
  _mute.set(muted);
  portENTER_CRITICAL(&playerVolumeMux);
  if (!_volumePending) {
    _pendingMode = 3; // Refresh output gain without changing saved USER volume.
    _volumePending = true;
  }
  portEXIT_CRITICAL(&playerVolumeMux);
  display.putRequest(DRAWVOL);
}

bool Player::outputSilent() const {
  return _mute.outputSilent(config.userVolume);
}

void Player::setVol(uint8_t volume) {
  _volTicks = millis();
  _volTimer = true;
  portENTER_CRITICAL(&playerVolumeMux);
  _pendingVolume = volume;
  _pendingMode = 0;
  _volumePending = true;
  portEXIT_CRITICAL(&playerVolumeMux);
}

void Player::setUserVol(uint8_t user) {
  if (user > 100) user = 100;
  _volTicks = millis();
  _volTimer = true;
  portENTER_CRITICAL(&playerVolumeMux);
  config.userVolume = user;
  config.store.lastUserVolume = user;
  _pendingVolume = user;
  _pendingMode = 1;
  _volumePending = true;
  portEXIT_CRITICAL(&playerVolumeMux);
}

void Player::stepUserVol(int8_t direction) {
  portENTER_CRITICAL(&playerVolumeMux);
  const uint8_t user = _mute.stepUserVolume(config.userVolume, direction);
  config.userVolume = user;
  config.store.lastUserVolume = user;
  _pendingVolume = user;
  _pendingMode = 1;
  _volumePending = true;
  portEXIT_CRITICAL(&playerVolumeMux);
  _volTicks = millis();
  _volTimer = true;
}

void Player::applyCurrentVolume() {
  portENTER_CRITICAL(&playerVolumeMux);
  _pendingVolume = config.userVolume;
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
  if (!pending || mode == 2) return config.store.volume;
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
