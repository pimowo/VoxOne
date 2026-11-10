#ifndef config_h
#define config_h
#pragma once
#include "Arduino.h"
#include <SPI.h>
#include <SPIFFS.h>
#include <EEPROM.h>
#include "config_persistence.h"
#include "config_runtime_snapshot.h"
#include "ha_name_config.h"
#include "tft_theme_config.h"
#include "power_mode_config.h"
#include "encoder_ui_config.h"
#include "line_out_config.h"
#include "sntp_server_lifetime.h"
#include "www_asset_integrity.h"
#include "wifi_config_parser.h"
#include "ui_menu.h"
#include <freertos/semphr.h>
#include "../displays/widgets/widgetsconfig.h" //BitrateFormat

#define EEPROM_SIZE       768
#define EEPROM_START      500
// Addresses 0..499 are reserved for historical IR data; never reuse them.
#define PLAYLIST_PATH     "/data/stations.tsv"
#define SSIDS_PATH        "/data/wifi.csv"
#define INDEX_PATH        "/data/stations.idx"

#define MDNS_LENGTH 24

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  #define ESP_ARDUINO_3 1
#endif

#define CONFIG_VERSION  7
constexpr uint16_t VOXONE_NO_STATION_MARKER = 0xC302;
enum StartupVolumeMode : uint8_t { STARTUP_LAST = 0, STARTUP_FIXED = 1 };

void u8fix(char *src);

struct theme_t {
  uint16_t background;
  uint16_t meta;
  uint16_t metabg;
  uint16_t metafill;
  uint16_t title1;
  uint16_t title2;
  uint16_t digit;
  uint16_t div;
  uint16_t vumax;
  uint16_t vumin;
  uint16_t clock;
  uint16_t seconds;
  uint16_t dow;
  uint16_t date;
  uint16_t ip;
  uint16_t vol;
  uint16_t rssi;
  uint16_t bitrate;
  uint16_t volbarout;
  uint16_t volbarin;
  uint16_t plcurrent;
  uint16_t plcurrentbg;
  uint16_t plcurrentfill;
  uint16_t playlist[5];
};
struct config_t
{
  uint16_t  config_set; // Runtime header; persistence uses explicit v7 serialization.
  uint16_t  version;
  uint8_t   volume;
  int8_t    balance;
  int8_t    trebble;
  int8_t    middle;
  int8_t    bass;
  uint16_t  lastStation;
  uint16_t  countStation;
  uint8_t   lastSSID;
  bool      audioinfo;
  uint8_t   smartstart;
  int8_t    tzHour;          // legacy/reserved: fixed Europe/Warsaw TZ is used
  int8_t    tzMin;           // legacy/reserved: keep EEPROM layout
  uint16_t  timezoneOffset;  // legacy/reserved: keep EEPROM layout
  bool      vumeter;
  uint8_t   softapdelay;
  bool      flipscreen;
  bool      invertdisplay;
  bool      numplaylist;
  bool      reservedInput0;  // retired input bytes; keep serialized offsets
  bool      reservedInput1;
  bool      dspon;
  uint8_t   brightness;
  uint8_t   contrast;
  char      sntp1[35];
  char      sntp2[35];
  uint8_t   reservedWeather[79];
  uint16_t  _reserved; // VoxOne: intentional current=0 marker; EEPROM layout unchanged
  uint16_t  reservedSdStation;
  bool      reservedSdFlags;
  uint8_t   volsteps;
  uint16_t  encacc;
  uint8_t   reservedPlayMode;
  uint8_t   irtlp;
  bool      btnpullup;
  uint16_t  btnlongpress;
  uint16_t  btnclickticks;
  uint16_t  btnpressticks;
  bool      encpullup;
  bool      enchalf;
  bool      enc2pullup;
  bool      enc2half;
  bool      forcemono;
  bool      i2sinternal;
  bool      rotate90;
  bool      screensaverEnabled;
  uint16_t  screensaverTimeout;
  bool      screensaverBlank;
  bool      screensaverPlayingEnabled;
  uint16_t  screensaverPlayingTimeout;
  bool      screensaverPlayingBlank;
  char      mdnsname[24];
  bool      skipPlaylistUpDown;
  uint16_t  abuff;
  bool      reservedTelnet;
  bool      haDiscoveryEnabled;
  // Config v7 runtime field. Persistence is encoded into the retired weather
  // bytes so the 255-byte wire format, CRC, backup and downgrade guard remain
  // unchanged.
  char      haName[voxone::ha_name::kRuntimeCapacity];
  TftTheme  tftTheme;
  PowerMode powerMode;
  bool      encoderUiReversed;
  bool      lineOutEnabled;
  uint8_t   lineOutVolume;
  bool      watchdog; // legacy/reserved: runtime always forces audio watchdog on
  uint16_t  timeSyncInterval;
  uint16_t  timeSyncIntervalRTC;
  uint16_t  reservedWeatherSyncInterval;
  uint8_t   maximumVolume;
  uint8_t   startupMode;
  uint8_t   startupFixedVolume;
  uint8_t   lastUserVolume;
};
static_assert(sizeof(((config_t*)nullptr)->haName) ==
                  voxone::ha_name::kRuntimeCapacity,
              "HA name runtime capacity changed");

struct station_t
{
  uint64_t id = 0;
  uint8_t metadataMode = 0;
  char name[BUFLEN];
  char url[BUFLEN];
  char title[BUFLEN];
  uint16_t bitrate;
  int  ovol;
};

using neworkItem = voxone::wifi_config::Profile;

class Config {
  public:
    config_t store;
    uint8_t userVolume = 0;
    bool volumeBootDirty = false;
    station_t station;
    theme_t   theme;
    BitrateFormat configFmt = BF_UNKNOWN;
    neworkItem ssids[voxone::wifi_config::kMaxProfiles];
    uint8_t ssidsCount;
    uint16_t sleepfor;
    bool     spiffsMounted;
    bool     webUpdateDataReady;
    bool     wifiConfigReady;
    bool     currentWwwReady;
    voxone::www_integrity::Report wwwAssetIntegrity;
    bool     radioPlaylistReady;
    uint16_t screensaverTicks;
    uint16_t screensaverPlayingTicks;
    bool     isScreensaver;
    char      tmpBuf[BUFLEN];
    char     tmpBuf2[BUFLEN];
    char       ipBuf[16];
    char _stationBuf[BUFLEN/2];
  public:
    Config() {};
    //void save();
    void init();
    bool persistV7();
    bool setHaName(const char* value);
    bool setTftTheme(TftTheme theme);
    bool setPowerMode(PowerMode mode);
    bool setEncoderUiReversed(bool reversed);
    bool setLineOutEnabled(bool enabled);
    bool setLineOutVolume(uint8_t volume);
    void beginMenuTransaction();
    void previewMenuSettings(const voxone::ui_menu::MenuSettings& settings);
    bool commitMenuTransaction();
    void discardMenuTransaction();
    bool setBtEnabled(bool enabled);
    voxone::config_format::ConfigStorageMode storageMode() const { return _storage.mode(); }
    voxone::config_format::ConfigRecordStatus startupStatus() const { return _startupStatus; }
    bool storedBtEnabled() const { return _storage.storedBtEnabled(); }
    voxone::config_runtime::Snapshot runtimeSnapshot() const { return _runtime.read(); }
    bool audioVuMeterEnabled() const { return _runtime.audioVuMeterEnabled(); }
    void resetVuThreshold() { _vuThreshold.reset(); }
    void observeVuLevel(uint16_t value) { _vuThreshold.observe(value); }
    void reduceVuThreshold(uint16_t value) { _vuThreshold.reduce(value); }
    uint16_t vuThresholdSnapshot() const { return _vuThreshold.read(); }
    void loadTheme();
    uint8_t setVolume(uint8_t val);
    uint8_t setVolumeState(uint8_t raw, uint8_t user);
    uint8_t applyConfiguredStartupVolume();
    void saveVolume();
    void setMaximumVolume(uint8_t maximum);
    void setStartupMode(uint8_t mode);
    void setStartupFixedVolume(uint8_t user);
    // Returns whether the EEPROM commit succeeded (or no persistence was needed).
    bool setTone(int8_t bass, int8_t middle, int8_t trebble);
    void setBalance(int8_t balance);
    uint8_t setLastStation(uint16_t val);
    bool setLastStationChecked(uint16_t val, bool intentionalZero=true);
    uint8_t setLastSSID(uint8_t val);
    void setTitle(const char* title);
    void setStation(const char* station);
    bool parseSsid(const char* line, char* ssid, char* pass);
    bool loadStation(uint16_t station);
    bool initNetwork();
    void setTimeConf();
    bool saveWifiCredentials(const char* ssid, const char* password);
    void setSmartStart(uint8_t ss);
    void setBitrateFormat(BitrateFormat fmt) { configFmt = fmt; }
    void initPlaylist();
    uint16_t playlistLength();
    uint16_t lastStation(){
      return runtimeSnapshot().lastStation;
    }
    bool intentionalNoStation() const {
      return runtimeSnapshot().intentionalNoStation;
    }
    void lastStation(uint16_t newstation);
    char * stationByNum(uint16_t num);
    void setTimezone(int8_t tzh, int8_t tzm);
    void setBrightness(bool dosave=false);
    void setRuntimeBrightness(uint8_t value);
    void setDspOn(bool dspon, bool saveval = true);
    void sleepForAfter(uint16_t sleepfor, uint16_t sleepafter=0);
    void bootInfo();
    void doSleepW();
    void initRadioPlaylist();
    void reset();
    void enableScreensaver(bool val);
    void setScreensaverTimeout(uint16_t val);
    void setScreensaverBlank(bool val);
    void setScreensaverPlayingEnabled(bool val);
    void setScreensaverPlayingTimeout(uint16_t val);
    void setScreensaverPlayingBlank(bool val);
    void setSntpOne(const char *val);
    void waitConnection();
    char * ipToStr(IPAddress ip);
    bool prepareForPlaying(uint16_t stationId, bool sourceResume = false);
    void configPostPlaying();
    bool isRTCFound(){ return _rtcFound; };
    template <typename T>
    void saveValue(T *field, const T &value, bool commit=true, bool force=false){
      WriteLock lock(_persistMutex);
      _storage.update(*field, value, force);
      _publishRuntimeLocked();
      if (commit && _storage.dirty() && !_startupReadOnly) persistV7();
    }
    void saveValue(char *field, const char *value, size_t N, bool commit=true, bool force=false) {
      WriteLock lock(_persistMutex);
      _storage.update(field, value, N, force);
      _publishRuntimeLocked();
      if (commit && _storage.dirty() && !_startupReadOnly) persistV7();
    }
    uint32_t getChipId(){
      uint32_t chipId = 0;
      for(int i=0; i<17; i=i+8) {
        chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
      }
      return chipId;
    }
  private:
    friend class Player;
    voxone::config_format::ConfigRecordStatus _startupStatus =
        voxone::config_format::ConfigRecordStatus::INVALID_ARGUMENT;
    class WriteLock {
     public:
      explicit WriteLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        if (mutex_) xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
      }
      ~WriteLock() { if (mutex_) xSemaphoreGiveRecursive(mutex_); }
      WriteLock(const WriteLock&) = delete;
      WriteLock& operator=(const WriteLock&) = delete;
     private:
      SemaphoreHandle_t mutex_;
    };
    SemaphoreHandle_t _persistMutex = nullptr;
    voxone::sntp::ServerNames _sntpServers;
    voxone::config_runtime::SnapshotStore _runtime;
    voxone::config_runtime::VuThresholdRuntime _vuThreshold;
    voxone::config_format::ConfigPersistence _storage;
    bool _startupReadOnly = false;
    bool _menuTransactionActive = false;
    voxone::ui_menu::MenuSettings _menuPersistedSettings{};
    voxone::ui_menu::MenuSettings _menuDraftSettings{};
    bool _rtcFound;
    bool setDefaults();
    void _applyDefaults();
    static void doSleep();
    uint16_t color565(uint8_t r, uint8_t g, uint8_t b);
    void _makeDefaultMdnsName(char *buffer, size_t size);
    void _normalizeProductConfig();
    void _normalizeAudioConfig();
    void _publishRuntimeLocked();
    void _applyMenuSettingsLive(
        const voxone::ui_menu::MenuSettings& settings);
    void _initHW();
};

extern Config config;
#if DSP_HSPI
extern SPIClass  SPI2;
#endif

#endif
