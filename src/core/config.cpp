#include "options.h"
#include "config.h"
#include "config_startup.h"
#include "bt_runtime.h"
#include "../hardware/hardware_descriptor.h"
#include "playlist_store.h"
#include "station_format.h"
#include "display.h"
#include "player.h"
#include "network.h"
#include "netserver.h"
#if defined(VOXONE_PROFILE_A0)
#include "dsp_transport_runtime.h"
#endif
#include "controls.h"
#include "timekeeper.h"
#include "serialcli.h"
#include "rtcsupport.h"
#include "volume_map.h"
#include "mqtt_config.h"
#include "ui_timeout_config.h"
#include "www_readiness.h"
#include "ap_wifi_recovery.h"
#include "../displays/tools/l10n.h"
#include <cstddef>
#include <nvs.h>
#if defined(VOXONE_PROFILE_C0)
#include <esp_system.h>
#endif

namespace {
static_assert(CONFIG_VERSION == voxone::config_format::kConfigV7,
              "Persistence adapter expects v7");
static_assert(STARTUP_LAST == 0, "Startup defaults must match STARTUP_LAST");
static_assert(EEPROM_SIZE - EEPROM_START == voxone::config_format::kConfigEepromCapacity,
              "Config area size changed");
constexpr char WARSAW_TZ[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";
constexpr char DEFAULT_NTP_1[] = "0.pl.pool.ntp.org";
constexpr char DEFAULT_NTP_2[] = "1.pl.pool.ntp.org";
constexpr int8_t TONE_MIN = -6;
constexpr int8_t TONE_MAX = 6;
constexpr int8_t BALANCE_MIN = -16;
constexpr int8_t BALANCE_MAX = 16;

// The existing Arduino EEPROM object uses namespace/key "eeprom".
// Read-back uses that same blob, not a second configuration store and not
// EEPROM.read(), which only returns the write cache in this framework.
struct ConfigEepromStorage {
  bool writeRecord(const uint8_t* bytes, size_t size) {
    if (size != voxone::config_format::kConfigV7SerializedSize ||
        EEPROM.length() != EEPROM_SIZE) return false;
    for (size_t i = 0; i < size; ++i) EEPROM.write(EEPROM_START + i, bytes[i]);
    return true;
  }
  bool commit() { return EEPROM.commit(); }
  bool readRecord(uint8_t* bytes, size_t size) {
    if (size != voxone::config_format::kConfigV7SerializedSize) return false;
    nvs_handle_t handle;
    if (nvs_open("eeprom", NVS_READONLY, &handle) != ESP_OK) return false;
    uint8_t stored[EEPROM_SIZE];
    size_t length = sizeof(stored);
    const esp_err_t result = nvs_get_blob(handle, "eeprom", stored, &length);
    nvs_close(handle);
    if (result != ESP_OK || length != sizeof(stored)) return false;
    memcpy(bytes, stored + EEPROM_START, size);
    return true;
  }
};

int8_t clampTone(int8_t value) {
  return constrain(value, TONE_MIN, TONE_MAX);
}

int8_t clampBalance(int8_t value) {
  return constrain(value, BALANCE_MIN, BALANCE_MAX);
}
}

#if DSP_MODEL==DSP_DUMMY
#define DUMMYDISPLAY
#endif

Config config;

void u8fix(char *src){
  char last = src[strlen(src)-1]; 
  if ((uint8_t)last >= 0xC2) src[strlen(src)-1]='\0';
}

bool Config::_hasCurrentWwwAssets() {
  return voxone::currentWwwAssetsReady([](const char* path) {
    return SPIFFS.exists(path);
  });
}

void Config::init() {
  voxone::config_format::ConfigStartupReadOnlyScope readOnly(_startupReadOnly);
  if (!_persistMutex) _persistMutex = xSemaphoreCreateRecursiveMutex();
  const bool eepromReady = _persistMutex && EEPROM.begin(EEPROM_SIZE);
  mqttConfig();
  uiTimeoutConfig();
  screensaverTicks = 0;
  screensaverPlayingTicks = 0;
  isScreensaver = false;
  memset(tmpBuf, 0, BUFLEN);
  //bootInfo();
#if RTCSUPPORTED
  _rtcFound = false;
  BOOTLOG("RTC begin(SDA=%d,SCL=%d)", RTC_SDA, RTC_SCL);
  if(rtc.init()){
    BOOTLOG("done");
    _rtcFound = true;
  }else{
    BOOTLOG("[ERROR] - Couldn't find RTC");
  }
#endif
  spiffsMounted = false;
  currentWwwReady = false;
  radioPlaylistReady = false;
  uint8_t configArea[EEPROM_SIZE - EEPROM_START]{};
  if (eepromReady) {
    for (size_t i = 0; i < sizeof(configArea); ++i)
      configArea[i] = EEPROM.read(EEPROM_START + i);
  }
  const bool supportsBt = voxone::hardware::hardwareCapabilities().supportsVoxOneBt;
  const auto loaded = voxone::config_format::loadStartupConfig(
      eepromReady ? configArea : nullptr, sizeof(configArea), supportsBt, store,
      [this](config_t&) { _applyDefaults(); });
  _storage.begin(loaded.status, loaded.storedBtEnabled);
  btRuntime.configureBeforeStart(loaded.btEnabled);
  Serial.printf("[CONFIG] %s\n", voxone::config_format::configStartupMessage(loaded.status));
  bootInfo(); // https://github.com/e2002/yoradio/pull/149
  const bool migratingVolume = loaded.migratingVolume;
  if (store.maximumVolume < 1 || store.maximumVolume > 100) store.maximumVolume = 100;
  if (store.startupMode != STARTUP_LAST && store.startupMode != STARTUP_FIXED) store.startupMode = STARTUP_LAST;
  if (store.startupFixedVolume > 100) store.startupFixedVolume = 20;
  if (store.lastUserVolume > 100) store.lastUserVolume = volumeRawToUser(store.volume, store.maximumVolume);
  const VolumeState bootVolume = volumeStateAtStartup(store.volume, store.lastUserVolume,
    store.maximumVolume, store.startupMode == STARTUP_FIXED, store.startupFixedVolume, migratingVolume);
  userVolume = bootVolume.user;
  store.volume = bootVolume.raw;
  store.lastUserVolume = bootVolume.user;
  // Read-only startup must not schedule a delayed legacy volume write either.
  volumeBootDirty = false;
  _normalizeProductConfig();
  _normalizeAudioConfig();
  // A valid v7 boot does not rewrite the record. Legacy/default records get
  // one full write; future versions remain read-only for this entire boot.
  ConfigEepromStorage backend;
  const auto startupWrite = _storage.finishStartup(store, backend);
  if (startupWrite != voxone::config_format::ConfigWriteStatus::OK &&
      _storage.mode() != voxone::config_format::ConfigStorageMode::FUTURE_READ_ONLY)
    Serial.printf("[CONFIG] persistence: %s\n", voxone::config_format::configWriteMessage(startupWrite));
  BOOTLOG("CONFIG_VERSION\t%d", store.version);
  _initHW();
  if (!SPIFFS.begin(true)) {
    Serial.println("##[ERROR]#\tSPIFFS Mount Failed");
    return;
  }
  spiffsMounted = true;
  BOOTLOG("SPIFFS mounted");
  if (!restoreWebUpdateData()) Serial.println("##[ERROR]# Web Update data restore incomplete");
  radioPlaylistReady = playlistStore.begin() && playlistStore.recover();
  if (!radioPlaylistReady)
    Serial.println("##[ERROR]# Playlist recovery incomplete");
  currentWwwReady = _hasCurrentWwwAssets();
  if (!currentWwwReady) BOOTLOG("Current VoxOne WWW assets are incomplete!");
  ssidsCount = 0;
  setTimeConf();
}

bool Config::spiffsCleanup(){
  bool ret = SPIFFS.exists(INDEX_PATH);
  if(SPIFFS.exists(INDEX_PATH)) SPIFFS.remove(INDEX_PATH);
  return ret;
}

void Config::waitConnection(){
#if I2S_DOUT==255
  return;
#endif
  while(!player.connproc) vTaskDelay(50);
  vTaskDelay(500);
}

char * Config::ipToStr(IPAddress ip){
  snprintf(ipBuf, 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return ipBuf;
}
bool Config::prepareForPlaying(uint16_t stationId, bool sourceResume){
  setDspOn(1);
  vuThreshold = 0;
  screensaverTicks=SCREENSAVERSTARTUPDELAY;
  screensaverPlayingTicks=SCREENSAVERSTARTUPDELAY;
  display.putRequest(PSTOP);
  
  if(!loadStation(stationId)) return false;
  setTitle(LANG::const_PlConnect);
  station.bitrate=0;
  setBitrateFormat(BF_UNKNOWN);
  display.putRequest(DBITRATE);
  netserver.requestOnChange(BITRATE, 0);
  display.putRequest(NEWSTATION);
  display.putRequest(NEWMODE, PLAYER);
  netserver.requestOnChange(STATION, 0);
  netserver.requestOnChange(MODE, 0);
  netserver.loop();
  netserver.loop();
  if(radioPlayPreparationUpdatesSmartStart(sourceResume, store.smartstart))
    setSmartStart(0);
  return true;
}
void Config::configPostPlaying(){
  if(store.smartstart!=2) setSmartStart(1);
  netserver.requestOnChange(MODE, 0);
  //display.putRequest(NEWMODE, PLAYER);
  display.putRequest(PSTART);
}
void Config::initRadioPlaylist(){
  uint16_t _lastStation = 0;
  if (voxone::radioPlaylistShouldInitialize(spiffsMounted, radioPlaylistReady))
    initPlaylist();
  uint16_t cs = playlistLength();
  _lastStation = store.lastStation;
  if (cs==0) _lastStation=0;
  else if (_lastStation>cs) _lastStation=1;
  log_i("%d" ,_lastStation);
  if (_lastStation == 0 && cs > 0 &&
      store._reserved!=VOXONE_NO_STATION_MARKER) {
    _lastStation = 1;
  }
  lastStation(_lastStation);
  loadStation(_lastStation);
}

void Config::_initHW(){
  loadTheme();
  #if BRIGHTNESS_PIN!=255
    pinMode(BRIGHTNESS_PIN, OUTPUT);
    setBrightness(false);
  #endif
}

uint16_t Config::color565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void Config::loadTheme(){
  theme.background    = color565(COLOR_BACKGROUND);
  theme.meta          = color565(COLOR_STATION_NAME);
  theme.metabg        = color565(COLOR_STATION_BG);
  theme.metafill      = color565(COLOR_STATION_FILL);
  theme.title1        = color565(COLOR_SNG_TITLE_1);
  theme.title2        = color565(COLOR_SNG_TITLE_2);
  theme.digit         = color565(COLOR_DIGITS);
  theme.div           = color565(COLOR_DIVIDER);
  theme.vumax         = color565(COLOR_VU_MAX);
  theme.vumin         = color565(COLOR_VU_MIN);
  theme.clock         = color565(COLOR_CLOCK);
  theme.seconds       = color565(COLOR_SECONDS);
  theme.dow           = color565(COLOR_DAY_OF_W);
  theme.date          = color565(COLOR_DATE);
  theme.ip            = color565(COLOR_IP);
  theme.vol           = color565(COLOR_VOLUME_VALUE);
  theme.rssi          = color565(COLOR_RSSI);
  theme.bitrate       = color565(COLOR_BITRATE);
  theme.volbarout     = color565(COLOR_VOLBAR_OUT);
  theme.volbarin      = color565(COLOR_VOLBAR_IN);
  theme.plcurrent     = color565(COLOR_PL_CURRENT);
  theme.plcurrentbg   = color565(COLOR_PL_CURRENT_BG);
  theme.plcurrentfill = color565(COLOR_PL_CURRENT_FILL);
  theme.playlist[0]   = color565(COLOR_PLAYLIST_0);
  theme.playlist[1]   = color565(COLOR_PLAYLIST_1);
  theme.playlist[2]   = color565(COLOR_PLAYLIST_2);
  theme.playlist[3]   = color565(COLOR_PLAYLIST_3);
  theme.playlist[4]   = color565(COLOR_PLAYLIST_4);
  #include "../displays/tools/tftinverttitle.h"
}

void Config::reset(){
  if (!_storage.writable()) {
    Serial.println("[CONFIG] factory reset blocked: storage read-only/unavailable");
    return;
  }
  if (!mqttClearConfig()) {
    Serial.println("##[ERROR]# MQTT config reset failed; factory reset cancelled");
    return;
  }
  if (!setDefaults()) return;
  delay(500);
  ESP.restart();
}
void Config::enableScreensaver(bool val){
  saveValue(&store.screensaverEnabled, val);
  display.putRequest(NEWMODE, PLAYER);
}
void Config::setScreensaverTimeout(uint16_t val){
  val=constrain(val,5,65520);
  saveValue(&store.screensaverTimeout, val);
  display.putRequest(NEWMODE, PLAYER);
}
void Config::setScreensaverBlank(bool val){
  saveValue(&store.screensaverBlank, val);
  display.putRequest(NEWMODE, PLAYER);
}
void Config::setScreensaverPlayingEnabled(bool val){
  saveValue(&store.screensaverPlayingEnabled, val);
  display.putRequest(NEWMODE, PLAYER);
}
void Config::setScreensaverPlayingTimeout(uint16_t val){
  val=constrain(val,1,1080);
  config.saveValue(&config.store.screensaverPlayingTimeout, val);
  display.putRequest(NEWMODE, PLAYER);
}
void Config::setScreensaverPlayingBlank(bool val){
  saveValue(&store.screensaverPlayingBlank, val);
  display.putRequest(NEWMODE, PLAYER);
}
void Config::setSntpOne(const char *val){
  if (strlen(val) == 0) return;
  saveValue(store.sntp1, val, sizeof(store.sntp1));
  setTimeConf();
  timekeeper.forceTimeSync = true;
}
void Config::resetSystem(const char *val, uint8_t clientId){
  if (strcmp(val, "system") == 0) {
    saveValue(&store.smartstart, (uint8_t)2, false);
    saveValue(&store.audioinfo, false, false);
    saveValue(&store.vumeter, false, false);
    saveValue(&store.softapdelay, (uint8_t)0, false);
    saveValue(&store.abuff, (uint16_t)7, false);
    saveValue(&store.watchdog, true);
    _makeDefaultMdnsName(tmpBuf, sizeof(tmpBuf));
    saveValue(store.mdnsname, tmpBuf, MDNS_LENGTH, true, true);
    display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
    netserver.requestOnChange(GETSYSTEM, clientId);
    return;
  }
  if (strcmp(val, "screen") == 0) {
    saveValue(&store.flipscreen, false, false);
    display.flip();
    saveValue(&store.dspon, true, false);
    saveValue(&store.brightness, static_cast<uint8_t>(100), false);
    setBrightness(false);
    saveValue(&store.contrast, (uint8_t)55, false);
    display.setContrast();
    saveValue(&store.numplaylist, false);
    saveValue(&store.screensaverEnabled, false);
    saveValue(&store.screensaverTimeout, (uint16_t)20);
    saveValue(&store.screensaverBlank, false);
    saveValue(&store.screensaverPlayingEnabled, false);
    saveValue(&store.screensaverPlayingTimeout, (uint16_t)5);
    saveValue(&store.screensaverPlayingBlank, false);
    display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
    uiTimeoutReset();
    netserver.requestOnChange(GETSCREEN, clientId);
    return;
  }
  if (strcmp(val, "timezone") == 0) {
    saveValue(store.sntp1, DEFAULT_NTP_1, sizeof(store.sntp1), false);
    saveValue(store.sntp2, DEFAULT_NTP_2, sizeof(store.sntp2));
    saveValue(&store.timeSyncInterval, (uint16_t)60);
    saveValue(&store.timeSyncIntervalRTC, (uint16_t)24);
    setTimeConf();
    timekeeper.forceTimeSync = true;
    return;
  }
  if (strcmp(val, "controls") == 0) {
    saveValue(&store.volsteps, (uint8_t)1, false);
    saveValue(&store.skipPlaylistUpDown, false);
    setEncAcceleration(200);
    return;
  }
  if (strcmp(val, "1") == 0) {
    config.reset();
    return;
  }
}



void Config::_makeDefaultMdnsName(char *buffer, size_t size) {
  const uint32_t macSuffix = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFULL);
  snprintf(buffer, size, "VoxOne-%06X", (unsigned int)macSuffix);
}

void Config::_normalizeProductConfig() {
  // Startup normalization is RAM-only; persistence remains explicit.
  char legacyMdnsName[MDNS_LENGTH];
  snprintf(legacyMdnsName, sizeof(legacyMdnsName), "yoradio-%x", (unsigned int)getChipId());
  if (store.mdnsname[0] == '\0' || strcmp(store.mdnsname, legacyMdnsName) == 0) {
    _makeDefaultMdnsName(tmpBuf, sizeof(tmpBuf));
    strlcpy(store.mdnsname, tmpBuf, sizeof(store.mdnsname));
  }

  const bool usesLegacyDefaultNtp =
    strcmp(store.sntp1, "pool.ntp.org") == 0 &&
    (strcmp(store.sntp2, "0.ru.pool.ntp.org") == 0 ||
     strcmp(store.sntp2, "1.ru.pool.ntp.org") == 0);
  if (usesLegacyDefaultNtp) {
    strlcpy(store.sntp1, DEFAULT_NTP_1, sizeof(store.sntp1));
    strlcpy(store.sntp2, DEFAULT_NTP_2, sizeof(store.sntp2));
  } else {
    if (store.sntp1[0] == '\0') strlcpy(store.sntp1, DEFAULT_NTP_1, sizeof(store.sntp1));
    if (store.sntp2[0] == '\0') strlcpy(store.sntp2, DEFAULT_NTP_2, sizeof(store.sntp2));
  }

  store.watchdog = true;
}

void Config::_normalizeAudioConfig() {
  const int8_t bass = clampTone(store.bass);
  const int8_t middle = clampTone(store.middle);
  const int8_t trebble = clampTone(store.trebble);
  const int8_t balance = clampBalance(store.balance);
  if (store.bass == bass && store.middle == middle &&
      store.trebble == trebble && store.balance == balance) return;

  store.bass = bass;
  store.middle = middle;
  store.trebble = trebble;
  store.balance = balance;
}

void Config::_applyDefaults() {
  char mdns[MDNS_LENGTH];
  _makeDefaultMdnsName(mdns, sizeof(mdns));
  voxone::config_format::buildConfigDefaults(
      store, DEFAULT_NTP_1, DEFAULT_NTP_2, mdns, 7);
}

bool Config::persistV7() {
  WriteLock lock(_persistMutex);
  if (_startupReadOnly) return false;
  ConfigEepromStorage backend;
  const auto previous = _storage.lastStatus();
  const auto result = _storage.persist(store, backend);
  if (result != voxone::config_format::ConfigWriteStatus::OK &&
      (result != previous || result != voxone::config_format::ConfigWriteStatus::BLOCKED))
    Serial.printf("[CONFIG] persistence: %s\n", voxone::config_format::configWriteMessage(result));
  return result == voxone::config_format::ConfigWriteStatus::OK;
}

bool Config::setBtEnabled(bool enabled) {
  WriteLock lock(_persistMutex);
  _storage.setStoredBtEnabled(enabled);
  const bool effective = voxone::hardware::hardwareCapabilities().supportsVoxOneBt && enabled;
  btRuntime.setEnabled(effective);
  if (!_storage.dirty()) return _storage.mode() == voxone::config_format::ConfigStorageMode::V7;
  return persistV7();
}

bool Config::setDefaults() {
  WriteLock lock(_persistMutex);
  ConfigEepromStorage backend;
  const bool supportsBt = voxone::hardware::hardwareCapabilities().supportsVoxOneBt;
  const auto result = _storage.factoryReset(store, supportsBt, backend,
      [this](config_t&) { _applyDefaults(); });
  if (result != voxone::config_format::ConfigWriteStatus::BLOCKED) {
    btRuntime.setEnabled(supportsBt);
    userVolume = store.lastUserVolume;
  }
  if (result != voxone::config_format::ConfigWriteStatus::OK)
    Serial.printf("[CONFIG] factory reset: %s\n", voxone::config_format::configWriteMessage(result));
  return result == voxone::config_format::ConfigWriteStatus::OK;
}

void Config::setTimezone(int8_t tzh, int8_t tzm) {
  // Preserve legacy timezone storage without affecting the fixed Warsaw TZ.
  saveValue(&store.tzHour, tzh, false);
  saveValue(&store.tzMin, tzm);
}

void Config::setTimezoneOffset(uint16_t tzo) {
  saveValue(&store.timezoneOffset, tzo);
}

uint16_t Config::getTimezoneOffset() {
  return 0; // TODO
}

void Config::saveVolume(){
  // The player's existing volume timer still controls when this is called.
  persistV7();
}

uint8_t Config::setVolume(uint8_t val) {
  const VolumeState state = volumeStateFromRaw(val, store.maximumVolume);
  return setVolumeState(state.raw, state.user);
}

uint8_t Config::setVolumeState(uint8_t raw, uint8_t user) {
  WriteLock lock(_persistMutex);
  store.volume = raw;
  userVolume = user;
  store.lastUserVolume = user;
  display.putRequest(DRAWVOL);
  netserver.requestOnChange(VOLUME, 0);
  return store.volume;
}

void Config::setMaximumVolume(uint8_t maximum) {
  WriteLock lock(_persistMutex);
  if (maximum < 1 || maximum > 100 || maximum == store.maximumVolume) return;
  const uint8_t oldMaximum = store.maximumVolume;
  const uint8_t oldUser = userVolume;
  const uint8_t oldRaw = player.pendingRawAtMax(oldMaximum);
  const uint8_t oldOutput = player.volToI2S(oldRaw);
  store.maximumVolume = maximum;
  VolumeState adjusted = volumeStateAfterMaximum({oldRaw, oldUser}, oldMaximum, maximum);
  if (maximum > oldMaximum) {
    adjusted.raw = player.rawNotLouderThan(adjusted.raw, maximum, oldOutput);
    adjusted.user = volumeRawToUser(adjusted.raw, maximum);
  }
  setVolumeState(adjusted.raw, adjusted.user);
  persistV7();
  player.applyCurrentVolume();
}

void Config::setStartupMode(uint8_t mode) {
  if (mode != STARTUP_LAST && mode != STARTUP_FIXED) return;
  saveValue(&store.startupMode, mode);
}

void Config::setStartupFixedVolume(uint8_t user) {
  if (user > 100) return;
  saveValue(&store.startupFixedVolume, user);
}

bool Config::setTone(int8_t bass, int8_t middle, int8_t trebble) {
#if defined(VOXONE_PROFILE_A0)
  const bool toneLocked = voxone::dsp::lockDspToneMutation();
#endif
  WriteLock lock(_persistMutex);
  bass = clampTone(bass);
  middle = clampTone(middle);
  trebble = clampTone(trebble);
  bool persisted = true;
  if (store.bass != bass || store.middle != middle || store.trebble != trebble ||
      _storage.dirty() || _storage.mode() != voxone::config_format::ConfigStorageMode::V7) {
    saveValue(&store.bass, bass, false);
    saveValue(&store.middle, middle, false);
    saveValue(&store.trebble, trebble, false);
    persisted = persistV7();
  }
  player.setTone(store.bass, store.middle, store.trebble);
  netserver.requestOnChange(EQUALIZER, 0);
#if defined(VOXONE_PROFILE_A0)
  voxone::dsp::dspTransportToneChanged({store.bass, store.middle, store.trebble});
  if (toneLocked) voxone::dsp::unlockDspToneMutation();
#endif
  return persisted;
}

void Config::setSmartStart(uint8_t ss) {
  saveValue(&store.smartstart, ss);
}

void Config::setBalance(int8_t balance) {
  saveValue(&store.balance, clampBalance(balance));
  player.setBalance(store.balance);
  netserver.requestOnChange(BALANCE, 0);
}

uint8_t Config::setLastStation(uint16_t val) {
  lastStation(val);
  return store.lastStation;
}

bool Config::setLastStationChecked(uint16_t val, bool intentionalZero) {
  WriteLock lock(_persistMutex);
  const uint16_t marker = val == 0 && intentionalZero ? VOXONE_NO_STATION_MARKER : 0;
  if (store.lastStation == val && store._reserved == marker)
    return _startupReadOnly || (!_storage.dirty() &&
      _storage.mode() == voxone::config_format::ConfigStorageMode::V7) || persistV7();
  const uint16_t previous = store.lastStation;
  const uint16_t previousMarker = store._reserved;
  store.lastStation = val;
  store._reserved = marker;
  // Playlist recovery can call this from init(); preserve EEPROM during load.
  if (_startupReadOnly) return true;
  if (persistV7()) return true;
  store.lastStation = previous;
  store._reserved = previousMarker;
  // Keep the previous RAM selection on failure. Never attempt a legacy
  // rollback write or claim the failed commit was undone in flash.
  return false;
}

uint8_t Config::setCountStation(uint16_t val) {
  saveValue(&store.countStation, val);
  return store.countStation;
}

uint8_t Config::setLastSSID(uint8_t val) {
  saveValue(&store.lastSSID, val);
  return store.lastSSID;
}

void Config::setTitle(const char* title) {
  vuThreshold = 0;
  memset(config.station.title, 0, BUFLEN);
  strlcpy(config.station.title, title, BUFLEN);
  u8fix(config.station.title);
  netserver.requestOnChange(TITLE, 0);
  display.putRequest(NEWTITLE);
}

void Config::setStation(const char* station) {
  memset(config.station.name, 0, BUFLEN);
  strlcpy(config.station.name, station, BUFLEN);
  u8fix(config.station.title);
}

void Config::indexPlaylist() {
  if (!playlistStore.rebuildIndex())
    Serial.println("##[ERROR]# Station index rebuild failed");
}

void Config::initPlaylist() {
  //store.countStation = 0;
  if (!SPIFFS.exists(INDEX_PATH) && !playlistStore.rebuildIndex())
    Serial.println("##[ERROR]# Playlist index rebuild failed");

  /*if (SPIFFS.exists(INDEX_PATH)) {
    File index = SPIFFS.open(INDEX_PATH, "r");
    store.countStation = index.size() / 4;
    index.close();
    saveValue(&store.countStation, store.countStation, true, true);
  }*/
}
uint16_t Config::playlistLength(){
  PlaylistGuard guard;
  if (!guard) return 0;
  uint16_t out = 0;
  if (SPIFFS.exists(INDEX_PATH)) {
    File index = SPIFFS.open(INDEX_PATH, "r");
    out = index.size() / 4;
    index.close();
  }
  return out;
}
bool Config::loadStation(uint16_t ls) {
  PlaylistGuard guard;
  if (!guard) return false;
  station.id = 0;
  station.metadataMode = STATION_META_NORMAL;
  if (ls == 0 && store._reserved == VOXONE_NO_STATION_MARKER) {
    station.name[0] = '\0';
    station.url[0] = '\0';
    station.ovol = 0;
    return false;
  }
  if (ls == 0) return false;
  int sOvol;
  uint16_t cs = playlistLength();
  if (cs == 0) {
    memset(station.url, 0, BUFLEN);
    memset(station.name, 0, BUFLEN);
    strncpy(station.name, "ёRadio", BUFLEN);
    station.ovol = 0;
    return false;
  }
  if (ls > playlistLength()) {
    ls = 1;
  }
  File playlist = SPIFFS.open(PLAYLIST_PATH, "r");
  File index = SPIFFS.open(INDEX_PATH, "r");
  if (!playlist || !index || !index.seek((ls - 1) * 4, SeekSet)) return false;
  uint32_t pos = 0;
  if (index.readBytes((char *) &pos, 4) != 4 || pos >= playlist.size() ||
      !playlist.seek(pos, SeekSet)) return false;
  index.close();
  String loadedLine = playlist.readStringUntil('\n');
  if (loadedLine.endsWith("\r")) loadedLine.remove(loadedLine.length() - 1);
  static char nativeLine[BUFLEN * 3]; // Loads are serialized by PlaylistGuard.
  loadedLine.toCharArray(nativeLine, sizeof(nativeLine));
  char* name = nullptr;
  char* url = nullptr;
  const bool parsed = loadedLine.length() < sizeof(nativeLine) &&
                      stationParseFields(nativeLine, station.id, name, url,
                                         sOvol, station.metadataMode);
  if (parsed) {
    strlcpy(tmpBuf, name, BUFLEN);
    strlcpy(tmpBuf2, url, BUFLEN);
  }
  if (parsed) {
    memset(station.url, 0, BUFLEN);
    memset(station.name, 0, BUFLEN);
    strncpy(station.name, tmpBuf, BUFLEN);
    strncpy(station.url, tmpBuf2, BUFLEN);
    station.ovol = sOvol;
    setLastStation(ls);
  } else {
    station.id = 0;
    station.metadataMode = STATION_META_NORMAL;
  }
  playlist.close();
  return parsed;
}

char * Config::stationByNum(uint16_t num){
  PlaylistGuard guard;
  if (!guard) {
    _stationBuf[0] = '\0';
    return _stationBuf;
  }
  File playlist = SPIFFS.open(PLAYLIST_PATH, "r");
  File index = SPIFFS.open(INDEX_PATH, "r");
  index.seek((num - 1) * 4, SeekSet);
  uint32_t pos;
  memset(_stationBuf, 0, sizeof(_stationBuf));
  index.readBytes((char *) &pos, 4);
  index.close();
  playlist.seek(pos, SeekSet);
  playlist.readStringUntil('\t'); // Skip immutable station ID.
  strncpy(_stationBuf, playlist.readStringUntil('\t').c_str(), sizeof(_stationBuf));
  playlist.close();
  return _stationBuf;
}

void Config::escapeQuotes(const char* input, char* output, size_t maxLen) {
  size_t j = 0;
  for (size_t i = 0; input[i] != '\0' && j < maxLen - 1; ++i) {
    if (input[i] == '"' && j < maxLen - 2) {
      output[j++] = '\\';
      output[j++] = '"';
    } else {
      output[j++] = input[i];
    }
  }
  output[j] = '\0';
}

bool Config::parseJSON(const char* line, char* name, char* url, int &ovol) {
  char* tmps, *tmpe;
  const char* cursor = line;
  char port[8], host[246], file[254];
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) return false;
  strlcpy(name, tmps + 3, tmpe - tmps - 3 + 1);
  if (strlen(name) == 0) return false;
  cursor = tmpe + 3;
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) return false;
  strlcpy(host, tmps + 3, tmpe - tmps - 3 + 1);
  if (strlen(host) == 0) return false;
  if (strstr(host, "http://") == NULL && strstr(host, "https://") == NULL) {
    sprintf(file, "http://%s", host);
    strlcpy(host, file, strlen(file) + 1);
  }
  cursor = tmpe + 3;
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) return false;
  strlcpy(file, tmps + 3, tmpe - tmps - 3 + 1);
  cursor = tmpe + 3;
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) return false;
  strlcpy(port, tmps + 3, tmpe - tmps - 3 + 1);
  int p = atoi(port);
  if (p > 0) {
    sprintf(url, "%s:%d%s", host, p, file);
  } else {
    sprintf(url, "%s%s", host, file);
  }
  cursor = tmpe + 3;
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\"}");
  if (tmpe == NULL) return false;
  strlcpy(port, tmps + 3, tmpe - tmps - 3 + 1);
  ovol = atoi(port);
  return true;
}

bool Config::parseWsCommand(const char* line, char* cmd, char* val, uint8_t cSize) {
  char *tmpe;
  tmpe = strstr(line, "=");
  if (tmpe == NULL) return false;
  memset(cmd, 0, cSize);
  strlcpy(cmd, line, tmpe - line + 1);
  //if (strlen(tmpe + 1) == 0) return false;
  memset(val, 0, cSize);
  strlcpy(val, tmpe + 1, strlen(line) - strlen(cmd) + 1);
  return true;
}

bool Config::parseSsid(const char* line, char* ssid, char* pass) {
  char *tmpe;
  tmpe = strstr(line, "\t");
  if (tmpe == NULL) return false;
  uint16_t pos = tmpe - line;
  if (pos > 29 || strlen(line) > 71) return false;
  memset(ssid, 0, 30);
  strlcpy(ssid, line, pos + 1);
  memset(pass, 0, 40);
  strlcpy(pass, line + pos + 1, strlen(line) - pos);
  return true;
}

bool Config::saveWifiCredentials(const char* ssid, const char* password) {
  if (!apWifiCredentialsValid(ssid, password)) return false;
  constexpr char wifiTempPath[] = "/data/wifi.csv.tmp";
  File file = SPIFFS.open(wifiTempPath, "w");
  if (!file) return false;
  const size_t ssidLength = strlen(ssid);
  const size_t passwordLength = strlen(password);
  const bool written = file.write(reinterpret_cast<const uint8_t*>(ssid), ssidLength) == ssidLength &&
                       file.write(static_cast<uint8_t>('\t')) == 1 &&
                       file.write(reinterpret_cast<const uint8_t*>(password), passwordLength) == passwordLength &&
                       file.write(static_cast<uint8_t>('\n')) == 1;
  file.close();
  if (!written || !SPIFFS.rename(wifiTempPath, SSIDS_PATH)) {
    SPIFFS.remove(wifiTempPath);
    return false;
  }
  return true;
}

void Config::setTimeConf(){
  if(strlen(store.sntp1)>0 && strlen(store.sntp2)>0){
    configTzTime(WARSAW_TZ, store.sntp1, store.sntp2);
  }else if(strlen(store.sntp1)>0){
    configTzTime(WARSAW_TZ, store.sntp1);
  }
  timekeeper.watchNtp();
}

bool Config::initNetwork() {
  File file = SPIFFS.open(SSIDS_PATH, "r");
  if (!file || file.isDirectory()) {
    return false;
  }
  char ssidval[30], passval[40];
  uint8_t c = 0;
  while (file.available()) {
    if (parseSsid(file.readStringUntil('\n').c_str(), ssidval, passval)) {
      strlcpy(ssids[c].ssid, ssidval, 30);
      strlcpy(ssids[c].password, passval, 40);
      ssidsCount++;
      c++;
    }
  }
  file.close();
  return true;
}

void Config::setBrightness(bool dosave){
#if BRIGHTNESS_PIN!=255
  if(!store.dspon && dosave) {
    display.wakeup();
  }
  analogWrite(BRIGHTNESS_PIN, map(store.brightness, 0, 100, 0, 255));
  if(!store.dspon) store.dspon = true;
  if(dosave){
    saveValue(&store.brightness, store.brightness, false, true);
    saveValue(&store.dspon, store.dspon, true, true);
  }
#endif
#if BRIGHTNESS_PIN != 255
  if (dosave) netserver.requestOnChange(GETSCREEN, 0);
#endif
}

void Config::setDspOn(bool dspon, bool saveval){
  if(saveval){
    store.dspon = dspon;
    saveValue(&store.dspon, store.dspon, true, true);
  }
  if(!dspon){
#if BRIGHTNESS_PIN!=255
  analogWrite(BRIGHTNESS_PIN, 0);
#endif
    display.deepsleep();
  }else{
    display.wakeup();
#if BRIGHTNESS_PIN!=255
  analogWrite(BRIGHTNESS_PIN, map(store.brightness, 0, 100, 0, 255));
#endif
  }
}

void Config::doSleep(){
  if(BRIGHTNESS_PIN!=255) analogWrite(BRIGHTNESS_PIN, 0);
  display.deepsleep();
#if !defined(ARDUINO_ESP32C3_DEV)
  if(WAKE_PIN!=255) esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_PIN, LOW);
  esp_sleep_enable_timer_wakeup(config.sleepfor * 60 * 1000000ULL);
  esp_deep_sleep_start();
#endif
}

void Config::doSleepW(){
  if(BRIGHTNESS_PIN!=255) analogWrite(BRIGHTNESS_PIN, 0);
  display.deepsleep();
#if !defined(ARDUINO_ESP32C3_DEV)
  if(WAKE_PIN!=255) esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_PIN, LOW);
  esp_deep_sleep_start();
#endif
}

void Config::sleepForAfter(uint16_t sf, uint16_t sa){
  sleepfor = sf;
  if(sa > 0) timekeeper.waitAndDo(static_cast<uint32_t>(sa) * 60UL, doSleep, DelayedActionSlot::SLEEP);
  else doSleep();
}

void Config::bootInfo() {
  BOOTLOG("************************************************");
#if defined(VOXONE_PROFILE_C0)
  BOOTLOG("reset reason:\t%d", static_cast<int>(esp_reset_reason()));
#endif
  BOOTLOG("VoxOne v%s-%s", VOXONE_VERSION, VOXONE_BUILD_CHANNEL);
  BOOTLOG("build: %s", VOXONE_BUILD_SHA);
  BOOTLOG("profile: %s", VOXONE_BUILD_PROFILE);
  BOOTLOG("based on yoRadio v%s", YOVERSION);
  BOOTLOG("************************************************");
  BOOTLOG("------------------------------------------------");
  BOOTLOG("arduino:\t%d", ARDUINO);
  BOOTLOG("compiler:\t%s", __VERSION__);
  BOOTLOG("esp32core:\t%d.%d.%d", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
  uint32_t chipId = 0;
  for(int i=0; i<17; i=i+8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  BOOTLOG("chip:\t\tmodel: %s | rev: %d | id: %lu | cores: %d | psram: %lu", ESP.getChipModel(), ESP.getChipRevision(), chipId, ESP.getChipCores(), ESP.getPsramSize());
  BOOTLOG("display:\t%d", DSP_MODEL);
  BOOTLOG("audio:\t\t%s (%d, %d, %d)", "I2S", I2S_DOUT, I2S_BCLK, I2S_LRC);
  BOOTLOG("audioinfo:\t%s", store.audioinfo?"true":"false");
  BOOTLOG("smartstart:\t%d", store.smartstart);
  BOOTLOG("vumeter:\t%s", store.vumeter?"true":"false");
  BOOTLOG("softapdelay:\t%d", store.softapdelay);
  BOOTLOG("flipscreen:\t%s", store.flipscreen?"true":"false");
  BOOTLOG("invertdisplay:\t%s", store.invertdisplay?"true":"false");
  BOOTLOG("buttons:\tleft=%d, center=%d, right=%d, up=%d, down=%d, mode=%d, pullup=%s", 
          BTN_LEFT, BTN_CENTER, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_MODE, BTN_INTERNALPULLUP?"true":"false");
  BOOTLOG("encoder:\tl=%d, b=%d, r=%d, pullup=%s, buttonPullup=%s",
          ENC_BTNL, ENC_BTNB, ENC_BTNR, ENC_INTERNALPULLUP?"true":"false",
          ENC_BUTTON_INTERNALPULLUP?"true":"false");
  BOOTLOG("------------------------------------------------");
}
