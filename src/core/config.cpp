#include "options.h"
#include "config.h"
#include "config_startup.h"
#include "config_capability_migration.h"
#include "config_eeprom_storage.h"
#include "config_restore_adapter.h"
#include "bt_runtime.h"
#include "../hardware/hardware_descriptor.h"
#include "playlist_store.h"
#include "station_format.h"
#include "display.h"
#include "player.h"
#include "network.h"
#include "netserver.h"
#include "dsp_build_config.h"
#if VOXONE_INCLUDE_DSP
#include "dsp_transport_runtime.h"
#endif
#include "controls.h"
#include "timekeeper.h"
#include "serialcli.h"
#include "rtcsupport.h"
#include "volume_map.h"
#include "ui_timeout_config.h"
#include "ui_state.h"
#include "ui_menu.h"
#include "source_manager.h"
#include "www_readiness.h"
#include "www_asset_integrity.h"
#include "ap_wifi_recovery.h"
#include "wifi_config_atomic.h"
#include "wifi_config_parser.h"
#include "utf8_text.h"
#include "../displays/tools/l10n.h"
#include <algorithm>
#include <cstddef>
#include <esp_sntp.h>
#include <esp_system.h>

namespace {
static_assert(CONFIG_VERSION == voxone::config_format::kConfigV7,
              "Persistence adapter expects v7");
static_assert(STARTUP_LAST == 0, "Startup defaults must match STARTUP_LAST");
static_assert(EEPROM_SIZE == voxone::config_storage::kEepromSize,
              "EEPROM storage size changed");
static_assert(EEPROM_START == voxone::config_storage::kConfigOffset,
              "EEPROM config offset changed");
static_assert(EEPROM_SIZE - EEPROM_START == voxone::config_format::kConfigEepromCapacity,
              "Config area size changed");
constexpr char WARSAW_TZ[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";
constexpr char DEFAULT_NTP_1[] = "0.pl.pool.ntp.org";
constexpr char DEFAULT_NTP_2[] = "1.pl.pool.ntp.org";
constexpr int8_t TONE_MIN = -6;
constexpr int8_t TONE_MAX = 6;
constexpr int8_t BALANCE_MIN = -16;
constexpr int8_t BALANCE_MAX = 16;

bool localDisplayInstalled() {
  return display.active();
}

bool localBacklightInstalled() {
  const auto& hardware = voxone::hardware::currentHardware();
  voxone::hardware::DisplayKind selected = voxone::hardware::DisplayKind::None;
  return localDisplayInstalled() &&
         voxone::hardware::hasPin(hardware.display.backlight) &&
         voxone::hardware::runtimeDisplayKind(
             voxone::hardware::runtimeHardwareConfig().display, selected) &&
         selected == hardware.display.backlightDisplay;
}

class WifiConfigSpiffsStorage {
 public:
  bool exists(const char* path) const { return SPIFFS.exists(path); }
  bool remove(const char* path) { return SPIFFS.remove(path); }
  bool rename(const char* from, const char* to) {
    return SPIFFS.rename(from, to);
  }
  bool write(const char* path, const uint8_t* bytes, size_t size) {
    File file = SPIFFS.open(path, "w");
    if (!file) return false;
    const bool written = (!size || file.write(bytes, size) == size);
    file.flush();
    file.close();
    return written;
  }
  bool matches(const char* path, const uint8_t* bytes, size_t size) const {
    File file = SPIFFS.open(path, "r");
    if (!file || file.size() != size) {
      file.close();
      return false;
    }
    uint8_t buffer[64];
    size_t position = 0;
    while (position < size) {
      const size_t count = std::min(sizeof(buffer), size - position);
      if (file.read(buffer, count) != count ||
          memcmp(buffer, bytes + position, count) != 0) {
        file.close();
        return false;
      }
      position += count;
    }
    file.close();
    return true;
  }
  bool validBytes(const uint8_t* bytes, size_t size) const {
    return voxone::wifi_config::validSerializedConfig(bytes, size);
  }
  bool valid(const char* path) const {
    File file = SPIFFS.open(path, "r");
    if (!file || file.isDirectory() ||
        file.size() > voxone::wifi_config::kMaxFileBytes) {
      file.close();
      return false;
    }
    uint8_t bytes[voxone::wifi_config::kMaxFileBytes];
    const size_t size = file.size();
    const bool read = !size || file.read(bytes, size) == size;
    file.close();
    return read && validBytes(bytes, size);
  }
};

class SpiffsAssetFile : public voxone::www_integrity::AssetFile {
 public:
  bool open(const char* path) override {
    close();
    file_ = SPIFFS.open(path, "r");
    return file_ && !file_.isDirectory();
  }
  void close() override {
    if (file_) file_.close();
  }
  uint32_t size() const override {
    return file_ ? static_cast<uint32_t>(file_.size()) : 0;
  }
  bool seek(uint32_t offset) override {
    return file_ && file_.seek(offset);
  }
  bool read(uint8_t* output, std::size_t capacity,
            std::size_t& bytesRead) override {
    bytesRead = 0;
    if (!file_ || (!output && capacity)) return false;
    bytesRead = file_.read(output, capacity);
    return bytesRead > 0 || capacity == 0;
  }

 private:
  File file_;
};

class SpiffsAssetStore : public voxone::www_integrity::AssetStore {
 public:
  explicit SpiffsAssetStore(bool mounted) : mounted_(mounted) {}
  bool mounted() const override { return mounted_; }
  bool exists(const char* path) const override {
    return mounted_ && SPIFFS.exists(path);
  }
  voxone::www_integrity::AssetFile& manifestFile() override {
    return manifest_;
  }
  voxone::www_integrity::AssetFile& assetFile() override { return asset_; }

 private:
  bool mounted_;
  SpiffsAssetFile manifest_;
  SpiffsAssetFile asset_;
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
  voxone::utf8::trimIncompleteTail(src);
}

void Config::init() {
  voxone::config_format::ConfigStartupReadOnlyScope readOnly(_startupReadOnly);
  if (!_persistMutex) _persistMutex = xSemaphoreCreateRecursiveMutex();
  const bool eepromReady = _persistMutex && voxone::config_storage::beginEeprom();
  uiTimeoutConfig();
  screensaverTicks = 0;
  screensaverPlayingTicks = 0;
  isScreensaver = false;
  memset(tmpBuf, 0, BUFLEN);
  //bootInfo();
  _rtcFound = false;
  if (voxone::hardware::hardwareCapabilities().supportsRtc) {
    const auto& i2c = voxone::hardware::currentHardware().i2c;
    BOOTLOG("RTC begin(SDA=%d,SCL=%d)", i2c.sda, i2c.scl);
    if (rtc.init()) {
      BOOTLOG("done");
      _rtcFound = true;
    } else {
      BOOTLOG("[ERROR] - Couldn't find RTC");
    }
  }
  spiffsMounted = false;
  webUpdateDataReady = false;
  wifiConfigReady = false;
  currentWwwReady = false;
  wwwAssetIntegrity = voxone::www_integrity::Report{};
  radioPlaylistReady = false;
  uint8_t configArea[EEPROM_SIZE - EEPROM_START]{};
  if (eepromReady) {
    for (size_t i = 0; i < sizeof(configArea); ++i)
      configArea[i] = EEPROM.read(EEPROM_START + i);
  }
  const auto& capabilities = voxone::hardware::hardwareCapabilities();
  const bool supportsBt = capabilities.supportsVoxOneBt;
  const auto loaded = voxone::config_format::loadStartupConfig(
      eepromReady ? configArea : nullptr, sizeof(configArea), supportsBt, store,
      [this](config_t&) { _applyDefaults(); });
  if (!voxone::ha_name::decode(
          store.reservedWeather, sizeof(store.reservedWeather),
          store.haName, sizeof(store.haName)))
    store.haName[0] = '\0';
  if (!voxone::tft_theme::decode(
          store.reservedWeather, sizeof(store.reservedWeather),
          store.tftTheme))
    store.tftTheme = THEME_DEFAULT;
  if (!voxone::power_mode::decode(
          store.reservedWeather, sizeof(store.reservedWeather),
          store.powerMode))
    store.powerMode = MODE_SOFT_STANDBY;
  if (loaded.status != voxone::config_format::ConfigRecordStatus::LOADED_V7 ||
      !voxone::encoder_ui::decode(
          store.reservedWeather, sizeof(store.reservedWeather),
          store.encoderUiReversed))
    store.encoderUiReversed = false;
  if (loaded.status != voxone::config_format::ConfigRecordStatus::LOADED_V7 ||
      !voxone::line_out::decode(
          store.reservedWeather, sizeof(store.reservedWeather),
          store.lineOutEnabled, store.lineOutVolume)) {
    store.lineOutEnabled = voxone::line_out::kDefaultEnabled;
    store.lineOutVolume = voxone::line_out::kDefaultVolume;
  }
  _startupStatus = loaded.status;
  _storage.begin(loaded.status, loaded.storedBtEnabled);
  const bool needsBtIntroductionMigration =
      capabilities.needsVoxOneBtIntroductionMigration;
  const auto btMigrationMarker = needsBtIntroductionMigration
      ? voxone::config_capability_migration::readS0BtIntroductionMarker()
      : voxone::config_capability_migration::MarkerStatus::Applied;
  const auto btMigration =
      voxone::config_capability_migration::planS0BtIntroduction(
          needsBtIntroductionMigration, supportsBt, loaded.status,
          loaded.storedBtEnabled,
          btMigrationMarker);
  if (btMigration.persistEnabled) _storage.setStoredBtEnabled(true);
  btRuntime.configureBeforeStart(btMigration.enableForThisBoot
                                     ? true : loaded.btEnabled);
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
  _vuThreshold.reset();
  _normalizeProductConfig();
  _normalizeAudioConfig();
  // A valid v7 boot does not rewrite the record. Legacy/default records get
  // one full write; future versions remain read-only for this entire boot.
  voxone::config_storage::ConfigEepromStorage backend;
  const auto startupWrite = btMigration.persistEnabled
      ? _storage.persist(store, backend)
      : _storage.finishStartup(store, backend);
  if (startupWrite != voxone::config_format::ConfigWriteStatus::OK &&
      _storage.mode() != voxone::config_format::ConfigStorageMode::FUTURE_READ_ONLY)
    Serial.printf("[CONFIG] persistence: %s\n", voxone::config_format::configWriteMessage(startupWrite));
  if (needsBtIntroductionMigration &&
      btMigration.markAppliedAfterPersistence &&
      startupWrite == voxone::config_format::ConfigWriteStatus::OK &&
      _storage.mode() == voxone::config_format::ConfigStorageMode::V7 &&
      _storage.storedBtEnabled()) {
    if (voxone::config_capability_migration::writeS0BtIntroductionMarker())
      Serial.println("[CONFIG] S0 VoxOneBT capability migration applied");
    else
      Serial.println("##[ERROR]# S0 VoxOneBT migration marker write failed");
  } else if (needsBtIntroductionMigration &&
             (btMigrationMarker ==
                 voxone::config_capability_migration::MarkerStatus::Invalid ||
             btMigrationMarker ==
                 voxone::config_capability_migration::MarkerStatus::Unavailable)) {
    Serial.println("##[ERROR]# S0 VoxOneBT migration marker unavailable; stored setting preserved");
  }
  _publishRuntimeLocked();
  uiMenu.configureEncoderUiReversed(store.encoderUiReversed);
  BOOTLOG("CONFIG_VERSION\t%d", store.version);
  _initHW();
  // Never format shared data automatically while a MAIN image may still be
  // PENDING_VERIFY. A damaged filesystem must be visible to the OTA health
  // gate so the previous firmware can be selected without destroying data.
  if (!SPIFFS.begin(false)) {
    Serial.println("##[ERROR]#\tSPIFFS Mount Failed");
    return;
  }
  spiffsMounted = true;
  BOOTLOG("SPIFFS mounted");
  voxone::config_restore::cleanupStaleAfterStorageInit();
  webUpdateDataReady = restoreWebUpdateData();
  if (!webUpdateDataReady) Serial.println("##[ERROR]# Web Update data restore incomplete");
  WifiConfigSpiffsStorage wifiStorage;
  wifiConfigReady = voxone::wifi_config_atomic::recover(wifiStorage);
  if (!wifiConfigReady)
    Serial.println("##[ERROR]# Wi-Fi credentials recovery incomplete");
  radioPlaylistReady = playlistStore.begin() && playlistStore.recover();
  if (!radioPlaylistReady)
    Serial.println("##[ERROR]# Playlist recovery incomplete");
  SpiffsAssetStore wwwAssets(spiffsMounted);
  const uint32_t wwwVerifyStarted = millis();
  wwwAssetIntegrity = voxone::www_integrity::verify(
      wwwAssets, voxone::kFirmwareWwwContract);
  wwwAssetIntegrity.elapsedMs = millis() - wwwVerifyStarted;
  currentWwwReady = wwwAssetIntegrity.wwwUsable;
  BOOTLOG("WWW assets status=%s contract=%s files=%u bytes=%lu time=%lums",
          voxone::www_integrity::statusName(wwwAssetIntegrity.status),
          voxone::www_integrity::contractStatusName(wwwAssetIntegrity.contract),
          static_cast<unsigned>(wwwAssetIntegrity.filesChecked),
          static_cast<unsigned long>(wwwAssetIntegrity.bytesChecked),
          static_cast<unsigned long>(wwwAssetIntegrity.elapsedMs));
  if (!currentWwwReady)
    BOOTLOG("Current VoxOne WWW assets are not usable: %s",
            wwwAssetIntegrity.problemAsset);
  ssidsCount = 0;
  setTimeConf();
}

void Config::waitConnection(){
  if (!voxone::hardware::hasPin(
          voxone::hardware::currentHardware().audioOut.dout)) return;
  while(!player.connproc) vTaskDelay(50);
  vTaskDelay(500);
}

char * Config::ipToStr(IPAddress ip){
  snprintf(ipBuf, 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return ipBuf;
}
bool Config::prepareForPlaying(uint16_t stationId, bool sourceResume){
  setDspOn(1);
  resetVuThreshold();
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
  transitionUiMode(PLAYER);
  netserver.requestOnChange(STATION, 0);
  netserver.requestOnChange(MODE, 0);
  netserver.loop();
  netserver.loop();
  if(radioPlayPreparationUpdatesSmartStart(
         sourceResume, runtimeSnapshot().smartStart))
    setSmartStart(0);
  return true;
}
void Config::configPostPlaying(){
  if(runtimeSnapshot().smartStart!=2) setSmartStart(1);
  netserver.requestOnChange(MODE, 0);
  //transitionUiMode(PLAYER);
  display.putRequest(PSTART);
}
void Config::initRadioPlaylist(){
  uint16_t _lastStation = 0;
  if (voxone::radioPlaylistShouldInitialize(spiffsMounted, radioPlaylistReady))
    initPlaylist();
  uint16_t cs = playlistLength();
  const auto runtime = runtimeSnapshot();
  _lastStation = runtime.lastStation;
  if (cs==0) _lastStation=0;
  else if (_lastStation>cs) _lastStation=1;
  log_i("%d" ,_lastStation);
  if (_lastStation == 0 && cs > 0 &&
      !runtime.intentionalNoStation) {
    _lastStation = 1;
  }
  lastStation(_lastStation);
  loadStation(_lastStation);
}

void Config::_initHW(){
  loadTheme();
  const auto backlight = voxone::hardware::currentHardware().display.backlight;
  if (localBacklightInstalled()) {
    pinMode(backlight, OUTPUT);
    setBrightness(false);
  }
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
  if (!setDefaults()) return;
  delay(500);
  ESP.restart();
}
void Config::enableScreensaver(bool val){
  saveValue(&store.screensaverEnabled, val);
  transitionUiMode(PLAYER);
}
void Config::setScreensaverTimeout(uint16_t val){
  val=constrain(val,5,65520);
  saveValue(&store.screensaverTimeout, val);
  transitionUiMode(PLAYER);
}
void Config::setScreensaverBlank(bool val){
  saveValue(&store.screensaverBlank, val);
  transitionUiMode(PLAYER);
}
void Config::setScreensaverPlayingEnabled(bool val){
  saveValue(&store.screensaverPlayingEnabled, val);
  transitionUiMode(PLAYER);
}
void Config::setScreensaverPlayingTimeout(uint16_t val){
  val=constrain(val,1,1080);
  config.saveValue(&config.store.screensaverPlayingTimeout, val);
  transitionUiMode(PLAYER);
}
void Config::setScreensaverPlayingBlank(bool val){
  saveValue(&store.screensaverPlayingBlank, val);
  transitionUiMode(PLAYER);
}
void Config::setSntpOne(const char *val){
  if (strlen(val) == 0) return;
  saveValue(store.sntp1, val, sizeof(store.sntp1));
  setTimeConf();
  timekeeper.forceTimeSync = true;
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
  store.haName[0] = '\0';
  voxone::ha_name::encode(
      store.haName, 0, store.reservedWeather, sizeof(store.reservedWeather));
  store.tftTheme = THEME_DEFAULT;
  voxone::tft_theme::encode(
      store.tftTheme, store.reservedWeather, sizeof(store.reservedWeather));
  store.powerMode = MODE_SOFT_STANDBY;
  voxone::power_mode::encode(
      store.powerMode, store.reservedWeather, sizeof(store.reservedWeather));
  store.encoderUiReversed = false;
  voxone::encoder_ui::encode(
      store.encoderUiReversed, store.reservedWeather,
      sizeof(store.reservedWeather));
  store.lineOutEnabled = voxone::line_out::kDefaultEnabled;
  store.lineOutVolume = voxone::line_out::kDefaultVolume;
  voxone::line_out::encode(
      store.lineOutEnabled, store.lineOutVolume, store.reservedWeather,
      sizeof(store.reservedWeather));
}

bool Config::persistV7() {
  WriteLock lock(_persistMutex);
  if (_startupReadOnly) return false;
  voxone::config_storage::ConfigEepromStorage backend;
  const auto previous = _storage.lastStatus();
  const auto result = _storage.persist(store, backend);
  if (result != voxone::config_format::ConfigWriteStatus::OK &&
      (result != previous || result != voxone::config_format::ConfigWriteStatus::BLOCKED))
    Serial.printf("[CONFIG] persistence: %s\n", voxone::config_format::configWriteMessage(result));
  return result == voxone::config_format::ConfigWriteStatus::OK;
}

bool Config::setHaName(const char* value) {
  if (!value) return false;
  const size_t length = strlen(value);
  if (!voxone::ha_name::valid(value, length)) return false;
  WriteLock lock(_persistMutex);
  if (strcmp(store.haName, value) == 0) return true;
  if (!voxone::ha_name::encode(
          value, length, store.reservedWeather,
          sizeof(store.reservedWeather)))
    return false;
  strlcpy(store.haName, value, sizeof(store.haName));
  _storage.markDirty();
  _publishRuntimeLocked();
  return !_startupReadOnly && persistV7();
}

bool Config::setTftTheme(TftTheme theme) {
  if (!voxone::tft_theme::valid(theme)) return false;
  WriteLock lock(_persistMutex);
  if (store.tftTheme == theme) return true;
  if (!voxone::tft_theme::encode(
          theme, store.reservedWeather, sizeof(store.reservedWeather)))
    return false;
  store.tftTheme = theme;
  _storage.markDirty();
  _publishRuntimeLocked();
  return !_startupReadOnly && persistV7();
}

bool Config::setPowerMode(PowerMode mode) {
  if (!voxone::power_mode::valid(mode)) return false;
  WriteLock lock(_persistMutex);
  if (store.powerMode == mode) return true;
  if (!voxone::power_mode::encode(
          mode, store.reservedWeather, sizeof(store.reservedWeather)))
    return false;
  store.powerMode = mode;
  _storage.markDirty();
  _publishRuntimeLocked();
  return !_startupReadOnly && persistV7();
}

bool Config::setEncoderUiReversed(bool reversed) {
  WriteLock lock(_persistMutex);
  if (store.encoderUiReversed == reversed) {
    uiMenu.configureEncoderUiReversed(reversed);
    netserver.requestOnChange(GETSCREEN, 0);
    return true;
  }
  if (!voxone::encoder_ui::encode(
          reversed, store.reservedWeather, sizeof(store.reservedWeather)))
    return false;
  store.encoderUiReversed = reversed;
  _storage.markDirty();
  _publishRuntimeLocked();
  uiMenu.configureEncoderUiReversed(reversed);
  const bool persisted = !_startupReadOnly && persistV7();
  if (persisted) netserver.requestOnChange(GETSCREEN, 0);
  return persisted;
}

bool Config::setLineOutEnabled(bool enabled) {
  bool persisted = true;
  {
    WriteLock lock(_persistMutex);
    if (store.lineOutEnabled == enabled) return true;
    if (!voxone::line_out::encode(
            enabled, store.lineOutVolume, store.reservedWeather,
            sizeof(store.reservedWeather)))
      return false;
    store.lineOutEnabled = enabled;
    _storage.markDirty();
    _publishRuntimeLocked();
    persisted = !_startupReadOnly && persistV7();
  }
  if (!persisted) return false;
  player.applyCurrentVolume();
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  sourceManagerApplyConfiguredBluetoothVolume();
#endif
  display.putRequest(DRAWVOL);
  netserver.requestOnChange(VOLUME, 0);
  return true;
}

bool Config::setLineOutVolume(uint8_t volume) {
  if (volume > 100) return false;
  bool enabled = false;
  bool persisted = true;
  {
    WriteLock lock(_persistMutex);
    if (store.lineOutVolume == volume) return true;
    if (!voxone::line_out::encode(
            store.lineOutEnabled, volume, store.reservedWeather,
            sizeof(store.reservedWeather)))
      return false;
    store.lineOutVolume = volume;
    enabled = store.lineOutEnabled;
    _storage.markDirty();
    _publishRuntimeLocked();
    persisted = !_startupReadOnly && persistV7();
  }
  if (!persisted) return false;
  if (enabled) {
    player.applyCurrentVolume();
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
    sourceManagerApplyConfiguredBluetoothVolume();
#endif
    display.putRequest(DRAWVOL);
  }
  netserver.requestOnChange(VOLUME, 0);
  return true;
}

void Config::beginMenuTransaction() {
  voxone::ui_menu::MenuSettings settings{};
  {
    WriteLock lock(_persistMutex);
    settings.balance = store.balance;
    settings.bass = store.bass;
    settings.treble = store.trebble;
    settings.brightness = store.brightness;
    settings.tftTheme = static_cast<uint8_t>(store.tftTheme);
    settings.screensaverEnabled = store.screensaverEnabled;
    settings.encoderUiReversed = store.encoderUiReversed;
    settings.lineOutEnabled = store.lineOutEnabled;
    settings.lineOutVolume = store.lineOutVolume;
    _menuPersistedSettings = settings;
    _menuDraftSettings = settings;
    _menuTransactionActive = true;
    _publishRuntimeLocked();
  }
  uiMenu.beginSettingsTransaction(settings);
}

void Config::_applyMenuSettingsLive(
    const voxone::ui_menu::MenuSettings& settings) {
  player.setBalance(settings.balance);
  player.setTone(settings.bass, store.middle, settings.treble);
  player.applyCurrentVolume();
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  sourceManagerApplyConfiguredBluetoothVolume();
#endif
  if (localBacklightInstalled() && runtimeSnapshot().dspOn) {
    analogWrite(voxone::hardware::currentHardware().display.backlight,
                map(settings.brightness, 0, 100, 0, 255));
  }
}

void Config::previewMenuSettings(
    const voxone::ui_menu::MenuSettings& requested) {
  voxone::ui_menu::MenuSettings settings = requested;
  settings.balance = clampBalance(settings.balance);
  settings.bass = clampTone(settings.bass);
  settings.treble = clampTone(settings.treble);
  if (settings.brightness > 100) settings.brightness = 100;
  if (!voxone::tft_theme::valid(static_cast<TftTheme>(settings.tftTheme)))
    settings.tftTheme = static_cast<uint8_t>(THEME_DEFAULT);
  if (settings.lineOutVolume > 100) settings.lineOutVolume = 100;
  {
    WriteLock lock(_persistMutex);
    if (!_menuTransactionActive) return;
    _menuDraftSettings = settings;
    _publishRuntimeLocked();
  }
  _applyMenuSettingsLive(settings);
}

bool Config::commitMenuTransaction() {
  bool persisted = false;
  voxone::ui_menu::MenuSettings draft{};
  {
    WriteLock lock(_persistMutex);
    if (!_menuTransactionActive) return true;
    draft = _menuDraftSettings;
    _storage.update(store.balance, draft.balance);
    _storage.update(store.bass, draft.bass);
    _storage.update(store.trebble, draft.treble);
    _storage.update(store.brightness, draft.brightness);
    _storage.update(store.screensaverEnabled, draft.screensaverEnabled);

    const TftTheme theme = static_cast<TftTheme>(draft.tftTheme);
    if (store.tftTheme != theme) {
      if (!voxone::tft_theme::encode(
              theme, store.reservedWeather, sizeof(store.reservedWeather)))
        return false;
      store.tftTheme = theme;
      _storage.markDirty();
    }
    if (store.encoderUiReversed != draft.encoderUiReversed) {
      if (!voxone::encoder_ui::encode(
              draft.encoderUiReversed, store.reservedWeather,
              sizeof(store.reservedWeather)))
        return false;
      store.encoderUiReversed = draft.encoderUiReversed;
      _storage.markDirty();
    }
    if (store.lineOutEnabled != draft.lineOutEnabled ||
        store.lineOutVolume != draft.lineOutVolume) {
      if (!voxone::line_out::encode(
              draft.lineOutEnabled, draft.lineOutVolume,
              store.reservedWeather, sizeof(store.reservedWeather)))
        return false;
      store.lineOutEnabled = draft.lineOutEnabled;
      store.lineOutVolume = draft.lineOutVolume;
      _storage.markDirty();
    }

    _menuTransactionActive = false;
    _publishRuntimeLocked();
    persisted = !_startupReadOnly && persistV7();
    if (!persisted) {
      store.balance = _menuPersistedSettings.balance;
      store.bass = _menuPersistedSettings.bass;
      store.trebble = _menuPersistedSettings.treble;
      store.brightness = _menuPersistedSettings.brightness;
      store.screensaverEnabled =
          _menuPersistedSettings.screensaverEnabled;
      store.tftTheme =
          static_cast<TftTheme>(_menuPersistedSettings.tftTheme);
      store.encoderUiReversed =
          _menuPersistedSettings.encoderUiReversed;
      store.lineOutEnabled = _menuPersistedSettings.lineOutEnabled;
      store.lineOutVolume = _menuPersistedSettings.lineOutVolume;
      voxone::tft_theme::encode(
          store.tftTheme, store.reservedWeather,
          sizeof(store.reservedWeather));
      voxone::encoder_ui::encode(
          store.encoderUiReversed, store.reservedWeather,
          sizeof(store.reservedWeather));
      voxone::line_out::encode(
          store.lineOutEnabled, store.lineOutVolume, store.reservedWeather,
          sizeof(store.reservedWeather));
      _menuTransactionActive = true;
      _publishRuntimeLocked();
    }
  }
  if (!persisted) return false;
  uiMenu.configureEncoderUiReversed(draft.encoderUiReversed);
  netserver.requestOnChange(BALANCE, 0);
  netserver.requestOnChange(EQUALIZER, 0);
  netserver.requestOnChange(VOLUME, 0);
  netserver.requestOnChange(GETSCREEN, 0);
  return true;
}

void Config::discardMenuTransaction() {
  voxone::ui_menu::MenuSettings restored{};
  {
    WriteLock lock(_persistMutex);
    if (!_menuTransactionActive) return;
    _menuTransactionActive = false;
    restored.balance = store.balance;
    restored.bass = store.bass;
    restored.treble = store.trebble;
    restored.brightness = store.brightness;
    restored.tftTheme = static_cast<uint8_t>(store.tftTheme);
    restored.screensaverEnabled = store.screensaverEnabled;
    restored.encoderUiReversed = store.encoderUiReversed;
    restored.lineOutEnabled = store.lineOutEnabled;
    restored.lineOutVolume = store.lineOutVolume;
    _publishRuntimeLocked();
  }
  uiMenu.configureEncoderUiReversed(restored.encoderUiReversed);
  _applyMenuSettingsLive(restored);
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
  voxone::config_storage::ConfigEepromStorage backend;
  const bool supportsBt = voxone::hardware::hardwareCapabilities().supportsVoxOneBt;
  const auto result = _storage.factoryReset(store, supportsBt, backend,
      [this](config_t&) { _applyDefaults(); });
  if (result != voxone::config_format::ConfigWriteStatus::BLOCKED) {
    btRuntime.setEnabled(supportsBt);
    portENTER_CRITICAL(&playerVolumeMux);
    userVolume = store.lastUserVolume;
    player._syncOutputSilence(userVolume, store.lineOutEnabled,
                              store.lineOutVolume);
    portEXIT_CRITICAL(&playerVolumeMux);
    _vuThreshold.reset();
    _publishRuntimeLocked();
  }
  if (result != voxone::config_format::ConfigWriteStatus::OK)
    Serial.printf("[CONFIG] factory reset: %s\n", voxone::config_format::configWriteMessage(result));
  return result == voxone::config_format::ConfigWriteStatus::OK;
}

void Config::setTimezone(int8_t tzh, int8_t tzm) {
  // Preserve legacy timezone storage without affecting the fixed Warsaw TZ.
  WriteLock lock(_persistMutex);
  _storage.update(store.tzHour, tzh);
  _storage.update(store.tzMin, tzm);
  _publishRuntimeLocked();
  if (_storage.dirty() && !_startupReadOnly) persistV7();
}

void Config::saveVolume(){
  // The player's existing volume timer still controls when this is called.
  persistV7();
}

uint8_t Config::setVolume(uint8_t val) {
  const VolumeState state = volumeStateFromRaw(
      val, runtimeSnapshot().maximumVolume);
  return setVolumeState(state.raw, state.user);
}

uint8_t Config::setVolumeState(uint8_t raw, uint8_t user) {
  WriteLock lock(_persistMutex);
  portENTER_CRITICAL(&playerVolumeMux);
  store.volume = raw;
  userVolume = user;
  store.lastUserVolume = user;
  player._syncOutputSilence(user, store.lineOutEnabled,
                            store.lineOutVolume);
  portEXIT_CRITICAL(&playerVolumeMux);
  _publishRuntimeLocked();
  display.putRequest(DRAWVOL);
  netserver.requestOnChange(VOLUME, 0);
  return store.volume;
}

void Config::setMaximumVolume(uint8_t maximum) {
  WriteLock lock(_persistMutex);
  if (maximum < 1 || maximum > 100 || maximum == store.maximumVolume) return;
  const uint8_t oldMaximum = store.maximumVolume;
  portENTER_CRITICAL(&playerVolumeMux);
  const uint8_t oldUser = userVolume;
  portEXIT_CRITICAL(&playerVolumeMux);
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
#if VOXONE_INCLUDE_DSP
  const bool toneLocked = voxone::dsp::lockDspToneMutation();
#endif
  WriteLock lock(_persistMutex);
  bass = clampTone(bass);
  middle = clampTone(middle);
  trebble = clampTone(trebble);
  bool persisted = true;
  if (store.bass != bass || store.middle != middle || store.trebble != trebble ||
      _storage.dirty() || _storage.mode() != voxone::config_format::ConfigStorageMode::V7) {
    _storage.update(store.bass, bass);
    _storage.update(store.middle, middle);
    _storage.update(store.trebble, trebble);
    _publishRuntimeLocked();
    persisted = persistV7();
  }
  player.setTone(store.bass, store.middle, store.trebble);
  netserver.requestOnChange(EQUALIZER, 0);
#if VOXONE_INCLUDE_DSP
  voxone::dsp::dspTransportToneChanged(
      {store.bass, store.middle, store.trebble});
  if (toneLocked) voxone::dsp::unlockDspToneMutation();
#endif
  return persisted;
}

void Config::setSmartStart(uint8_t ss) {
  saveValue(&store.smartstart, ss);
}

void Config::setBalance(int8_t balance) {
  balance = clampBalance(balance);
  saveValue(&store.balance, balance);
  player.setBalance(balance);
  netserver.requestOnChange(BALANCE, 0);
}

uint8_t Config::setLastStation(uint16_t val) {
  lastStation(val);
  return static_cast<uint8_t>(runtimeSnapshot().lastStation);
}

void Config::lastStation(uint16_t newstation) {
  WriteLock lock(_persistMutex);
  const bool clearMarker = newstation != 0 &&
                           store._reserved == VOXONE_NO_STATION_MARKER;
  if (clearMarker)
    _storage.update(store._reserved, static_cast<uint16_t>(0));
  _storage.update(store.lastStation, newstation, clearMarker);
  _publishRuntimeLocked();
  if (_storage.dirty() && !_startupReadOnly) persistV7();
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
  _publishRuntimeLocked();
  // Playlist recovery can call this from init(); preserve EEPROM during load.
  if (_startupReadOnly) return true;
  if (persistV7()) return true;
  store.lastStation = previous;
  store._reserved = previousMarker;
  _publishRuntimeLocked();
  // Keep the previous RAM selection on failure. Never attempt a legacy
  // rollback write or claim the failed commit was undone in flash.
  return false;
}

uint8_t Config::setLastSSID(uint8_t val) {
  saveValue(&store.lastSSID, val);
  return runtimeSnapshot().lastSsid;
}

void Config::setTitle(const char* title) {
  resetVuThreshold();
  memset(config.station.title, 0, BUFLEN);
  voxone::utf8::copy(config.station.title, BUFLEN, title);
  u8fix(config.station.title);
  netserver.requestOnChange(TITLE, 0);
  display.putRequest(NEWTITLE);
}

void Config::setStation(const char* station) {
  memset(config.station.name, 0, BUFLEN);
  voxone::utf8::copy(config.station.name, BUFLEN, station);
  u8fix(config.station.name);
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
  if (ls == 0 && intentionalNoStation()) {
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

bool Config::parseSsid(const char* line, char* ssid, char* pass) {
  if (!line || !ssid || !pass) return false;
  voxone::wifi_config::Profile parsed{};
  if (voxone::wifi_config::parseRecord(
          reinterpret_cast<const uint8_t*>(line), strlen(line), parsed) !=
      voxone::wifi_config::ParseStatus::Ok)
    return false;
  memcpy(ssid, parsed.ssid, sizeof(parsed.ssid));
  memcpy(pass, parsed.password, sizeof(parsed.password));
  return true;
}

bool Config::saveWifiCredentials(const char* ssid, const char* password) {
  if (!apWifiCredentialsValid(ssid, password)) return false;
  const size_t ssidLength = strlen(ssid);
  const size_t passwordLength = strlen(password);
  const size_t recordSize = ssidLength + 1 + passwordLength + 1;
  uint8_t record[voxone::wifi_config::kMaxRecordBytes];
  if (recordSize > sizeof(record)) return false;
  memcpy(record, ssid, ssidLength);
  record[ssidLength] = '\t';
  memcpy(record + ssidLength + 1, password, passwordLength);
  record[recordSize - 1] = '\n';
  WifiConfigSpiffsStorage storage;
  return voxone::wifi_config_atomic::replace(storage, record, recordSize);
}

void Config::setTimeConf(){
  static_assert(sizeof(store.sntp1) == voxone::sntp::ServerNames::kCapacity,
                "SNTP storage capacity changed");
  static_assert(sizeof(store.sntp2) == voxone::sntp::ServerNames::kCapacity,
                "SNTP storage capacity changed");
  WriteLock lock(_persistMutex);
  timekeeper.watchNtp();
  _sntpServers.replace(
      store.sntp1, store.sntp2,
      []() {
        if (sntp_enabled()) sntp_stop();
      },
      [](const char* primary, const char* secondary) {
        if (primary[0] && secondary[0]) {
          configTzTime(WARSAW_TZ, primary, secondary);
        } else if (primary[0]) {
          configTzTime(WARSAW_TZ, primary);
        }
      });
}

bool Config::initNetwork() {
  memset(ssids, 0, sizeof(ssids));
  ssidsCount = 0;
  if (!wifiConfigReady) return false;
  if (!SPIFFS.exists(SSIDS_PATH)) {
    Serial.println("##[WIFI-CONFIG]# status=missing profiles=0");
    return true;
  }
  File file = SPIFFS.open(SSIDS_PATH, "r");
  if (!file || file.isDirectory() ||
      file.size() > voxone::wifi_config::kMaxFileBytes) {
    file.close();
    wifiConfigReady = false;
    Serial.println("##[ERROR]# Wi-Fi config read failed status=file_error");
    return false;
  }
  uint8_t bytes[voxone::wifi_config::kMaxFileBytes];
  const size_t size = file.size();
  if (size && file.read(bytes, size) != size) {
    file.close();
    wifiConfigReady = false;
    Serial.println("##[ERROR]# Wi-Fi config read failed status=io_error");
    return false;
  }
  file.close();
  const auto parsed = voxone::wifi_config::parseFile(
      bytes, size, ssids, voxone::wifi_config::kMaxProfiles);
  if (!parsed.valid()) {
    wifiConfigReady = false;
    Serial.printf("##[ERROR]# Wi-Fi config invalid status=%s record=%u\n",
                  voxone::wifi_config::parseStatusName(parsed.status),
                  static_cast<unsigned>(parsed.recordNumber));
    return false;
  }
  ssidsCount = static_cast<uint8_t>(parsed.profileCount);
  Serial.printf("##[WIFI-CONFIG]# status=%s profiles=%u\n",
                voxone::wifi_config::parseStatusName(parsed.status),
                static_cast<unsigned>(ssidsCount));
  return true;
}

void Config::setBrightness(bool dosave){
  if (!localBacklightInstalled()) return;
  bool wake = false;
  uint8_t brightness = 0;
  {
    WriteLock lock(_persistMutex);
    wake = !store.dspon && dosave;
    brightness = store.brightness;
    if(!store.dspon) store.dspon = true;
    if(dosave){
      _storage.update(store.brightness, store.brightness, true);
      _storage.update(store.dspon, store.dspon, true);
    }
    _publishRuntimeLocked();
    if (dosave && !_startupReadOnly) persistV7();
  }
  if(wake) display.wakeup();
  analogWrite(voxone::hardware::currentHardware().display.backlight,
              map(brightness, 0, 100, 0, 255));
  if (dosave) netserver.requestOnChange(GETSCREEN, 0);
}

void Config::setRuntimeBrightness(uint8_t value) {
  WriteLock lock(_persistMutex);
  store.brightness = value;
  _publishRuntimeLocked();
}

void Config::_publishRuntimeLocked() {
  voxone::config_runtime::Snapshot runtime{};
  runtime.volume = store.volume;
  runtime.userVolume = userVolume;
  runtime.lastUserVolume = store.lastUserVolume;
  runtime.maximumVolume = store.maximumVolume;
  runtime.startupMode = store.startupMode;
  runtime.startupFixedVolume = store.startupFixedVolume;
  runtime.balance = store.balance;
  runtime.bass = store.bass;
  runtime.middle = store.middle;
  runtime.treble = store.trebble;
  runtime.lastStation = store.lastStation;
  runtime.countStation = store.countStation;
  runtime.lastSsid = store.lastSSID;
  runtime.intentionalNoStation =
      store._reserved == VOXONE_NO_STATION_MARKER;
  runtime.audioInfo = store.audioinfo;
  runtime.smartStart = store.smartstart;
  runtime.vuMeter = store.vumeter;
  runtime.softApDelay = store.softapdelay;
  runtime.flipScreen = store.flipscreen;
  runtime.invertDisplay = store.invertdisplay;
  runtime.numPlaylist = store.numplaylist;
  runtime.dspOn = store.dspon;
  runtime.brightness = store.brightness;
  runtime.contrast = store.contrast;
  runtime.screensaverEnabled = store.screensaverEnabled;
  runtime.screensaverTimeout = store.screensaverTimeout;
  runtime.screensaverBlank = store.screensaverBlank;
  runtime.screensaverPlayingEnabled = store.screensaverPlayingEnabled;
  runtime.screensaverPlayingTimeout = store.screensaverPlayingTimeout;
  runtime.screensaverPlayingBlank = store.screensaverPlayingBlank;
  runtime.skipPlaylistUpDown = store.skipPlaylistUpDown;
  runtime.audioBuffer = store.abuff;
  runtime.watchdog = store.watchdog;
  runtime.timeSyncInterval = store.timeSyncInterval;
  runtime.timeSyncIntervalRtc = store.timeSyncIntervalRTC;
  runtime.timezoneHour = store.tzHour;
  runtime.timezoneMinute = store.tzMin;
  runtime.encoderAcceleration = store.encacc;
  runtime.volumeSteps = store.volsteps;
  runtime.haDiscoveryEnabled = store.haDiscoveryEnabled;
  runtime.tftTheme = static_cast<uint8_t>(store.tftTheme);
  runtime.powerMode = static_cast<uint8_t>(store.powerMode);
  runtime.encoderUiReversed = store.encoderUiReversed;
  runtime.lineOutEnabled = store.lineOutEnabled;
  runtime.lineOutVolume = store.lineOutVolume;
  if (_menuTransactionActive) {
    runtime.balance = _menuDraftSettings.balance;
    runtime.bass = _menuDraftSettings.bass;
    runtime.treble = _menuDraftSettings.treble;
    runtime.brightness = _menuDraftSettings.brightness;
    runtime.screensaverEnabled =
        _menuDraftSettings.screensaverEnabled;
    runtime.tftTheme = _menuDraftSettings.tftTheme;
    runtime.encoderUiReversed =
        _menuDraftSettings.encoderUiReversed;
    runtime.lineOutEnabled = _menuDraftSettings.lineOutEnabled;
    runtime.lineOutVolume = _menuDraftSettings.lineOutVolume;
  }
  strlcpy(runtime.mdnsName, store.mdnsname, sizeof(runtime.mdnsName));
  strlcpy(runtime.haName, store.haName, sizeof(runtime.haName));
  _runtime.publish(runtime);
}

void Config::setDspOn(bool dspon, bool saveval){
  if(saveval){
    WriteLock lock(_persistMutex);
    _storage.update(store.dspon, dspon, true);
    _publishRuntimeLocked();
    if (!_startupReadOnly) persistV7();
  }
  if (!localDisplayInstalled()) return;
  if(!dspon){
    if (localBacklightInstalled())
      analogWrite(voxone::hardware::currentHardware().display.backlight, 0);
    display.deepsleep();
  }else{
    display.wakeup();
    if (localBacklightInstalled())
      analogWrite(voxone::hardware::currentHardware().display.backlight, map(
          runtimeSnapshot().brightness, 0, 100, 0, 255));
  }
}

void Config::doSleep(){
  if (localBacklightInstalled())
    analogWrite(voxone::hardware::currentHardware().display.backlight, 0);
  display.deepsleep();
#if !defined(ARDUINO_ESP32C3_DEV)
  if(WAKE_PIN!=255) esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_PIN, LOW);
  esp_sleep_enable_timer_wakeup(config.sleepfor * 60 * 1000000ULL);
  esp_deep_sleep_start();
#endif
}

void Config::doSleepW(){
  if (localBacklightInstalled())
    analogWrite(voxone::hardware::currentHardware().display.backlight, 0);
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
  BOOTLOG("reset reason:\t%d", static_cast<int>(esp_reset_reason()));
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
  const auto& audioPins = voxone::hardware::currentHardware().audioOut;
  BOOTLOG("audio:\t\t%s (%d, %d, %d)", "I2S", audioPins.dout,
          audioPins.bclk, audioPins.ws);
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
