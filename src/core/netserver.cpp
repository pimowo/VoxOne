#include "options.h"
#include "Arduino.h"
#include <SPIFFS.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <nvs.h>
#include <memory>
#include "config.h"
#include "netserver.h"
#include "player.h"
#include "serialcli.h"
#include "display.h"
#include "network.h"
#include "mqtt.h"
#include "controls.h"
#include "commandhandler.h"
#include "timekeeper.h"
#include "../displays/dspcore.h"
#include "../displays/widgets/widgetsconfig.h" //BitrateFormat

#if DSP_MODEL==DSP_DUMMY
#define DUMMYDISPLAY
#endif

#ifdef USE_SD
#include "sdmanager.h"
#endif
#ifndef MIN_MALLOC
#define MIN_MALLOC 24112
#endif
#ifndef NSQ_SEND_DELAY
  //#define NSQ_SEND_DELAY       portMAX_DELAY
  #define NSQ_SEND_DELAY       pdMS_TO_TICKS(300)
#endif
#ifndef NS_QUEUE_TICKS
  //#define NS_QUEUE_TICKS pdMS_TO_TICKS(2)
  #define NS_QUEUE_TICKS 0
#endif
#ifndef NS_VOLUME_INTERVAL_MS
  #define NS_VOLUME_INTERVAL_MS 75
#endif

#ifdef DEBUG_V
#define DBGVB( ... ) { char buf[200]; sprintf( buf, __VA_ARGS__ ) ; Serial.print("[DEBUG]\t"); Serial.println(buf); }
#else
#define DBGVB( ... )
#endif

//#define CORS_DEBUG //Enable CORS policy: 'Access-Control-Allow-Origin' (for testing)

NetServer netserver;
portMUX_TYPE netserverVolumeMux = portMUX_INITIALIZER_UNLOCKED;

AsyncWebServer webserver(80);
AsyncWebSocket websocket("/ws");

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleStationsRead(AsyncWebServerRequest *request);
void handleIndex(AsyncWebServerRequest * request);
void handleNotFound(AsyncWebServerRequest * request);
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

bool  shouldReboot  = false;
uint32_t webUpdateRebootAt = 0;
#ifdef MQTT_ROOT_TOPIC
//Ticker mqttplaylistticker;
bool  mqttplaylistblock = false;
void mqttplaylistSend() {
  mqttplaylistblock = true;
//  mqttplaylistticker.detach();
  mqttPublishPlaylist();
  mqttplaylistblock = false;
}
#endif

namespace {
struct WebUpdateFile {
  const char* path;
  const char* key;
};
constexpr WebUpdateFile kWebUpdateFiles[] = {
  {SSIDS_PATH, "wifi"},
  {PLAYLIST_PATH, "playlist"},
  {PLAYLIST_SD_PATH, "playlistsd"}
};
constexpr char kWebUpdateNamespace[] = "voxupdate";
AsyncWebServerRequest* activeUpdateRequest = nullptr;

struct WebUpdateSession {
  size_t expected;
  size_t limit;
  int target;
  bool started;
  bool finished;
  const char* error;
};

bool backupWebUpdateData(const char*& error) {
  nvs_handle_t handle;
  if (nvs_open(kWebUpdateNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    error = "Cannot open NVS backup";
    return false;
  }
  uint8_t pending = 0;
  if (nvs_get_u8(handle, "pending", &pending) == ESP_OK && pending) {
    error = "Previous SPIFFS backup is pending; reboot before retry";
    nvs_close(handle);
    return false;
  }
  if (nvs_erase_all(handle) != ESP_OK) {
    error = "Could not initialize NVS backup";
    nvs_close(handle);
    return false;
  }
  for (const auto& item : kWebUpdateFiles) {
    File file = SPIFFS.open(item.path, "r");
    if (!file) continue;
    const size_t size = file.size();
    uint8_t* bytes = static_cast<uint8_t*>(malloc(size ? size : 1));
    if (!bytes || file.read(bytes, size) != size ||
        nvs_set_blob(handle, item.key, bytes, size) != ESP_OK) {
      free(bytes);
      file.close();
      error = "NVS backup has insufficient space or could not save data";
      nvs_erase_all(handle);
      nvs_commit(handle);
      nvs_close(handle);
      return false;
    }
    free(bytes);
    file.close();
  }
  if (nvs_set_u8(handle, "pending", 1) != ESP_OK ||
      nvs_commit(handle) != ESP_OK) {
    error = "Could not commit NVS backup";
    nvs_close(handle);
    return false;
  }
  nvs_close(handle);
  if (nvs_open(kWebUpdateNamespace, NVS_READONLY, &handle) != ESP_OK) {
    error = "Could not reopen NVS backup";
    return false;
  }
  if (nvs_get_u8(handle, "pending", &pending) != ESP_OK || pending != 1) {
    error = "Could not verify NVS backup";
    nvs_close(handle);
    return false;
  }
  nvs_close(handle);
  return true;
}
}  // namespace

bool restoreWebUpdateData() {
  nvs_handle_t handle;
  if (nvs_open(kWebUpdateNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  uint8_t pending = 0;
  if (nvs_get_u8(handle, "pending", &pending) != ESP_OK || pending != 1) {
    nvs_close(handle);
    return true;
  }
  Serial.println("##[BOOT]# Restoring Web Update data from NVS");
  for (const auto& item : kWebUpdateFiles) {
    size_t size = 0;
    esp_err_t result = nvs_get_blob(handle, item.key, nullptr, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) continue;
    uint8_t* bytes = static_cast<uint8_t*>(malloc(size ? size : 1));
    if (result != ESP_OK || !bytes ||
        nvs_get_blob(handle, item.key, bytes, &size) != ESP_OK) {
      free(bytes);
      Serial.printf("##[ERROR]# Web Update restore failed: %s (NVS read)\n", item.path);
      nvs_close(handle);
      return false;
    }
    File file = SPIFFS.open(item.path, "w");
    const bool written = file && file.write(bytes, size) == size;
    file.close();
    free(bytes);
    if (!written) {
      Serial.printf("##[ERROR]# Web Update restore failed: %s (SPIFFS write)\n", item.path);
      nvs_close(handle);
      return false;
    }
    Serial.printf("##[BOOT]# Restored %s (%u B)\n", item.path, static_cast<unsigned>(size));
  }
  const bool cleared = nvs_erase_all(handle) == ESP_OK && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  if (!cleared) Serial.println("##[ERROR]# Web Update backup cleanup failed");
  return cleared;
}

char* updateError() {
  sprintf(netserver.nsBuf, "Update failed with error (%d)<br /> %s", (int)Update.getError(), Update.errorString());
  return netserver.nsBuf;
}

bool NetServer::begin(bool quiet) {
  if(network.status==SDREADY) return true;
  if(!quiet) Serial.print("##[BOOT]#\tnetserver.begin\t");
  importRequest = IMDONE;
  irRecordEnable = false;
  playerBufMax = psramInit()?300000:1600 * config.store.abuff;
  _volumeUpdatePending = false;
  _lastVolumeUpdate = millis() - NS_VOLUME_INTERVAL_MS;
  nsQueue = xQueueCreate( 20, sizeof( nsRequestParams_t ) );
  while(nsQueue==NULL){;}

  webserver.on("/", HTTP_ANY, handleIndex);
  webserver.on("/api/stations", HTTP_GET, handleStationsRead);
  webserver.onNotFound(handleNotFound);
  webserver.onFileUpload(handleUpload);

  // Preview HTML must revalidate after a SPIFFS update; legacy WWW keeps its cache policy.
  webserver.serveStatic("/voxone.html", SPIFFS, "/www/voxone.html").setCacheControl("no-cache, max-age=0, must-revalidate");
  webserver.serveStatic("/", SPIFFS, "/www/").setCacheControl("max-age=31536000");
#ifdef CORS_DEBUG
  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Origin"), F("*"));
  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Headers"), F("content-type"));
#endif
  webserver.begin();
  //if(strlen(config.store.mdnsname)>0)
  //  MDNS.begin(config.store.mdnsname);
  websocket.onEvent(onWsEvent);
  webserver.addHandler(&websocket);
  if(!quiet) Serial.println("done");
  return true;
}

size_t NetServer::chunkedHtmlPageCallback(uint8_t* buffer, size_t maxLen, size_t index){
  File requiredfile;
  bool sdpl = strcmp(netserver.chunkedPathBuffer, PLAYLIST_SD_PATH) == 0;
  if(sdpl){
    requiredfile = config.SDPLFS()->open(netserver.chunkedPathBuffer, "r");
  }else{
    requiredfile = SPIFFS.open(netserver.chunkedPathBuffer, "r");
  }
  if (!requiredfile) return 0;
  size_t filesize = requiredfile.size();
  size_t needread = filesize - index;
  if (!needread) {
    requiredfile.close();
    display.unlock();
    return 0;
  }
  #ifdef MAX_PL_READ_BYTES
    if(maxLen>MAX_PL_READ_BYTES) maxLen=MAX_PL_READ_BYTES;
  #endif
  size_t canread = (needread > maxLen) ? maxLen : needread;
  DBGVB("[%s] seek to %d in %s and read %d bytes with maxLen=%d", __func__, index, netserver.chunkedPathBuffer, canread, maxLen);
  //netserver.loop();
  requiredfile.seek(index, SeekSet);
  requiredfile.read(buffer, canread);
  index += canread;
  if (requiredfile) requiredfile.close();
  return canread;
}

void NetServer::chunkedHtmlPage(const String& contentType, AsyncWebServerRequest *request, const char * path) {
  memset(chunkedPathBuffer, 0, sizeof(chunkedPathBuffer));
  strlcpy(chunkedPathBuffer, path, sizeof(chunkedPathBuffer)-1);
  AsyncWebServerResponse *response;
  #ifndef NETSERVER_LOOP1
  display.lock();
  #endif
  response = request->beginChunkedResponse(contentType, chunkedHtmlPageCallback);
  response->addHeader("Cache-Control","max-age=31536000");
  request->send(response);
}

#ifndef DSP_NOT_FLIPPED
  #define DSP_CAN_FLIPPED true
#else
  #define DSP_CAN_FLIPPED false
#endif

const char *getFormat(BitrateFormat _format) {
  switch (_format) {
    case BF_MP3:  return "MP3";
    case BF_AAC:  return "AAC";
    case BF_FLAC: return "FLC";
    case BF_OGG:  return "OGG";
    case BF_WAV:  return "WAV";
    default:      return "bitrate";
  }
}

static bool appendJsonEscaped(char *output, size_t capacity, size_t &used, const char *value, bool truncateForWs = false) {
  const size_t valueLength = strlen(value);
  for (size_t i = 0; i < valueLength;) {
    const unsigned char ch = static_cast<unsigned char>(value[i]);
    char encoded[7];
    size_t count = 1;
    size_t consumed = 1;
    if (ch == '"' || ch == '\\') {
      encoded[0] = '\\';
      encoded[1] = static_cast<char>(ch);
      count = 2;
    } else if (ch < 0x20) {
      snprintf(encoded, sizeof(encoded), "\\u%04x", ch);
      count = 6;
    } else if (ch >= 0x80) {
      const size_t utf8Length = ch >= 0xF0 ? (ch <= 0xF4 ? 4 : 0) :
                                ch >= 0xE0 ? 3 : ch >= 0xC2 ? 2 : 0;
      bool valid = utf8Length != 0 && i + utf8Length <= valueLength;
      for (size_t j = 1; valid && j < utf8Length; ++j) {
        valid = (static_cast<unsigned char>(value[i + j]) & 0xC0) == 0x80;
      }
      if (valid) {
        count = consumed = utf8Length;
        memcpy(encoded, value + i, count);
      } else {
        encoded[0] = '?';
      }
    } else {
      encoded[0] = static_cast<char>(ch);
    }
    if (used + count + (truncateForWs ? 5 : 1) > capacity) {
      if (truncateForWs) break;
      return false;
    }
    memcpy(output + used, encoded, count);
    used += count;
    i += consumed;
  }
  output[used] = '\0';
  return true;
}

static void formatWsTextPayload(char *output, size_t capacity, const char *id, const char *value) {
  const int prefix = snprintf(output, capacity,
      "{\"payload\":[{\"id\":\"%s\", \"value\":\"", id);
  if (prefix < 0 || static_cast<size_t>(prefix) >= capacity) {
    output[0] = '\0';
    return;
  }
  size_t used = static_cast<size_t>(prefix);
  if (!appendJsonEscaped(output, capacity, used, value, true) || used + 5 > capacity) {
    output[0] = '\0';
    return;
  }
  memcpy(output + used, "\"}]}", 5);
}

static bool readIndexedStation(File &playlist, File &index, uint16_t number, station_t &station) {
  const uint32_t indexOffset = (static_cast<uint32_t>(number) - 1) * sizeof(uint32_t);
  if (number == 0 || !index.seek(indexOffset, SeekSet)) return false;
  uint32_t position = 0;
  if (index.readBytes(reinterpret_cast<char*>(&position), sizeof(position)) != sizeof(position) ||
      position >= playlist.size() || !playlist.seek(position, SeekSet)) return false;

  char line[BUFLEN * 3];
  const size_t length = playlist.readBytesUntil('\n', line, sizeof(line) - 1);
  if (length == 0 || length >= sizeof(line) - 1) return false;
  line[length] = '\0';
  const char *nameEnd = strchr(line, '\t');
  const char *urlEnd = nameEnd ? strchr(nameEnd + 1, '\t') : nullptr;
  if (!nameEnd || !urlEnd || nameEnd - line >= BUFLEN ||
      urlEnd - nameEnd - 1 >= BUFLEN) return false;
  return config.parseCSV(line, station.name, station.url, station.ovol);
}

struct StationsJsonStream {
  File playlist;
  File index;
  uint16_t count = 0;
  uint16_t current = 0;
  uint32_t next = 1;
  uint8_t stage = 0;
  bool failed = false;
  char pending[BUFLEN * 12 + 128];
  size_t length = 0;
  size_t offset = 0;

  bool appendLiteral(const char *value) {
    const size_t size = strlen(value);
    if (size >= sizeof(pending) - length) return false;
    memcpy(pending + length, value, size);
    length += size;
    pending[length] = '\0';
    return true;
  }

  void reset() {
    next = 1;
    stage = 0;
    failed = false;
    length = offset = 0;
  }

  bool fail() {
    failed = true;
    return false;
  }

  bool nextPiece() {
    length = offset = 0;
    if (stage == 0) {
      const int written = snprintf(pending, sizeof(pending),
          "{\"current\":%u,\"count\":%u,\"stations\":[", current, count);
      if (written < 0 || static_cast<size_t>(written) >= sizeof(pending)) return fail();
      length = static_cast<size_t>(written);
      stage = 1;
      return true;
    }
    if (next <= count) {
      station_t station;
      if (!readIndexedStation(playlist, index, static_cast<uint16_t>(next), station)) return fail();
      const int written = snprintf(pending, sizeof(pending),
          "%s{\"number\":%u,\"name\":\"", next == 1 ? "" : ",", next);
      if (written < 0 || static_cast<size_t>(written) >= sizeof(pending)) return fail();
      length = static_cast<size_t>(written);
      if (!appendJsonEscaped(pending, sizeof(pending), length, station.name) ||
          !appendLiteral("\",\"url\":\"") ||
          !appendJsonEscaped(pending, sizeof(pending), length, station.url)) return fail();
      const int tail = snprintf(pending + length, sizeof(pending) - length,
          "\",\"ovol\":%d}", station.ovol);
      if (tail < 0 || static_cast<size_t>(tail) >= sizeof(pending) - length) return fail();
      length += static_cast<size_t>(tail);
      ++next;
      return true;
    }
    if (stage == 1) {
      memcpy(pending, "]}", 2);
      length = 2;
      stage = 2;
      return true;
    }
    return false;
  }

  size_t fill(uint8_t *buffer, size_t capacity) {
    size_t copied = 0;
    while (copied < capacity) {
      if (offset == length && !nextPiece()) break;
      const size_t available = length - offset;
      const size_t size = min(available, capacity - copied);
      memcpy(buffer + copied, pending + offset, size);
      offset += size;
      copied += size;
    }
    return copied;
  }
};

void handleStationsRead(AsyncWebServerRequest *request) {
  std::shared_ptr<StationsJsonStream> stream = std::make_shared<StationsJsonStream>();
  stream->playlist = SPIFFS.open(PLAYLIST_PATH, "r");
  stream->index = SPIFFS.open(INDEX_PATH, "r");
  bool valid = stream->playlist && stream->index;
  if (valid) {
    const size_t indexSize = stream->index.size();
    valid = indexSize % sizeof(uint32_t) == 0 &&
            indexSize / sizeof(uint32_t) <= UINT16_MAX;
    if (valid) {
      stream->count = indexSize / sizeof(uint32_t);
      station_t station;
      for (uint32_t number = 1; number <= stream->count; ++number) {
        if (!readIndexedStation(stream->playlist, stream->index, number, station)) {
          valid = false;
          break;
        }
      }
    }
  }
  if (!valid) {
    AsyncWebServerResponse *response = request->beginResponse(
        500, "application/json; charset=utf-8", "{\"error\":\"playlist_read_failed\"}");
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
    return;
  }
  const uint16_t selected = config.lastStation();
  stream->current = selected >= 1 && selected <= stream->count ? selected : 0;
  size_t responseLength = 0;
  while (stream->nextPiece()) {
    if (SIZE_MAX - responseLength < stream->length) {
      stream->failed = true;
      break;
    }
    responseLength += stream->length;
  }
  if (stream->failed || stream->stage != 2 || responseLength == 0) {
    AsyncWebServerResponse *response = request->beginResponse(
        500, "application/json; charset=utf-8", "{\"error\":\"playlist_read_failed\"}");
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
    return;
  }
  stream->reset();
  AsyncWebServerResponse *response = request->beginResponse(
      "application/json; charset=utf-8", responseLength,
      [stream](uint8_t *buffer, size_t capacity, size_t) {
        if (stream->failed) return static_cast<size_t>(RESPONSE_TRY_AGAIN);
        const size_t copied = stream->fill(buffer, capacity);
        return stream->failed ? static_cast<size_t>(RESPONSE_TRY_AGAIN) : copied;
      });
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}
void NetServer::processQueue(){
  if(nsQueue==NULL) return;
  nsRequestParams_t request;
  if(xQueueReceive(nsQueue, &request, NS_QUEUE_TICKS)){
    uint32_t clientId = request.clientId;
    wsBuf[0]='\0';
    switch (request.type) {
      case PLAYLIST:        getPlaylist(clientId); break;
      case PLAYLISTSAVED:   {
        #ifdef USE_SD
        if(config.getMode()==PM_SDCARD) {
        //  config.indexSDPlaylist();
          config.initSDPlaylist();
        }
        #endif
        if(config.getMode()==PM_WEB){
          config.indexPlaylist(); 
          config.initPlaylist(); 
        }
        getPlaylist(clientId); break;
      }
      case GETACTIVE: {
          bool dbgact = false, nxtn=false;
          //String act = F("\"group_wifi\",");
          nsBuf[0]='\0';
          APPEND_GROUP("group_wifi");
          if (network.status == CONNECTED) {
                                                                //act += F("\"group_system\",");
                                                                APPEND_GROUP("group_system");
            if (BRIGHTNESS_PIN != 255 || DSP_CAN_FLIPPED || DSP_MODEL == DSP_NOKIA5110 || dbgact)    APPEND_GROUP("group_display");
          #ifdef USE_NEXTION
                                                                APPEND_GROUP("group_nextion");
            nxtn=true;
          #endif
                                                              #if defined(LCD_I2C) || defined(DSP_OLED)
                                                                APPEND_GROUP("group_oled");
                                                              #endif
                                                              #if !defined(HIDE_VU) && !defined(DUMMYDISPLAY)
                                                                APPEND_GROUP("group_vu");
                                                              #endif
            if (BRIGHTNESS_PIN != 255 || nxtn || dbgact)        APPEND_GROUP("group_brightness");
            if (DSP_CAN_FLIPPED || dbgact)                      APPEND_GROUP("group_tft");
            if (TS_MODEL != TS_MODEL_UNDEFINED || dbgact)       APPEND_GROUP("group_touch");
            if (DSP_MODEL == DSP_NOKIA5110)                     APPEND_GROUP("group_nokia");
                                                                APPEND_GROUP("group_timezone");
            if (TS_MODEL != TS_MODEL_UNDEFINED || IR_PIN != 255 || dbgact)
                                                                APPEND_GROUP("group_controls");
            if (IR_PIN != 255 || dbgact)                        APPEND_GROUP("group_ir");
            if (!psramInit())                                   APPEND_GROUP("group_buffer");
                                                              #if RTCSUPPORTED
                                                                APPEND_GROUP("group_rtc");
                                                              #else
                                                                APPEND_GROUP("group_wortc");
                                                              #endif
          }
          size_t len = strlen(nsBuf);
          if (len > 0 && nsBuf[len - 1] == ',') nsBuf[len - 1] = '\0';
          
          snprintf(wsBuf, sizeof(wsBuf), "{\"act\":[%s]}", nsBuf);
          break;
        }
      case GETINDEX:      {
          requestOnChange(STATION, clientId); 
          requestOnChange(TITLE, clientId); 
          requestOnChange(VOLUME, clientId); 
          requestOnChange(EQUALIZER, clientId); 
          requestOnChange(BALANCE, clientId); 
          requestOnChange(BITRATE, clientId); 
          requestOnChange(MODE, clientId); 
          requestOnChange(SDINIT, clientId);
          requestOnChange(GETPLAYERMODE, clientId); 
          if (config.getMode()==PM_SDCARD) { requestOnChange(SDPOS, clientId); requestOnChange(SDLEN, clientId); requestOnChange(SDSNUFFLE, clientId); } 
          return; 
          break;
        }
      case GETSYSTEM:     sprintf (wsBuf, "{\"sst\":%d,\"vu\":%d,\"softr\":%d,\"vut\":%d,\"mdns\":\"%s\",\"ipaddr\":\"%s\", \"abuff\": %d }",
                                  config.store.smartstart != 2, 
                                  config.store.vumeter, 
                                  config.store.softapdelay,
                                  config.vuThreshold,
                                  config.store.mdnsname,
                                  config.ipToStr(WiFi.localIP()),
                                  config.store.abuff);
                                  break;
      case GETSCREEN:     sprintf (wsBuf, "{\"flip\":%d,\"nump\":%d,\"tsf\":%d,\"tsd\":%d,\"dspon\":%d,\"br\":%d,\"con\":%d,\"scre\":%d,\"scrt\":%d,\"scrb\":%d,\"scrpe\":%d,\"scrpt\":%d,\"scrpb\":%d}",
                                  config.store.flipscreen, 
                                  config.store.numplaylist, 
                                  config.store.fliptouch, 
                                  config.store.dbgtouch, 
                                  config.store.dspon, 
                                  config.store.brightness, 
                                  config.store.contrast,
                                  config.store.screensaverEnabled,
                                  config.store.screensaverTimeout,
                                  config.store.screensaverBlank,
                                  config.store.screensaverPlayingEnabled,
                                  config.store.screensaverPlayingTimeout,
                                  config.store.screensaverPlayingBlank);
                                  break;
      case GETTIMEZONE:   sprintf (wsBuf, "{\"sntp1\":\"%s\",\"sntp2\":\"%s\", \"timeint\":%d,\"timeintrtc\":%d}",
                                  config.store.sntp1, 
                                  config.store.sntp2,
                                  config.store.timeSyncInterval,
                                  config.store.timeSyncIntervalRTC); 
                                  break;
      case GETCONTROLS:   sprintf (wsBuf, "{\"irtl\":%d}", config.store.irtlp);

                                  break;
      case DSPON:         sprintf (wsBuf, "{\"dspontrue\":%d}", 1); break;
      case STATION:       requestOnChange(STATIONNAME, clientId); requestOnChange(ITEM, clientId); break;
      case STATIONNAME:   formatWsTextPayload(wsBuf, sizeof(wsBuf), "nameset", config.station.name); break;
      case ITEM:          sprintf (wsBuf, "{\"current\": %d}", config.lastStation()); break;
      case TITLE:         formatWsTextPayload(wsBuf, sizeof(wsBuf), "meta", config.station.title); serialCli.printf("##CLI.META#: %s\n> ", config.station.title); break;
      case VOLUME:        sprintf (wsBuf, "{\"payload\":[{\"id\":\"volume\", \"value\": %d}]}", config.store.volume); serialCli.printf("##CLI.VOL#: %d\n", config.store.volume); break;
      case NRSSI:         rssi = WiFi.RSSI(); sprintf (wsBuf, "{\"payload\":[{\"id\":\"rssi\", \"value\": %d}, {\"id\":\"heap\", \"value\": %d}]}", rssi, (player.isRunning() && config.store.audioinfo)?(int)(100*player.inBufferFilled()/playerBufMax):0); /*rssi = 255;*/ break;
      case SDPOS:         sprintf (wsBuf, "{\"sdpos\": %lu,\"sdend\": %lu,\"sdtpos\": %lu,\"sdtend\": %lu}", 
                                  player.getFilePos(), 
                                  player.getFileSize(), 
                                  player.getAudioCurrentTime(), 
                                  player.getAudioFileDuration()); 
                                  break;
      case SDLEN:         sprintf (wsBuf, "{\"sdmin\": %lu,\"sdmax\": %lu}", player.sd_min, player.sd_max); break;
      case SDSNUFFLE:     sprintf (wsBuf, "{\"snuffle\": %d}", config.store.sdsnuffle); break;
      case BITRATE:       sprintf (wsBuf, "{\"payload\":[{\"id\":\"bitrate\", \"value\": %d}, {\"id\":\"fmt\", \"value\": \"%s\"}]}", config.station.bitrate, getFormat(config.configFmt)); break;
      case MODE:          sprintf (wsBuf, "{\"payload\":[{\"id\":\"playerwrap\", \"value\": \"%s\"}]}", player.status() == PLAYING ? "playing" : "stopped"); serialCli.info(); break;
      case EQUALIZER:     sprintf (wsBuf, "{\"payload\":[{\"id\":\"bass\", \"value\": %d}, {\"id\": \"middle\", \"value\": %d}, {\"id\": \"trebble\", \"value\": %d}]}", config.store.bass, config.store.middle, config.store.trebble); break;
      case BALANCE:       sprintf (wsBuf, "{\"payload\":[{\"id\": \"balance\", \"value\": %d}]}", config.store.balance); break;
      case SDINIT:        sprintf (wsBuf, "{\"sdinit\": %d}", SDC_CS!=255); break;
      case GETPLAYERMODE: sprintf (wsBuf, "{\"playermode\": \"%s\"}", config.getMode()==PM_SDCARD?"modesd":"modeweb"); break;
      #ifdef USE_SD
        case CHANGEMODE:    config.changeMode(config.newConfigMode); return; break;
      #endif
      default:          break;
    }
    if (strlen(wsBuf) > 0) {
      if (clientId == 0) { websocket.textAll(wsBuf); }else{ websocket.text(clientId, wsBuf); }
  #ifdef MQTT_ROOT_TOPIC
      if (clientId == 0 && (request.type == STATION || request.type == ITEM || request.type == TITLE || request.type == MODE)) mqttPublishStatus();
      if (clientId == 0 && request.type == VOLUME) mqttPublishVolume();
  #endif
    }
  }
}

void NetServer::processVolumeUpdate(){
  bool pending;
  portENTER_CRITICAL(&netserverVolumeMux);
  pending = _volumeUpdatePending;
  portEXIT_CRITICAL(&netserverVolumeMux);
  if(!pending) return;

  bool hasWebClients = websocket.count() > 0;
  uint32_t now = millis();
  if((uint32_t)(now - _lastVolumeUpdate) < NS_VOLUME_INTERVAL_MS) return;
  if(hasWebClients && websocket.hasQueuedMessages()) return;

  portENTER_CRITICAL(&netserverVolumeMux);
  pending = _volumeUpdatePending;
  _volumeUpdatePending = false;
  portEXIT_CRITICAL(&netserverVolumeMux);
  if(!pending) return;

  _lastVolumeUpdate = now;
  sprintf(wsBuf, "{\"payload\":[{\"id\":\"volume\", \"value\": %d}]}", config.store.volume);
  if(hasWebClients) websocket.textAll(wsBuf);
  serialCli.printf("##CLI.VOL#: %d\n", config.store.volume);
#ifdef MQTT_ROOT_TOPIC
  mqttPublishVolume();
#endif
}

void NetServer::loop() {
  if(network.status==SDREADY) return;
  if (shouldReboot && (int32_t)(millis() - webUpdateRebootAt) >= 0) {
    Serial.println("Rebooting...");
    delay(100);
    ESP.restart();
  }
  processQueue();
  processVolumeUpdate();
  websocket.cleanupClients();
  switch (importRequest) {
    case IMPL:    importPlaylist();  importRequest = IMDONE; break;
    case IMWIFI:  config.saveWifi(); importRequest = IMDONE; break;
    default:      break;
  }
  //processQueue();
}

#if IR_PIN!=255
void NetServer::irToWs(const char* protocol, uint64_t irvalue) {
  wsBuf[0]='\0';
  sprintf (wsBuf, "{\"ircode\": %llu, \"protocol\": \"%s\"}", irvalue, protocol);
  websocket.textAll(wsBuf);
}
void NetServer::irValsToWs() {
  if (!irRecordEnable) return;
  wsBuf[0]='\0';
  sprintf (wsBuf, "{\"irvals\": [%llu, %llu, %llu]}", config.ircodes.irVals[config.irindex][0], config.ircodes.irVals[config.irindex][1], config.ircodes.irVals[config.irindex][2]);
  websocket.textAll(wsBuf);
}
#endif

void NetServer::onWsMessage(void *arg, uint8_t *data, size_t len, uint32_t clientId) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    if (config.parseWsCommand((const char*)data, _wscmd, _wsval, 65)) {
      if (strcmp(_wscmd, "ping") == 0) {
        websocket.text(clientId, "{\"pong\": 1}");
        return;
      }
      if (strcmp(_wscmd, "trebble") == 0) {
        int8_t valb = atoi(_wsval);
        config.setTone(config.store.bass, config.store.middle, valb);
        return;
      }
      if (strcmp(_wscmd, "middle") == 0) {
        int8_t valb = atoi(_wsval);
        config.setTone(config.store.bass, valb, config.store.trebble);
        return;
      }
      if (strcmp(_wscmd, "bass") == 0) {
        int8_t valb = atoi(_wsval);
        config.setTone(valb, config.store.middle, config.store.trebble);
        return;
      }
      if (strcmp(_wscmd, "submitplaylistdone") == 0) {
#ifdef MQTT_ROOT_TOPIC
        //mqttplaylistticker.attach(5, mqttplaylistSend);
        timekeeper.waitAndDo(5, mqttplaylistSend, DelayedActionSlot::MQTT);
#endif
        if (player.isRunning()) player.sendCommand({PR_PLAY, -config.lastStation()});
        return;
      }
      
      if(cmd.exec(_wscmd, _wsval, clientId)){
        return;
      }
    }
  }
}

void NetServer::getPlaylist(uint32_t clientId) {
  sprintf(nsBuf, "{\"file\": \"http://%s%s\"}", config.ipToStr(WiFi.localIP()), PLAYLIST_PATH);
  if (clientId == 0) { websocket.textAll(nsBuf); } else { websocket.text(clientId, nsBuf); }
}

int NetServer::_readPlaylistLine(File &file, char * line, size_t size){
  int bytesRead = file.readBytesUntil('\n', line, size);
  if(bytesRead>0){
    line[bytesRead] = 0;
    if(line[bytesRead-1]=='\r') line[bytesRead-1]=0;
  }
  return bytesRead;
}

bool NetServer::importPlaylist() {
  if(config.getMode()==PM_SDCARD) return false;
  //player.sendCommand({PR_STOP, 0});
  File tempfile = SPIFFS.open(TMP_PATH, "r");
  if (!tempfile) {
    return false;
  }
  char linePl[BUFLEN*3];
  int sOvol;
  _readPlaylistLine(tempfile, linePl, sizeof(linePl)-1);
  if (config.parseCSV(linePl, nsBuf, nsBuf2, sOvol)) {
    tempfile.close();
    SPIFFS.rename(TMP_PATH, PLAYLIST_PATH);
    requestOnChange(PLAYLISTSAVED, 0);
    return true;
  }
  if (config.parseJSON(linePl, nsBuf, nsBuf2, sOvol)) {
    File playlistfile = SPIFFS.open(PLAYLIST_PATH, "w");
    snprintf(linePl, sizeof(linePl)-1, "%s\t%s\t%d", nsBuf, nsBuf2, 0);
    playlistfile.println(linePl);
    while (tempfile.available()) {
      _readPlaylistLine(tempfile, linePl, sizeof(linePl)-1);
      if (config.parseJSON(linePl, nsBuf, nsBuf2, sOvol)) {
        snprintf(linePl, sizeof(linePl)-1, "%s\t%s\t%d", nsBuf, nsBuf2, 0);
        playlistfile.println(linePl);
      }
    }
    playlistfile.flush();
    playlistfile.close();
    tempfile.close();
    SPIFFS.remove(TMP_PATH);
    requestOnChange(PLAYLISTSAVED, 0);
    return true;
  }
  tempfile.close();
  SPIFFS.remove(TMP_PATH);
  return false;
}

void NetServer::requestOnChange(requestType_e request, uint32_t clientId) {
  if(nsQueue==NULL) return;
  if(request == VOLUME && clientId == 0){
    portENTER_CRITICAL(&netserverVolumeMux);
    _volumeUpdatePending = true;
    portEXIT_CRITICAL(&netserverVolumeMux);
    return;
  }
  nsRequestParams_t nsrequest;
  nsrequest.type = request;
  nsrequest.clientId = clientId;
  if(xQueueSend(nsQueue, &nsrequest, NSQ_SEND_DELAY) != pdPASS){
    serialCli.printf("##ERROR#:\tnetserver queue full for request %u\n", static_cast<unsigned>(request));
  }
}

void NetServer::resetQueue(){
  if(nsQueue!=NULL) xQueueReset(nsQueue);
}

void handleWebUpdateUpload(AsyncWebServerRequest *request, const String& filename,
                           size_t index, uint8_t *data, size_t len, bool final) {
#if defined(HTTP_USER) && defined(HTTP_PASS)
  if (network.status == CONNECTED && !request->authenticate(HTTP_USER, HTTP_PASS)) return;
#endif
  WebUpdateSession* session = static_cast<WebUpdateSession*>(request->_tempObject);
  if (index == 0) {
    if (session) {
      session->error = "Only one image per request is allowed";
      if (activeUpdateRequest == request) Update.abort();
      return;
    }
    session = static_cast<WebUpdateSession*>(calloc(1, sizeof(WebUpdateSession)));
    if (!session) return;
    request->_tempObject = session;
    session->error = nullptr;
    if (activeUpdateRequest && activeUpdateRequest != request) {
      session->error = "Another update is already running";
      return;
    }
    activeUpdateRequest = request;
    request->onDisconnect([request]() {
      if (activeUpdateRequest == request) {
        Update.abort();
        activeUpdateRequest = nullptr;
      }
    });
    if (!request->hasParam("updatetarget", true)) {
      session->error = "Missing update target";
      return;
    }
    const String target = request->getParam("updatetarget", true)->value();
    if (target == "firmware" || target == "fw") {
      session->target = U_FLASH;
    } else if (target == "spiffs") {
      session->target = U_SPIFFS;
    } else {
      session->error = "Unknown update target";
      return;
    }
    String lowerName = filename;
    lowerName.toLowerCase();
    if (!lowerName.endsWith(".bin")) {
      session->error = "Only .bin images are accepted";
      return;
    }
    if (lowerName.indexOf("-full.bin") >= 0) {
      session->error = "full.bin is for esptool recovery only";
      return;
    }
    if ((session->target == U_FLASH && lowerName.indexOf("spiffs") >= 0) ||
        (session->target == U_SPIFFS && lowerName.indexOf("firmware") >= 0)) {
      session->error = "Image filename does not match the selected target";
      return;
    }
    if (len < 8) {
      session->error = "Image header is too short";
      return;
    }
    if (session->target == U_FLASH) {
      if (data[0] != 0xE9 || data[1] == 0 || data[1] > 16) {
        session->error = "Invalid ESP32 firmware image header";
        return;
      }
    } else if (data[0] != 0 || data[1] != 0 || data[2] != 1 ||
               data[3] != 0 || data[4] != 1 || data[5] != 0) {
      session->error = "Invalid SPIFFS image header";
      return;
    }
    const esp_partition_t* partition = session->target == U_FLASH
        ? esp_ota_get_next_update_partition(nullptr)
        : esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                   ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
    if (!partition) {
      session->error = "Update partition not found";
      return;
    }
    session->limit = partition->size;
    if (request->hasParam("filesize", true)) {
      const String sizeText = request->getParam("filesize", true)->value();
      for (size_t i = 0; i < sizeText.length(); ++i) {
        if (!isDigit(sizeText[i])) {
          session->error = "Invalid image size";
          return;
        }
      }
      session->expected = static_cast<size_t>(sizeText.toInt());
    }
    if ((session->expected == 0 && request->contentLength() > session->limit) ||
        session->expected > session->limit) {
      session->error = "Image exceeds update partition";
      return;
    }
    if (session->target == U_SPIFFS && session->expected != session->limit) {
      session->error = "SPIFFS image must exactly match the partition size";
      return;
    }
    if (session->expected && session->expected > request->contentLength()) {
      session->error = "Declared image size exceeds upload size";
      return;
    }
    if (session->target == U_SPIFFS) {
      if (!backupWebUpdateData(session->error)) return;
      SPIFFS.end();
    }
    if (!Update.begin(session->expected ? session->expected : UPDATE_SIZE_UNKNOWN,
                      session->target)) {
      Serial.printf("Web Update begin failed: %s\n", Update.errorString());
      session->error = "Update.begin failed (see Serial)";
      if (session->target == U_SPIFFS) SPIFFS.begin(false);
      return;
    }
    session->started = true;
    Serial.printf("Web Update started: %s, %u bytes, limit %u\n",
                  session->target == U_FLASH ? "firmware" : "SPIFFS",
                  static_cast<unsigned>(session->expected),
                  static_cast<unsigned>(session->limit));
    player.sendCommand({PR_STOP, 0});
    display.putRequest(NEWMODE, UPDATING);
  }
  if (!session || session->error || !session->started) return;
  if (index > session->limit || len > session->limit - index ||
      (session->expected && (index > session->expected ||
                             len > session->expected - index))) {
    session->error = "Uploaded image exceeds declared size or partition";
    Update.abort();
    return;
  }
  if (len && Update.write(data, len) != len) {
    Serial.printf("Web Update write failed: %s\n", Update.errorString());
    session->error = "Update.write failed (see Serial)";
    Update.abort();
    return;
  }
  if (final) {
    if (session->expected && index + len != session->expected) {
      session->error = "Incomplete upload: image size differs";
      Update.abort();
    } else if (!Update.end(session->expected == 0)) {
      Serial.printf("Web Update end failed: %s\n", Update.errorString());
      session->error = "Update.end failed (see Serial)";
    } else {
      session->finished = true;
      Serial.printf("Web Update success: %u bytes\n", static_cast<unsigned>(index + len));
    }
    activeUpdateRequest = nullptr;
  }
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  static int freeSpace = 0;
  if(request->url()=="/upload"){
    if (!index) {
      if(filename!="tempwifi.csv"){
        //player.sendCommand({PR_STOP, 0});
        if(SPIFFS.exists(PLAYLIST_PATH)) SPIFFS.remove(PLAYLIST_PATH);
        if(SPIFFS.exists(INDEX_PATH)) SPIFFS.remove(INDEX_PATH);
        if(SPIFFS.exists(PLAYLIST_SD_PATH)) SPIFFS.remove(PLAYLIST_SD_PATH);
        if(SPIFFS.exists(INDEX_SD_PATH)) SPIFFS.remove(INDEX_SD_PATH);
      }
      freeSpace = (float)SPIFFS.totalBytes()/100*68-SPIFFS.usedBytes();
      request->_tempFile = SPIFFS.open(TMP_PATH , "w");
    }else{
      
    }
    if (len) {
      if(freeSpace>index+len){
        request->_tempFile.write(data, len);
      }
    }
    if (final) {
      request->_tempFile.close();
      freeSpace = 0;
    }
  }else if(request->url()=="/update"){
    handleWebUpdateUpload(request, filename, index, data, len, final);
  }else{ // "/webboard"
    DBGVB("File: %s, size:%u bytes, index: %u, final: %s\n", filename.c_str(), len, index, final?"true":"false");
    if (!index) {
      player.sendCommand({PR_STOP, 0});
      String spath = "/www/";
      if(filename=="playlist.csv" || filename=="wifi.csv") spath = "/data/";
      request->_tempFile = SPIFFS.open(spath + filename , "w");
    }
    if (len) {
      request->_tempFile.write(data, len);
    }
    if (final) {
      request->_tempFile.close();
      if(filename=="playlist.csv") config.indexPlaylist();
    }
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT: /*netserver.requestOnChange(STARTUP, client->id()); */if (config.store.audioinfo) Serial.printf("[WEBSOCKET] client #%lu connected from %s\n", client->id(), config.ipToStr(client->remoteIP())); break;
    case WS_EVT_DISCONNECT: if (config.store.audioinfo) Serial.printf("[WEBSOCKET] client #%lu disconnected\n", client->id()); break;
    case WS_EVT_DATA: netserver.onWsMessage(arg, data, len, client->id()); break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}
void handleNotFound(AsyncWebServerRequest * request) {
#if defined(HTTP_USER) && defined(HTTP_PASS)
  if(network.status == CONNECTED)
    if (request->url() == "/logout") {
      request->send(401);
      return;
    }
    if (!request->authenticate(HTTP_USER, HTTP_PASS)) {
      return request->requestAuthentication();
    }
#endif
  if(request->url()=="/emergency") { request->send_P(200, "text/html", emergency_form); return; }
  if (request->method() == HTTP_GET && request->url() == "/legacy.html") {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_html);
    response->addHeader("Cache-Control", "max-age=31536000");
    request->send(response);
    return;
  }
  if(request->method() == HTTP_POST && request->url()=="/webboard" && config.emptyFS) { request->redirect("/"); ESP.restart(); return; }
  if (request->method() == HTTP_GET) {
    DBGVB("[%s] client ip=%s request of %s", __func__, config.ipToStr(request->client()->remoteIP()), request->url().c_str());
    if (strcmp(request->url().c_str(), PLAYLIST_PATH) == 0 || 
        strcmp(request->url().c_str(), SSIDS_PATH) == 0 || 
        strcmp(request->url().c_str(), INDEX_PATH) == 0 || 
        strcmp(request->url().c_str(), TMP_PATH) == 0 || 
        strcmp(request->url().c_str(), PLAYLIST_SD_PATH) == 0 || 
        strcmp(request->url().c_str(), INDEX_SD_PATH) == 0) {
#ifdef MQTT_ROOT_TOPIC
      if (strcmp(request->url().c_str(), PLAYLIST_PATH) == 0) while (mqttplaylistblock) vTaskDelay(5);
#endif
      if(strcmp(request->url().c_str(), PLAYLIST_PATH) == 0 && config.getMode()==PM_SDCARD){
        netserver.chunkedHtmlPage("application/octet-stream", request, PLAYLIST_SD_PATH);
      }else{
        netserver.chunkedHtmlPage("application/octet-stream", request, request->url().c_str());
      }
      return;
    }// if (strcmp(request->url().c_str(), PLAYLIST_PATH) == 0 || 
  }// if (request->method() == HTTP_GET)
  
  if (request->method() == HTTP_POST) {
    if(request->url()=="/webboard"){ request->redirect("/"); return; } // <--post files from /data/www
    if(request->url()=="/upload"){ // <--upload playlist.csv or wifi.csv
      if (request->hasParam("plfile", true, true)) {
        netserver.importRequest = IMPL;
        request->send(200);
      } else if (request->hasParam("wifile", true, true)) {
        netserver.importRequest = IMWIFI;
        request->send(200);
      } else {
        request->send(404);
      }
      return;
    }
    if(request->url()=="/update"){
#if defined(HTTP_USER) && defined(HTTP_PASS)
      if (network.status == CONNECTED && !request->authenticate(HTTP_USER, HTTP_PASS)) {
        activeUpdateRequest = nullptr;
        request->requestAuthentication();
        return;
      }
#endif
      WebUpdateSession* session = static_cast<WebUpdateSession*>(request->_tempObject);
      const bool success = session && session->finished && !session->error;
      shouldReboot = success;
      if (success) webUpdateRebootAt = millis() + 1500;
      const char* message = success ? "OK" :
          (session && session->error ? session->error : "No valid update image received");
      AsyncWebServerResponse *response = request->beginResponse(
          success ? 200 : 400, "text/plain", message);
      response->addHeader("Connection", "close");
      response->addHeader("Cache-Control", "no-store");
      request->send(response);
      if (!success && activeUpdateRequest == request) {
        Update.abort();
        activeUpdateRequest = nullptr;
      }
      return;
    }
  }// if (request->method() == HTTP_POST)
  
  if (request->url() == "/favicon.ico") {
    request->send(200, "image/x-icon", "data:,");
    return;
  }
  if (request->url() == "/variables.js") {
    sprintf (netserver.nsBuf, "var voxOneVersion='%s';\nvar yoRadioVersion='%s';\nvar yoVersion=voxOneVersion;\nvar voxOneProfile='%s';\nvar formAction='%s';\nvar playMode='%s';\n", VOXONE_VERSION, YOVERSION, VOXONE_PROFILE_NAME, (network.status == CONNECTED && !config.emptyFS)?"webboard":"", (network.status == CONNECTED)?"player":"ap");
    request->send(200, "text/html", netserver.nsBuf);
    return;
  }
  if (strcmp(request->url().c_str(), "/settings.html") == 0 || strcmp(request->url().c_str(), "/update.html") == 0 || strcmp(request->url().c_str(), "/ir.html") == 0){
    //request->send_P(200, "text/html", index_html);
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_html);
    response->addHeader("Cache-Control","max-age=31536000");
    request->send(response);
    return;
  }
  if (request->method() == HTTP_GET && request->url() == "/webboard") {
    request->send_P(200, "text/html", emptyfs_html);
    return;
  }
  Serial.print("Not Found: ");
  Serial.println(request->url());
  request->send(404, "text/plain", "Not found");
}

void handleIndex(AsyncWebServerRequest * request) {
  if(config.emptyFS){
    if(request->url()=="/" && request->method() == HTTP_GET ) { request->send_P(200, "text/html", emptyfs_html); return; }
    if(request->url()=="/" && request->method() == HTTP_POST) {
      if(request->arg("ssid")!="" && request->arg("pass")!=""){
        netserver.nsBuf[0]='\0';
        snprintf(netserver.nsBuf, sizeof(netserver.nsBuf), "%s\t%s", request->arg("ssid").c_str(), request->arg("pass").c_str());
        request->redirect("/");
        config.saveWifiFromNextion(netserver.nsBuf);
        return;
      }
      request->redirect("/"); 
      ESP.restart();
      return;
    }
    Serial.print("Not Found: ");
    Serial.println(request->url());
    request->send(404, "text/plain", "Not found");
    return;
  } // end if(config.emptyFS)
#if defined(HTTP_USER) && defined(HTTP_PASS)
  if(network.status == CONNECTED)
    if (!request->authenticate(HTTP_USER, HTTP_PASS)) {
      return request->requestAuthentication();
    }
#endif
  if (strcmp(request->url().c_str(), "/") == 0 && request->params() == 0) {
    if(network.status == CONNECTED) {
      request->redirect("/voxone.html");
    } else request->redirect("/settings.html");
    return;
  }
  if(network.status == CONNECTED){
    int paramsNr = request->params();
    if(paramsNr==1){
      AsyncWebParameter* p = request->getParam(0);
      if(cmd.exec(p->name().c_str(),p->value().c_str())) {
        if(p->name()=="reset" || p->name()=="clearspiffs") request->redirect("/");
        if(p->name()=="clearspiffs") { delay(100); ESP.restart(); }
        request->send(200, "text/plain", "");
        return;
      }
    }
    if (request->hasArg("trebble") && request->hasArg("middle") && request->hasArg("bass")) {
      config.setTone(request->getParam("bass")->value().toInt(), request->getParam("middle")->value().toInt(), request->getParam("trebble")->value().toInt());
      request->send(200, "text/plain", "");
      return;
    }
    if (request->hasArg("sleep")) {
      int sford = request->getParam("sleep")->value().toInt();
      int safterd = request->hasArg("after")?request->getParam("after")->value().toInt():0;
      if(sford > 0 && safterd >= 0){ request->send(200, "text/plain", ""); config.sleepForAfter(sford, safterd); return; }
    }
    request->send(404, "text/plain", "Not found");
    
  }else{
    request->send(404, "text/plain", "Not found");
  }
}
