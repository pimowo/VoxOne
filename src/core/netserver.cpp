#include "options.h"
#include "system_operation_state.h"
#include "source_manager.h"
#include "bt_link.h"
#include "web_status_view.h"
#include "bt_audio_input.h"
#include "Arduino.h"
#include <SPIFFS.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_mac.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <nvs.h>
#include <memory>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include "config.h"
#if defined(VOXONE_PROFILE_SALON)
#include "dsp_transport_runtime.h"
#endif
#include "ap_wifi_recovery.h"
#include "playlist_store.h"
#include "playlist_mapping.h"
#include "station_metadata.h"
#include "station_directory.h"
#include "station_directory_format.h"
#include "station_directory_routes.h"
#include "netserver.h"
#include "player.h"
#include "serialcli.h"
#include "display.h"
#include "network.h"
#include "mqtt.h"
#include "mqtt_config.h"
#include "controls.h"
#include "commandhandler.h"
#include "volume_map.h"
#include "timekeeper.h"
#include "ui_timeout_config.h"
#include "rtcsupport.h"
#include "../displays/dspcore.h"
#include "../displays/widgets/widgetsconfig.h" //BitrateFormat

#if DSP_MODEL==DSP_DUMMY
#define DUMMYDISPLAY
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
static portMUX_TYPE netserverLoopMux = portMUX_INITIALIZER_UNLOCKED;
static bool netserverLoopActive = false;

static bool parseToneValue(const char* value, int8_t& bass, int8_t& middle, int8_t& trebble) {
  int8_t* fields[] = {&bass, &middle, &trebble};
  const char* cursor = value;
  for (int index = 0; index < 3; ++index) {
    const char* digits = cursor;
    if (*digits == '+' || *digits == '-') ++digits;
    if (*digits < '0' || *digits > '9') return false;
    errno = 0;
    char* end = nullptr;
    const long parsed = strtol(cursor, &end, 10);
    if (errno == ERANGE || parsed < INT8_MIN || parsed > INT8_MAX) return false;
    if (index < 2 ? *end != ',' : *end != '\0') return false;
    *fields[index] = static_cast<int8_t>(parsed);
    cursor = end + 1;
  }
  return true;
}

AsyncWebServer webserver(80);
AsyncWebSocket websocket("/ws");

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleStationsRead(AsyncWebServerRequest *request);
void handleStationsReadSnapshot(AsyncWebServerRequest *request);
void handleStationDirectorySearch(AsyncWebServerRequest *request);
void handleStationsMutation(AsyncWebServerRequest *request);
void handleStationMetadata(AsyncWebServerRequest *request);
void handleStationsExport(AsyncWebServerRequest *request);
void handleStationsImport(AsyncWebServerRequest *request);
void handleStationsImportUpload(AsyncWebServerRequest *request, String filename,
                                size_t index, uint8_t *data, size_t len, bool final);
void handleMqttConfigRead(AsyncWebServerRequest *request);
void handleMqttConfigSave(AsyncWebServerRequest *request);
void handleTimeStatus(AsyncWebServerRequest *request);
void handleTimeSync(AsyncWebServerRequest *request);
void handleIndex(AsyncWebServerRequest * request);
void handleNotFound(AsyncWebServerRequest * request);
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

uint32_t webUpdateRebootAt = 0;
//Ticker mqttplaylistticker;
namespace {
SystemOperationState systemOperationState;

void finishFailedUpdateAudio() {
  if (!systemOperationState.audioBlocked() || systemOperationState.restartPending() ||
      !systemOperationState.blocksRequests()) return;
  systemOperationState.awaitRadioStop();
  player.resetQueue();
  player.sendCommand({PR_STOP, 0});
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  sourceManagerStopForUpdate();
#endif
  display.putRequest(NEWMODE, PLAYER);
  systemOperationState.updateFailed();
}

void scheduleSystemRestart(const char* source) {
  if (systemOperationState.restartPending()) return;
  webUpdateRebootAt = millis() + 1500;
  systemOperationState.restartRequested();
  Serial.printf("##[%s]# restart pending\n", source);
}

void remountFilesystemAfterFailedUpdate() {
  if (!systemOperationState.filesystemUnavailable()) return;
  if (SPIFFS.begin(false)) {
    Serial.println("##[UPDATE]# SPIFFS remounted after failed update");
  } else {
    Serial.println("##[ERROR]# SPIFFS remount failed after update error");
  }
}

class WebUpdateRequestGuard : public AsyncWebHandler {
public:
  bool canHandle(AsyncWebServerRequest* request) override {
    return systemOperationState.blocksRequests() &&
           !(request->method() == HTTP_POST && request->url() == "/update");
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    if (request->method() == HTTP_GET && request->url() == "/emergency") {
#if defined(HTTP_USER) && defined(HTTP_PASS)
      if (network.status == CONNECTED && !request->authenticate(HTTP_USER, HTTP_PASS)) {
        request->requestAuthentication();
        return;
      }
#endif
      request->send_P(200, "text/html", emergency_form);
      return;
    }
    request->send(503, "text/plain", "Update in progress");
  }
};

struct WebUpdateFile {
  const char* path;
  const char* key;
};
constexpr WebUpdateFile kWebUpdateFiles[] = {
  {SSIDS_PATH, "wifi"},
  {PLAYLIST_PATH, "stations"},
  {INDEX_PATH, "stidx"},
  {"/data/playlist.csv", "playlist"} // Restore backups made by pre-v1 firmware.
};
constexpr char kWebUpdateNamespace[] = "voxupdate";
AsyncWebServerRequest* activeUpdateRequest = nullptr;

struct WebUpdateSession {
  size_t expected;
  size_t limit;
  int target;
  bool started;
  bool finished;
  bool audioBlocked;
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
    if (size == 0 && strcmp(item.path, "/data/playlist.csv") == 0) {
      file.close();
      continue;
    }
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

bool systemRestartPending() { return systemOperationState.restartPending(); }
bool systemUpdateAudioBlocked() { return systemOperationState.audioBlocked(); }
void systemUpdateRadioStopped() { systemOperationState.radioStopped(); }

void requestSystemRestart() { scheduleSystemRestart("SYSTEM"); }

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
  Serial.printf("##[BOOT]# NetServer::begin called: status=%d started=%s caller=%s\n",
                static_cast<int>(network.status), _started ? "yes" : "no",
                quiet ? "searchWiFi" : "setup");
  if (!netServerShouldInitialize(_started)) return true;
  if(!quiet) Serial.print("##[BOOT]#\tnetserver.begin\t");
  playerBufMax = psramInit()?300000:1600 * config.store.abuff;
  _volumeUpdatePending = false;
  _lastVolumeUpdate = millis() - NS_VOLUME_INTERVAL_MS;
  nsQueue = xQueueCreate( 20, sizeof( nsRequestParams_t ) );
  while(nsQueue==NULL){;}
#if defined(VOXONE_PROFILE_SALON)
  if (!voxone::dsp::beginDspTransport())
    Serial.println("[DSP] transport queue unavailable");
#endif

  // Must precede static handlers: their canHandle() opens files in SPIFFS.
  webserver.addHandler(new WebUpdateRequestGuard());
  webserver.on("/", HTTP_ANY, handleIndex);
  webserver.on("/api/stations/export", HTTP_GET, handleStationsExport);
  webserver.on("/api/stations/import", HTTP_POST, handleStationsImport,
               handleStationsImportUpload);
  webserver.on(stationDirectory::kStationsSnapshotRoute, HTTP_GET, handleStationsReadSnapshot);
  webserver.on(stationDirectory::kDirectorySearchRoute, HTTP_GET, handleStationDirectorySearch);
  webserver.on("/api/stations/add", HTTP_POST, handleStationsMutation);
  webserver.on("/api/stations/edit", HTTP_POST, handleStationsMutation);
  webserver.on("/api/stations/delete", HTTP_POST, handleStationsMutation);
  webserver.on("/api/stations/reorder", HTTP_POST, handleStationsMutation);
  webserver.on("/api/stations/metadata", HTTP_POST, handleStationMetadata);
  webserver.on("/api/mqtt", HTTP_GET, handleMqttConfigRead);
  webserver.on("/api/mqtt", HTTP_POST, handleMqttConfigSave);
  webserver.on("/api/time", HTTP_GET, handleTimeStatus);
  webserver.on("/api/time/sync", HTTP_POST, handleTimeSync);
  // Browsers may request the legacy icon path even when HTML declares the SVG icon.
  webserver.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->redirect("/voxone-logo.svg");
  });
#if defined(VOXONE_PROFILE_SALON)
  webserver.on("/api/dsp/state", HTTP_GET, voxone::dsp::handleDspState);
#endif
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
  _started = true;
  if(!quiet) Serial.println("done");
  return true;
}

size_t NetServer::chunkedHtmlPageCallback(uint8_t* buffer, size_t maxLen, size_t index){
  if (systemOperationState.blocksRequests()) {
    display.unlock();
    return 0;
  }
  File requiredfile = SPIFFS.open(netserver.chunkedPathBuffer, "r");
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

static size_t jsonEscapedSize(const char* value) {
  size_t size = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p)
    size += *p < 0x20 ? 6 : (*p == '"' || *p == '\\' ? 2 : 1);
  return size;
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

static bool appendWebStatusLiteral(char* output, size_t capacity,
                                   size_t& used, const char* literal) {
  const size_t length = strlen(literal);
  if (used + length >= capacity) return false;
  memcpy(output + used, literal, length);
  used += length;
  output[used] = '\0';
  return true;
}

static bool appendWebStatusText(char* output, size_t capacity, size_t& used,
                                const char* key, const char* value) {
  if (!appendWebStatusLiteral(output, capacity, used, key)) return false;
  // A bounded field keeps unusual control-heavy metadata from exhausting wsBuf.
  const size_t fieldEnd = std::min(capacity, used + 240);
  if (!appendJsonEscaped(output, fieldEnd, used, value, true)) return false;
  return appendWebStatusLiteral(output, capacity, used, "\"");
}

static bool formatNetworkInfo(char* output, size_t capacity, size_t& used) {
  const String activeSsid = network.status == CONNECTED ? WiFi.SSID() : String();
  if (!appendWebStatusLiteral(output, capacity, used, "{\"hostname\":\"") ||
      !appendJsonEscaped(output, capacity, used, config.store.mdnsname, true) ||
      !appendWebStatusLiteral(output, capacity, used, "\",\"activeSsid\":\"") ||
      !appendJsonEscaped(output, capacity, used, activeSsid.c_str(), true) ||
      !appendWebStatusLiteral(output, capacity, used, "\",\"profiles\":[")) return false;

  const uint8_t profileCount = std::min<uint8_t>(config.ssidsCount, 5);
  for (uint8_t index = 0; index < profileCount; ++index) {
    if (index && !appendWebStatusLiteral(output, capacity, used, ",")) return false;
    if (!appendWebStatusLiteral(output, capacity, used, "{\"ssid\":\"") ||
        !appendJsonEscaped(output, capacity, used, config.ssids[index].ssid, true)) return false;
    char profileFields[80];
    const int length = snprintf(profileFields, sizeof(profileFields),
        "\",\"passwordSet\":%s,\"order\":%u}",
        config.ssids[index].password[0] ? "true" : "false",
        static_cast<unsigned>(index + 1));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(profileFields) ||
        !appendWebStatusLiteral(output, capacity, used, profileFields)) return false;
  }
  return appendWebStatusLiteral(output, capacity, used, "]}");
}

static void formatWebStatus(char* output, size_t capacity) {
  char radioMetadata[BUFLEN + 1]{};
  stationMetaDisplay(config.station.title,
                     config.station.metadataMode == STATION_META_SWAP,
                     radioMetadata, sizeof(radioMetadata));
  DisplaySourceView sourceView{};
  sourceView.kind = DisplaySourceKind::Radio;
  sourceView.playback = player.isRunning() ? DisplayPlaybackState::Playing
                                            : DisplayPlaybackState::Stopped;
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  SourceWebSnapshot bt{};
  sourceManagerWebSnapshot(bt);
  sourceView.kind = bt.kind;
  sourceView.connected = bt.connected;
  sourceView.playback = bt.playback;
  sourceView.sampleRate = bt.sampleRate;
  sourceView.peerName = bt.peerName;
  sourceView.artist = bt.artist;
  sourceView.title = bt.title;
#endif
  bool btOnline = false;
  unsigned btProtocol = 0;
  const char* btFirmware = "";
  const char* btName = "";
  const char* btCapabilities = "";
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  const BtLinkState& link = btLink.state();
  btOnline = link.runtimeAvailable;
  if (btOnline) {
    btProtocol = link.protocolVersion;
    btFirmware = link.firmwareVersion;
    btName = link.btName;
    btCapabilities = link.capabilities;
  }
#endif
  const WebStatusView status = selectWebStatusView(
      sourceView, config.station.name, radioMetadata,
      getFormat(config.configFmt), config.station.bitrate);
  size_t used = 0;
  output[0] = '\0';
  if (!appendWebStatusLiteral(output, capacity, used, "{\"webStatus\":{") ||
      !appendWebStatusText(output, capacity, used, "\"source\":\"", status.source) ||
      !appendWebStatusText(output, capacity, used, ",\"name\":\"", status.name) ||
      !appendWebStatusText(output, capacity, used, ",\"metadata\":\"", status.metadata) ||
      !appendWebStatusText(output, capacity, used, ",\"artist\":\"", status.artist) ||
      !appendWebStatusText(output, capacity, used, ",\"title\":\"", status.title) ||
      !appendWebStatusText(output, capacity, used, ",\"codec\":\"", status.codec) ||
      !appendWebStatusText(output, capacity, used, ",\"playback\":\"", status.playback) ||
      !appendWebStatusLiteral(output, capacity, used, ",\"btModule\":{\"firmware\":\"") ||
      !appendWebStatusText(output, capacity, used, "", btFirmware) ||
      !appendWebStatusText(output, capacity, used, ",\"name\":\"", btName) ||
      !appendWebStatusText(output, capacity, used, ",\"capabilities\":\"", btCapabilities)) {
    output[0] = '\0';
    return;
  }
  const int tail = snprintf(output + used, capacity - used,
      ",\"online\":%s,\"protocol\":%u},\"bitrate\":%u,\"sampleRate\":%lu,\"btConnected\":%s,\"availableSources\":[\"radio\"%s]}}",
      btOnline ? "true" : "false", btProtocol,
      status.bitrate, static_cast<unsigned long>(status.sampleRate),
      status.btConnected ? "true" : "false",
      voxone::activeProfile.capabilities.hasBt ? ",\"bt\"" : "");
  if (tail < 0 || static_cast<size_t>(tail) >= capacity - used) output[0] = '\0';
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
  char* name = nullptr;
  char* url = nullptr;
  if (!stationParseFields(line, station.id, name, url, station.ovol,
                          station.metadataMode)) return false;
  strlcpy(station.name, name, sizeof(station.name));
  strlcpy(station.url, url, sizeof(station.url));
  return true;
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
      char id[17];
      stationFormatId(station.id, id);
      const int written = snprintf(pending, sizeof(pending),
          "%s{\"number\":%u,\"id\":\"%s\",\"name\":\"",
          next == 1 ? "" : ",", next, id);
      if (written < 0 || static_cast<size_t>(written) >= sizeof(pending)) return fail();
      length = static_cast<size_t>(written);
      if (!appendJsonEscaped(pending, sizeof(pending), length, station.name) ||
          !appendLiteral("\",\"url\":\"") ||
          !appendJsonEscaped(pending, sizeof(pending), length, station.url)) return fail();
      const int tail = snprintf(pending + length, sizeof(pending) - length,
          "\",\"ovol\":%d,\"metadataMode\":\"%s\",\"swapArtistTitle\":%s}",
          station.ovol, station.metadataMode == STATION_META_SWAP ? "swap" : "normal",
          station.metadataMode == STATION_META_SWAP ? "true" : "false");
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
static void stationsError(AsyncWebServerRequest* request, int code, const char* error);

void handleStationsReadSnapshot(AsyncWebServerRequest *request) {
  PlaylistGuard guard;
  std::vector<PlaylistRow> rows;
  String revision;
  if (!guard || !playlistStore.snapshot(rows, revision)) {
    Serial.printf("##[ERROR]# stations GET failed: %s\n",
                  guard ? "snapshot" : "lock");
    AsyncWebServerResponse* error = request->beginResponse(
        500, "application/json; charset=utf-8", "{\"error\":\"playlist_read_failed\"}");
    error->addHeader("Cache-Control", "no-store");
    request->send(error);
    return;
  }
  const uint16_t selected = config.lastStation();
  const uint16_t current = selected <= rows.size() ? selected : 0;
  size_t capacity = 80 + rows.size() * 128;
  for (const PlaylistRow& row : rows)
    capacity += jsonEscapedSize(row.name.c_str()) + jsonEscapedSize(row.url.c_str());
  String body;
  if (capacity > ESP.getFreeHeap() / 2 || !body.reserve(capacity)) {
    Serial.printf("##[ERROR]# stations GET response allocation failed: need=%u heap=%u\n",
                  static_cast<unsigned>(capacity), static_cast<unsigned>(ESP.getFreeHeap()));
    AsyncWebServerResponse* error = request->beginResponse(
        500, "application/json; charset=utf-8", "{\"error\":\"playlist_read_failed\"}");
    error->addHeader("Cache-Control", "no-store");
    request->send(error);
    return;
  }
  body = "{\"revision\":\"" + revision + "\",\"current\":" + String(current) +
         ",\"count\":" + String(rows.size()) + ",\"stations\":[";
  char encoded[BUFLEN * 6 + 1];
  for (size_t i = 0; i < rows.size(); ++i) {
    char id[17];
    stationFormatId(rows[i].id, id);
    body += (i ? "," : "") + String("{\"number\":") + String(i + 1) +
            ",\"id\":\"" + id + "\",\"name\":\"";
    size_t used = 0;
    if (!appendJsonEscaped(encoded, sizeof(encoded), used, rows[i].name.c_str())) {
      stationsError(request, 500, "playlist_response_failed");
      return;
    }
    body += encoded;
    body += "\",\"url\":\"";
    used = 0;
    if (!appendJsonEscaped(encoded, sizeof(encoded), used, rows[i].url.c_str())) {
      stationsError(request, 500, "playlist_response_failed");
      return;
    }
    body += encoded;
    body += "\",\"ovol\":" + String(rows[i].ovol) +
            ",\"metadataMode\":\"" +
            (rows[i].metadataMode == STATION_META_SWAP ? "swap" : "normal") +
            "\",\"swapArtistTitle\":" +
            (rows[i].metadataMode == STATION_META_SWAP ? "true" : "false") + "}";
  }
  body += "]}";
  AsyncWebServerResponse* response = request->beginResponse(
      200, "application/json; charset=utf-8", body);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

static void stationsReply(AsyncWebServerRequest* request, int code, const String& body) {
  AsyncWebServerResponse* response = request->beginResponse(
      code, "application/json; charset=utf-8", body);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void handleStationDirectorySearch(AsyncWebServerRequest* request) {
  const stationDirectory::SearchRequest kind = stationDirectory::classifySearchRequest(
      request->hasParam("job"), request->hasParam("q"));
  if (kind == stationDirectory::SearchRequest::POLL) {
    const String jobText = request->getParam("job")->value();
    char* end = nullptr;
    const unsigned long parsed = strtoul(jobText.c_str(), &end, 10);
    if (!jobText.length() || !end || *end || !parsed || parsed > UINT32_MAX) {
      stationsReply(request, 400, R"({"error":"bad_job"})");
      return;
    }
    int status = 0;
    String body;
    bool ready = false;
    if (!stationDirectoryPoll(static_cast<uint32_t>(parsed), status, body, ready)) {
      stationsReply(request, 404, R"({"error":"job_not_found"})");
      return;
    }
    stationsReply(request, status, body);
    return;
  }
  if (kind == stationDirectory::SearchRequest::MISSING_QUERY) {
    stationsReply(request, 400, R"({"error":"missing_query"})");
    return;
  }
  const String query = request->getParam("q")->value();
  const String country = request->hasParam("country") ?
      request->getParam("country")->value() : String("PL");
  unsigned limit = stationDirectory::kDefaultLimit;
  if (request->hasParam("limit")) {
    const String limitText = request->getParam("limit")->value();
    char* end = nullptr;
    const unsigned long parsed = strtoul(limitText.c_str(), &end, 10);
    if (!limitText.length() || !end || *end || parsed > UINT_MAX) {
      stationsReply(request, 400, R"({"error":"bad_limit"})");
      return;
    }
    limit = static_cast<unsigned>(parsed);
  }
  if (!stationDirectory::validQuery(std::string(query.c_str())) ||
      !stationDirectory::validCountry(std::string(country.c_str())) ||
      !stationDirectory::validLimit(limit)) {
    stationsReply(request, 400, R"({"error":"bad_search_parameter"})");
    return;
  }
  uint32_t job = 0;
  const DirectoryStartResult result = stationDirectoryStart(query, country, limit, job);
  if (result == DirectoryStartResult::BUSY) {
    stationsReply(request, 429, R"({"error":"directory_busy"})");
  } else if (result == DirectoryStartResult::OFFLINE) {
    stationsReply(request, 503, R"({"error":"offline"})");
  } else if (result == DirectoryStartResult::NO_MEMORY) {
    stationsReply(request, 503, R"({"error":"insufficient_memory"})");
  } else {
    Serial.printf("##[DIRECTORY]# search q=%s country=%s limit=%u\n",
                  query.c_str(), country.c_str(), limit);
    stationsReply(request, 202,
                  String(R"({"status":"pending","job":)") + job + "}");
  }
}
static bool deviceApiAuthorized(AsyncWebServerRequest* request) {
#if defined(HTTP_USER) && defined(HTTP_PASS)
  if (network.status == CONNECTED && !request->authenticate(HTTP_USER, HTTP_PASS)) {
    request->requestAuthentication();
    return false;
  }
#endif
  return true;
}

static void mqttApiError(AsyncWebServerRequest* request, int code, const char* error) {
  stationsReply(request, code, String("{\"error\":\"") + error + "\"}");
}

void handleTimeStatus(AsyncWebServerRequest* request) {
  if (!deviceApiAuthorized(request)) return;
  char systemTime[20] = "";
  char rtcTime[20] = "";
  char systemJson[24] = "null";
  char rtcJson[24] = "null";
  const time_t now = time(nullptr);
  tm localTime = {};
  if (localtime_r(&now, &localTime) && localTime.tm_year >= 120)
    strftime(systemTime, sizeof(systemTime), "%Y-%m-%d %H:%M:%S", &localTime);
  if (systemTime[0]) snprintf(systemJson, sizeof(systemJson), "\"%s\"", systemTime);
#if RTCSUPPORTED
  if (config.isRTCFound() && rtc.isRunning()) {
    tm rtcLocalTime = {};
    rtc.getTime(&rtcLocalTime);
    strftime(rtcTime, sizeof(rtcTime), "%Y-%m-%d %H:%M:%S", &rtcLocalTime);
    if (rtcTime[0]) snprintf(rtcJson, sizeof(rtcJson), "\"%s\"", rtcTime);
  }
#endif
  char body[256];
  const int length = snprintf(body, sizeof(body),
      "{\"rtcSupported\":%s,\"rtcFound\":%s,\"systemTime\":%s,\"rtcTime\":%s,"
      "\"timeSyncInterval\":%u,\"timeSyncIntervalRTC\":%u,\"syncCount\":%lu}",
      RTCSUPPORTED ? "true" : "false",
#if RTCSUPPORTED
      config.isRTCFound() ? "true" : "false",
#else
      "false",
#endif
      systemJson, rtcJson,
      config.store.timeSyncInterval, config.store.timeSyncIntervalRTC,
      static_cast<unsigned long>(timekeeper.successfulSyncCount));
  if (length < 0 || static_cast<size_t>(length) >= sizeof(body)) {
    mqttApiError(request, 500, "time_status_failed");
    return;
  }
  stationsReply(request, 200, body);
}

void handleTimeSync(AsyncWebServerRequest* request) {
  if (!deviceApiAuthorized(request)) return;
  if (!RTCSUPPORTED) { mqttApiError(request, 404, "rtc_unsupported"); return; }
  if (network.status != CONNECTED) { mqttApiError(request, 503, "network_unavailable"); return; }
  const uint32_t beforeSync = timekeeper.successfulSyncCount;
  network.requestTimeSync(false);
  stationsReply(request, 202, String("{\"ok\":true,\"syncCount\":") + beforeSync + "}");
}

static bool mqttApiText(AsyncWebServerRequest* request, const char* key, String& value) {
  if (!request->hasParam(key, true)) return false;
  value = request->getParam(key, true)->value();
  for (size_t i = 0; i < value.length(); ++i)
    if (value[i] == '\0') return false;
  return true;
}

static __attribute__((noinline)) String mqttJsonText(const char* value) {
  char escaped[128 * 6 + 1];
  size_t used = 0;
  if (!appendJsonEscaped(escaped, sizeof(escaped), used, value)) return String();
  return String(escaped);
}

void handleMqttConfigRead(AsyncWebServerRequest* request) {
  if (!deviceApiAuthorized(request)) return;
  char autoRoot[14];
  char effectiveRoot[64];
  if (!mqttAutoRoot(autoRoot) || !mqttEffectiveRoot(effectiveRoot, sizeof(effectiveRoot))) {
    mqttApiError(request, 500, "mac_unavailable");
    return;
  }
  const MqttSettings& settings = mqttConfig();
  String body = String("{\"enabled\":") + (settings.enabled ? "true" : "false") +
      ",\"host\":\"" + mqttJsonText(settings.host) +
      "\",\"port\":" + String(settings.port) +
      ",\"username\":\"" + mqttJsonText(settings.username) +
      "\",\"passwordSet\":" + (settings.password[0] ? "true" : "false") +
      ",\"rootTopic\":\"" + mqttJsonText(settings.rootTopic) +
      "\",\"effectiveRoot\":\"" + mqttJsonText(effectiveRoot) +
      "\",\"autoRoot\":\"" + mqttJsonText(autoRoot) + "\"}";
  stationsReply(request, 200, body);
}

void handleMqttConfigSave(AsyncWebServerRequest* request) {
  if (!deviceApiAuthorized(request)) return;
  if (systemRestartPending()) { mqttApiError(request, 409, "reboot_pending"); return; }
  String enabledText, hostText, portText, usernameText, rootText;
  if (!mqttApiText(request, "enabled", enabledText) ||
      !mqttApiText(request, "host", hostText) ||
      !mqttApiText(request, "port", portText) ||
      !mqttApiText(request, "username", usernameText) ||
      !mqttApiText(request, "rootTopic", rootText)) {
    mqttApiError(request, 400, "missing_or_invalid_field");
    return;
  }
  if (enabledText != "true" && enabledText != "false" && enabledText != "1" && enabledText != "0") {
    mqttApiError(request, 400, "invalid_enabled");
    return;
  }
  if (portText.length() == 0 || portText.length() > 5 ||
      hostText.length() > 255 || usernameText.length() > 63 || rootText.length() > 255) {
    mqttApiError(request, 400, "field_too_long");
    return;
  }
  unsigned port = 0;
  for (char digit : portText) {
    if (digit < '0' || digit > '9') { mqttApiError(request, 400, "invalid_port"); return; }
    port = port * 10 + static_cast<unsigned>(digit - '0');
  }
  if (port == 0 || port > 65535) { mqttApiError(request, 400, "invalid_port"); return; }
  String newPassword;
  if (request->hasParam("password", true) && !mqttApiText(request, "password", newPassword)) {
    mqttApiError(request, 400, "invalid_password");
    return;
  }
  String clearText;
  const bool hasClear = request->hasParam("clearPassword", true);
  if (hasClear && !mqttApiText(request, "clearPassword", clearText)) {
    mqttApiError(request, 400, "invalid_clear_password");
    return;
  }
  if (hasClear && clearText != "true" && clearText != "false") {
    mqttApiError(request, 400, "invalid_clear_password");
    return;
  }
  if (newPassword.length() > 127) { mqttApiError(request, 400, "field_too_long"); return; }
  MqttSettings candidate = mqttConfig();
  candidate.enabled = enabledText == "true" || enabledText == "1";
  candidate.port = static_cast<uint16_t>(port);
  memset(candidate.host, 0, sizeof(candidate.host));
  memset(candidate.username, 0, sizeof(candidate.username));
  memset(candidate.rootTopic, 0, sizeof(candidate.rootTopic));
  if (!mqttNormalizeHost(hostText.c_str(), candidate.host, sizeof(candidate.host)) ||
      !mqttNormalizeRoot(rootText.c_str(), candidate.rootTopic, sizeof(candidate.rootTopic))) {
    mqttApiError(request, 400, "invalid_host_or_root");
    return;
  }
  usernameText.toCharArray(candidate.username, sizeof(candidate.username));
  if (!mqttApplyPassword(candidate, newPassword.c_str(), clearText == "true") ||
      !mqttValidSettings(candidate)) {
    mqttApiError(request, 400, "invalid_config");
    return;
  }
  if (!mqttSaveConfig(candidate)) { mqttApiError(request, 500, "storage_failed"); return; }
  stationsReply(request, 200, "{\"ok\":true,\"rebooting\":true}");
  scheduleSystemRestart("SYSTEM");
}

static void stationsError(AsyncWebServerRequest* request, int code, const char* error) {
  stationsReply(request, code, String("{\"error\":\"") + error + "\"}");
}

static bool stationParam(AsyncWebServerRequest* request, const char* key, String& value);

namespace {
constexpr char kPlaylistUploadPath[] = "/data/stations.upload";
constexpr size_t kPlaylistUploadLimit = 192 * 1024;
AsyncWebServerRequest* activePlaylistUpload = nullptr;

struct PlaylistImportSession {
  File file;
  size_t bytes = 0;
  bool finished = false;
  int status = 0;
  const char* error = nullptr;
};

void finishPlaylistUpload(AsyncWebServerRequest* request) {
  PlaylistImportSession* session =
      static_cast<PlaylistImportSession*>(request->_tempObject);
  if (session) {
    session->file.close();
    delete session;
    request->_tempObject = nullptr;
  }
  if (activePlaylistUpload == request) {
    activePlaylistUpload = nullptr;
    if (SPIFFS.exists(kPlaylistUploadPath)) SPIFFS.remove(kPlaylistUploadPath);
  }
}
}  // namespace

void handleStationsExport(AsyncWebServerRequest* request) {
  PlaylistGuard guard;
  std::vector<PlaylistRow> rows;
  String revision;
  if (!guard || !playlistStore.snapshot(rows, revision)) {
    stationsError(request, 500, "playlist_read_failed");
    return;
  }
  if (!SPIFFS.exists(PLAYLIST_PATH)) {
    AsyncWebServerResponse* response = request->beginResponse(
        200, "text/tab-separated-values; charset=utf-8", kStationsHeader);
    response->addHeader("Content-Disposition", "attachment; filename=\"VoxOne-stations.tsv\"");
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
    return;
  }
  File file = SPIFFS.open(PLAYLIST_PATH, "r");
  if (!file) {
    stationsError(request, 500, "playlist_read_failed");
    return;
  }
  AsyncWebServerResponse* response = request->beginResponse(
      file, "VoxOne-stations.tsv", "text/tab-separated-values; charset=utf-8", true);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void handleStationsImportUpload(AsyncWebServerRequest* request, String,
                                size_t index, uint8_t* data, size_t len, bool final) {
  PlaylistImportSession* session =
      static_cast<PlaylistImportSession*>(request->_tempObject);
  if (!session) {
    session = new (std::nothrow) PlaylistImportSession;
    if (!session) return;
    request->_tempObject = session;
    request->onDisconnect([request]() { finishPlaylistUpload(request); });
    if (activePlaylistUpload && activePlaylistUpload != request) {
      session->status = 507;
      session->error = "insufficient_storage";
      return;
    }
    activePlaylistUpload = request;
    if (index != 0 || (SPIFFS.exists(kPlaylistUploadPath) &&
                       !SPIFFS.remove(kPlaylistUploadPath))) {
      session->status = 500;
      session->error = "upload_failed";
      return;
    }
    session->file = SPIFFS.open(kPlaylistUploadPath, "w");
    if (!session->file) {
      session->status = 507;
      session->error = "insufficient_storage";
      return;
    }
  }
  if (session->error) return;
  if (session->finished || index != session->bytes) {
    session->status = 400;
    session->error = "bad_upload";
    return;
  }
  if (len > kPlaylistUploadLimit - session->bytes) {
    session->status = 413;
    session->error = "playlist_too_large";
    return;
  }
  if (len && session->file.write(data, len) != len) {
    session->status = 507;
    session->error = "insufficient_storage";
    return;
  }
  session->bytes += len;
  if (final) {
    session->file.flush();
    session->file.close();
    session->finished = true;
  }
}

void handleStationsImport(AsyncWebServerRequest* request) {
  PlaylistImportSession* session =
      static_cast<PlaylistImportSession*>(request->_tempObject);
  if (!request->multipart()) {
    stationsError(request, 400, "bad_request");
    finishPlaylistUpload(request);
    return;
  }
  String clientRevision;
  if (!stationParam(request, "revision", clientRevision) ||
      clientRevision.length() != 8) {
    stationsError(request, 400, "bad_revision");
    finishPlaylistUpload(request);
    return;
  }
  clientRevision.toUpperCase();
  for (char ch : clientRevision) {
    if (!isxdigit(static_cast<unsigned char>(ch))) {
      stationsError(request, 400, "bad_revision");
      finishPlaylistUpload(request);
      return;
    }
  }
  const bool hasEmpty = request->hasParam("empty", true);
  const bool empty = hasEmpty && request->getParam("empty", true)->value() == "1";
  if (hasEmpty) {
    stationsError(request, 422, "invalid_stations");
    finishPlaylistUpload(request);
    return;
  }
  size_t fileCount = 0;
  for (size_t i = 0; i < request->params(); ++i)
    if (request->getParam(i)->isFile()) ++fileCount;

  PlaylistGuard guard;
  std::vector<PlaylistRow> rows;
  String currentRevision;
  if (!guard || !playlistStore.recover() ||
      !playlistStore.snapshot(rows, currentRevision)) {
    stationsError(request, 500, "playlist_read_failed");
    finishPlaylistUpload(request);
    return;
  }
  if (clientRevision != currentRevision) {
    stationsError(request, 409, "revision_conflict");
    finishPlaylistUpload(request);
    return;
  }
  const size_t oldCount = rows.size();
  if (hasEmpty && !empty) {
    stationsError(request, 400, "bad_empty");
    finishPlaylistUpload(request);
    return;
  }
  if ((!empty && fileCount == 0) || (empty && (fileCount != 0 || session))) {
    stationsError(request, 400, empty ? "bad_request" : "missing_file");
    finishPlaylistUpload(request);
    return;
  }
  if (fileCount > 1) {
    stationsError(request, 400, "bad_request");
    finishPlaylistUpload(request);
    return;
  }
  if (activePlaylistUpload && activePlaylistUpload != request) {
    stationsError(request, 507, "insufficient_storage");
    finishPlaylistUpload(request);
    return;
  }
  if (!empty) {
    if (!session) {
      stationsError(request, 500, "upload_failed");
      return;
    }
    if (session->error) {
      stationsError(request, session->status, session->error);
      finishPlaylistUpload(request);
      return;
    }
    if (!session->finished || !session->bytes) {
      stationsError(request, 400, "bad_upload");
      finishPlaylistUpload(request);
      return;
    }
    File uploaded = SPIFFS.open(kPlaylistUploadPath, "r");
    const bool intact = uploaded && uploaded.size() == session->bytes;
    uploaded.close();
    if (!intact) {
      stationsError(request, 500, "upload_failed");
      finishPlaylistUpload(request);
      return;
    }
    const PlaylistReadError readResult =
        playlistStore.readImport(kPlaylistUploadPath, rows);
    if (readResult != PlaylistReadError::OK) {
      stationsError(request, readResult == PlaylistReadError::INVALID ? 422 : 500,
                    readResult == PlaylistReadError::INVALID ?
                        "invalid_stations" : "upload_failed");
      finishPlaylistUpload(request);
      return;
    }
    // Free upload space before PlaylistStore creates its transactional copy.
    if (!SPIFFS.remove(kPlaylistUploadPath)) {
      stationsError(request, 500, "upload_failed");
      finishPlaylistUpload(request);
      return;
    }
  } else {
    if (SPIFFS.exists(kPlaylistUploadPath) &&
        !SPIFFS.remove(kPlaylistUploadPath)) {
      stationsError(request, 500, "upload_failed");
      finishPlaylistUpload(request);
      return;
    }
    rows.clear();
  }
  const uint16_t oldCurrent = config.lastStation();
  if (oldCurrent > oldCount) {
    stationsError(request, 500, "invalid_playlist_state");
    finishPlaylistUpload(request);
    return;
  }
  const uint16_t newCurrent = stationImportCurrent(rows, oldCurrent, config.station.id);
  const uint64_t oldId = config.station.id;
  const String oldUrl = config.station.url;
  const uint8_t oldMode = config.station.metadataMode;
  String newRevision;
  const PlaylistWriteError result =
      playlistStore.commit(rows, newCurrent, newRevision);
  if (result != PlaylistWriteError::OK) {
    if (result == PlaylistWriteError::NO_SPACE)
      stationsError(request, 507, "insufficient_storage");
    else if (result == PlaylistWriteError::INVALID)
      stationsError(request, 422, "invalid_stations");
    else
      stationsError(request, 500, "playlist_write_failed");
    finishPlaylistUpload(request);
    return;
  }
  finishPlaylistUpload(request);
  if (newCurrent == 0) {
    config.station.id = 0;
    config.station.metadataMode = STATION_META_NORMAL;
    config.station.name[0] = '\0';
    config.station.url[0] = '\0';
    config.station.title[0] = '\0';
    config.station.ovol = 0;
    player.sendCommand({PR_STOP, 0});
    display.putRequest(NEWSTATION);
    display.putRequest(NEWTITLE);
    netserver.requestOnChange(STATION, 0);
    netserver.requestOnChange(TITLE, 0);
  } else {
    const PlaylistRow& selected = rows[newCurrent - 1];
    if (player.isRunning() && (oldId != selected.id || oldUrl != selected.url)) {
      player.sendCommand({PR_PLAY, newCurrent});
    } else {
      config.loadStation(newCurrent);
      display.putRequest(NEWSTATION);
      netserver.requestOnChange(STATION, 0);
    }
  }
  if (newCurrent != 0 && oldMode != config.station.metadataMode) {
    display.putRequest(NEWTITLE);
    netserver.requestOnChange(TITLE, 0);
  }
  stationsReply(request, 200, String("{\"ok\":true,\"revision\":\"") +
      newRevision + "\",\"current\":" + String(newCurrent) +
      ",\"count\":" + String(rows.size()) + "}");
}

static bool stationNumber(const String& text, uint16_t& number) {
  if (!text.length() || text.length() > 5) return false;
  uint32_t value = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    value = value * 10 + text[i] - '0';
  }
  if (value > UINT16_MAX) return false;
  number = static_cast<uint16_t>(value);
  return true;
}

static bool stationParam(AsyncWebServerRequest* request, const char* key, String& value) {
  if (!request->hasParam(key, true)) return false;
  value = request->getParam(key, true)->value();
  return true;
}

static bool stationPositionByRequestId(AsyncWebServerRequest* request,
                                       const std::vector<PlaylistRow>& rows,
                                       uint16_t& number) {
  String idText;
  uint64_t id = 0;
  if (!stationParam(request, "id", idText) || !stationParseId(idText.c_str(), id)) return false;
  return stationFindPositionById(rows, id, number);
}

void handleStationMetadata(AsyncWebServerRequest* request) {
  String revision, idText, swapText;
  uint64_t id = 0;
  if (!stationParam(request, "revision", revision) || revision.length() != 8 ||
      !stationParam(request, "id", idText) || !stationParseId(idText.c_str(), id) ||
      !stationParam(request, "swapArtistTitle", swapText) ||
      (swapText != "0" && swapText != "1")) {
    stationsError(request, 400, "bad_parameter");
    return;
  }
  revision.toUpperCase();
  for (char ch : revision) {
    if (!isxdigit(static_cast<unsigned char>(ch))) {
      stationsError(request, 400, "bad_revision");
      return;
    }
  }
  PlaylistGuard guard;
  std::vector<PlaylistRow> rows;
  String currentRevision;
  if (!guard || !playlistStore.recover() ||
      !playlistStore.snapshot(rows, currentRevision)) {
    stationsError(request, 500, "playlist_read_failed");
    return;
  }
  if (!stationRevisionMatches(revision.c_str(), currentRevision.c_str())) {
    stationsError(request, 409, "revision_conflict");
    return;
  }
  size_t index = 0;
  while (index < rows.size() && rows[index].id != id) ++index;
  if (index == rows.size()) {
    stationsError(request, 404, "station_not_found");
    return;
  }
  rows[index].metadataMode = swapText == "1" ? STATION_META_SWAP : STATION_META_NORMAL;
  String newRevision;
  const bool active = config.station.id == id;
  const PlaylistWriteError metadataResult =
      playlistStore.commit(rows, config.lastStation(), newRevision);
  if (metadataResult != PlaylistWriteError::OK) {
    stationsError(request, metadataResult == PlaylistWriteError::NO_SPACE ? 507 : 500,
                  "station_write_failed");
    return;
  }
  if (active && config.station.metadataMode != rows[index].metadataMode) {
    config.station.metadataMode = rows[index].metadataMode;
    display.putRequest(NEWTITLE);
    netserver.requestOnChange(TITLE, 0);
  }
  stationsReply(request, 200, String("{\"ok\":true,\"revision\":\"") +
      newRevision + "\",\"id\":\"" + idText +
      "\",\"metadataMode\":\"" + (swapText == "1" ? "swap" : "normal") +
      "\",\"swapArtistTitle\":" + (swapText == "1" ? "true" : "false") + "}");
}

void handleStationsMutation(AsyncWebServerRequest *request) {
  const String route = request->url();
  const bool add = route == "/api/stations/add";
  const bool edit = route == "/api/stations/edit";
  const bool remove = route == "/api/stations/delete";
  const bool reorder = route == "/api/stations/reorder";
  if (!add && !edit && !remove && !reorder) {
    stationsError(request, 404, "not_found");
    return;
  }
  String clientRevision;
  if (!stationParam(request, "revision", clientRevision) || clientRevision.length() != 8) {
    stationsError(request, 400, "bad_revision");
    return;
  }
  clientRevision.toUpperCase();
  for (char ch : clientRevision) {
    if (!isxdigit(static_cast<unsigned char>(ch))) {
      stationsError(request, 400, "bad_revision");
      return;
    }
  }
  PlaylistGuard guard;
  std::vector<PlaylistRow> rows;
  String currentRevision;
  if (!guard || !playlistStore.recover() ||
      !playlistStore.snapshot(rows, currentRevision)) {
    stationsError(request, 500, "playlist_read_failed");
    return;
  }
  if (clientRevision != currentRevision) {
    stationsError(request, 409, "revision_conflict");
    return;
  }
  const uint16_t oldCurrent = config.lastStation();
  uint16_t newCurrent = oldCurrent;
  uint16_t number = 0;
  bool reconnect = false, stop = false, refreshCurrent = false;
  if (oldCurrent > rows.size()) {
    stationsError(request, 500, "invalid_playlist_state");
    return;
  }
  if (add || edit) {
    String name, url, ovolText;
    if (!stationParam(request, "name", name) || !stationParam(request, "url", url) ||
        !stationParam(request, "ovol", ovolText)) {
      stationsError(request, 400, "missing_parameter");
      return;
    }
    int ovol = 0;
    if (!PlaylistStore::parseInteger(ovolText, ovol)) {
      stationsError(request, 400, "bad_ovol");
      return;
    }
    PlaylistRow replacement;
    replacement.name = name;
    replacement.url = url;
    replacement.ovol = ovol;
    if (edit) {
      if (!stationPositionByRequestId(request, rows, number)) {
        stationsError(request, 404, "station_not_found");
        return;
      }
      reconnect = number == oldCurrent && player.isRunning() &&
                  rows[number - 1].url != replacement.url;
      refreshCurrent = number == oldCurrent;
      replacement.id = rows[number - 1].id;
      replacement.metadataMode = rows[number - 1].metadataMode;
      if (!PlaylistStore::validRecord(replacement)) {
        stationsError(request, 422, "invalid_station");
        return;
      }
      rows[number - 1] = replacement;
    } else {
      if (rows.size() >= UINT16_MAX) {
        stationsError(request, 507, "playlist_full");
        return;
      }
      replacement.id = playlistStore.generateId(rows);
      if (!replacement.id) { stationsError(request, 500, "id_generation_failed"); return; }
      if (!PlaylistStore::validRecord(replacement)) {
        stationsError(request, 422, "invalid_station");
        return;
      }
      rows.push_back(replacement);
    }
  }
  if (remove) {
    if (!stationPositionByRequestId(request, rows, number)) {
      stationsError(request, 404, "station_not_found");
      return;
    }
    rows.erase(rows.begin() + number - 1);
    newCurrent = stationAfterDelete(oldCurrent, number, rows.size());
    if (number == oldCurrent) {
      reconnect = newCurrent != 0 && player.isRunning();
      stop = newCurrent == 0;
    }
    refreshCurrent = number <= oldCurrent;
  }
  if (reorder) {
    String toText;
    uint16_t from = 0, to = 0;
    if (!stationPositionByRequestId(request, rows, from)) {
      stationsError(request, 404, "station_not_found");
      return;
    }
    if (!stationParam(request, "targetPosition", toText) || !stationNumber(toText, to)) {
      stationsError(request, 400, "bad_number");
      return;
    }
    if (from == 0 || to == 0 || from > rows.size() || to > rows.size()) {
      stationsError(request, 404, "station_not_found");
      return;
    }
    PlaylistRow moved = rows[from - 1];
    rows.erase(rows.begin() + from - 1);
    rows.insert(rows.begin() + to - 1, moved);
    newCurrent = stationAfterMove(oldCurrent, from, to);
    refreshCurrent = newCurrent != oldCurrent;
  }
  String newRevision;
  const PlaylistWriteError result = playlistStore.commit(rows, newCurrent, newRevision);
  if (result != PlaylistWriteError::OK) {
    if (result == PlaylistWriteError::INVALID) stationsError(request, 422, "invalid_station");
    else if (result == PlaylistWriteError::NO_SPACE) stationsError(request, 507, "insufficient_storage");
    else stationsError(request, 500, "playlist_write_failed");
    return;
  }
  const uint8_t previousMode = config.station.metadataMode;
  if (edit && number == oldCurrent && !reconnect) {
    const PlaylistRow& active = rows[number - 1];
    const bool volumeChanged = config.station.ovol != active.ovol;
    strlcpy(config.station.name, active.name.c_str(), sizeof(config.station.name));
    strlcpy(config.station.url, active.url.c_str(), sizeof(config.station.url));
    config.station.ovol = active.ovol;
    config.station.id = active.id;
    config.station.metadataMode = active.metadataMode;
    if (volumeChanged) player.setVol(config.store.volume);
    display.putRequest(NEWSTATION);
  }
  if (remove && number == oldCurrent && newCurrent != 0 && !reconnect) {
    config.loadStation(newCurrent);
    display.putRequest(NEWSTATION);
  }
  if (stop) {
    config.station.id = 0;
    config.station.metadataMode = STATION_META_NORMAL;
    config.station.name[0] = '\0';
    config.station.url[0] = '\0';
    config.station.title[0] = '\0';
    player.sendCommand({PR_STOP, 0});
    display.putRequest(NEWSTATION);
    display.putRequest(NEWTITLE);
    netserver.requestOnChange(STATION, 0);
    netserver.requestOnChange(TITLE, 0);
  } else if (reconnect) {
    netserver.requestOnChange(ITEM, 0);
    player.sendCommand({PR_PLAY, newCurrent});
  } else if (refreshCurrent) {
    netserver.requestOnChange(STATION, 0);
  }
  if (!reconnect && !stop && previousMode != config.station.metadataMode) {
    display.putRequest(NEWTITLE);
    netserver.requestOnChange(TITLE, 0);
  }
  stationsReply(request, 200, String("{\"ok\":true,\"revision\":\"") +
      newRevision + "\",\"current\":" + String(newCurrent) +
      ",\"count\":" + String(rows.size()) + "}");
}

void NetServer::processQueue(){
  if(nsQueue==NULL) return;
  nsRequestParams_t request;
  if(xQueueReceive(nsQueue, &request, NS_QUEUE_TICKS)){
    uint32_t clientId = request.clientId;
    wsBuf[0]='\0';
    switch (request.type) {
      case GETINDEX:      {
          requestOnChange(STATION, clientId); 
          requestOnChange(TITLE, clientId); 
          requestOnChange(VOLUME, clientId); 
          requestOnChange(EQUALIZER, clientId); 
          requestOnChange(BALANCE, clientId); 
          requestOnChange(BITRATE, clientId); 
          requestOnChange(MODE, clientId); 
          return; 
          break;
        }
      case GETSYSTEM: {
        uint8_t mac[6]{};
        char macText[18] = "";
        if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
          snprintf(macText, sizeof(macText), "%02X:%02X:%02X:%02X:%02X:%02X",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
        const IPAddress address = network.status == SOFT_AP ? WiFi.softAPIP() : WiFi.localIP();
        char ipText[16];
        snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", address[0], address[1],
                 address[2], address[3]);

        char capabilities[128] = "";
        size_t capabilityLength = 0;
        const struct { bool enabled; const char* name; } profileCapabilities[] = {
          {voxone::activeProfile.capabilities.hasDisplay, "DISPLAY"},
          {voxone::activeProfile.capabilities.hasEncoder, "ENCODER"},
          {voxone::activeProfile.capabilities.hasBt, "BT"},
          {voxone::activeProfile.capabilities.hasVu, "VU"},
          {RTCSUPPORTED, "RTC"},
          {voxone::activeProfile.capabilities.hasAux, "AUX"},
          {voxone::activeProfile.capabilities.hasSpdif, "SPDIF"},
          {voxone::activeProfile.capabilities.hasTda7719, "TDA7719"}
        };
        for (const auto& capability : profileCapabilities) {
          if (!capability.enabled || capabilityLength >= sizeof(capabilities)) continue;
          const int written = snprintf(capabilities + capabilityLength,
              sizeof(capabilities) - capabilityLength, "%s%s",
              capabilityLength ? ", " : "", capability.name);
          if (written > 0) capabilityLength = std::min(
              capabilityLength + static_cast<size_t>(written), sizeof(capabilities) - 1);
        }

        const uint32_t internalHeapCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        const uint32_t freeHeap = heap_caps_get_free_size(internalHeapCaps);
        const uint32_t minimumFreeHeap = heap_caps_get_minimum_free_size(internalHeapCaps);
        const uint32_t psramTotal = ESP.getPsramSize();
        const uint32_t psramFree = ESP.getFreePsram();
        const int wifiRssi = network.status == CONNECTED ? WiFi.RSSI() : -127;
        const unsigned long long uptimeSeconds =
            static_cast<unsigned long long>(esp_timer_get_time() / 1000000LL);
        const int prefixLength = snprintf(wsBuf, sizeof(wsBuf),
            "{\"sst\":%d,\"vu\":%d,\"canVu\":%d,\"softr\":%d,\"vut\":%d,\"mdns\":\"%s\",\"ipaddr\":\"%s\",\"abuff\":%d,\"systemInfo\":{\"version\":\"%s\",\"channel\":\"%s\",\"build\":\"%s\",\"profile\":\"%s\",\"mac\":\"%s\",\"rssi\":%d,\"uptimeSeconds\":%llu,\"freeHeap\":%lu,\"minimumFreeHeap\":%lu,\"psramTotal\":%lu,\"psramFree\":%lu,\"capabilities\":\"%s\"},\"networkInfo\":",
            config.store.smartstart != 2, config.store.vumeter,
            voxone::activeProfile.capabilities.hasVu, config.store.softapdelay,
            config.vuThreshold, config.store.mdnsname, ipText, config.store.abuff,
            VOXONE_VERSION, VOXONE_BUILD_CHANNEL, VOXONE_BUILD_SHA,
            VOXONE_BUILD_PROFILE, macText, wifiRssi,
            uptimeSeconds, static_cast<unsigned long>(freeHeap),
            static_cast<unsigned long>(minimumFreeHeap),
            static_cast<unsigned long>(psramTotal),
            static_cast<unsigned long>(psramFree), capabilities);
        if (prefixLength < 0 || static_cast<size_t>(prefixLength) >= sizeof(wsBuf)) {
          wsBuf[0] = '\0';
          break;
        }
        size_t used = static_cast<size_t>(prefixLength);
        if (!formatNetworkInfo(wsBuf, sizeof(wsBuf), used) ||
            !appendWebStatusLiteral(wsBuf, sizeof(wsBuf), used, "}")) wsBuf[0] = '\0';
        break;
      }
      case GETSCREEN:     snprintf (wsBuf, sizeof(wsBuf), "{\"flip\":%d,\"canFlip\":%d,\"canBrightness\":%d,\"br\":%d,\"nump\":%d,\"dspon\":%d,\"con\":%d,\"scre\":%d,\"scrt\":%d,\"scrb\":%d,\"scrpe\":%d,\"scrpt\":%d,\"scrpb\":%d,\"stationListTimeout\":%u,\"btTransportTimeout\":%u,\"canBtTransport\":%d}",
                                  config.store.flipscreen,
                                  voxone::activeProfile.display != voxone::Display::None,
                                  BRIGHTNESS_PIN != 255,
                                  config.store.brightness,
                                  config.store.numplaylist,
                                  config.store.dspon, 
                                  config.store.contrast,
                                  config.store.screensaverEnabled,
                                  config.store.screensaverTimeout,
                                  config.store.screensaverBlank,
                                  config.store.screensaverPlayingEnabled,
                                  config.store.screensaverPlayingTimeout,
                                  config.store.screensaverPlayingBlank,
                                  uiTimeoutConfig().stationListSeconds,
                                  uiTimeoutConfig().btTransportSeconds,
                                  VOXONE_HAS_BT && DSP_MODEL == DSP_ST7796);
                                  break;
      case STATION:       requestOnChange(STATIONNAME, clientId); requestOnChange(ITEM, clientId); break;
      case STATIONNAME:   formatWsTextPayload(wsBuf, sizeof(wsBuf), "nameset", config.station.name); break;
      case ITEM:          sprintf (wsBuf, "{\"current\": %d}", config.lastStation()); break;
      case TITLE: {
        char interpreted[BUFLEN + 1];
        stationMetaDisplay(config.station.title, config.station.metadataMode == STATION_META_SWAP,
                           interpreted, sizeof(interpreted));
        formatWsTextPayload(wsBuf, sizeof(wsBuf), "meta", interpreted);
        serialCli.printf("##CLI.META#: %s\n> ", interpreted);
        break;
      }
      case VOLUME:        sprintf (wsBuf, "{\"payload\":[{\"id\":\"volume\",\"value\":%d},{\"id\":\"volume100\",\"value\":%d},{\"id\":\"muted\",\"value\":%d},{\"id\":\"maximumVolume\",\"value\":%d},{\"id\":\"startupMode\",\"value\":%d},{\"id\":\"startupFixedVolume\",\"value\":%d}]}", config.store.volume, config.userVolume, player.isMuted(), config.store.maximumVolume, config.store.startupMode, config.store.startupFixedVolume); serialCli.printf("##CLI.VOL#: %d\n", config.store.volume); break;
      case NRSSI:         rssi = WiFi.RSSI(); sprintf (wsBuf, "{\"payload\":[{\"id\":\"rssi\", \"value\": %d}, {\"id\":\"heap\", \"value\": %d}]}", rssi, (player.isRunning() && config.store.audioinfo)?(int)(100*player.inBufferFilled()/playerBufMax):0); /*rssi = 255;*/ break;
      case BITRATE:       sprintf (wsBuf, "{\"payload\":[{\"id\":\"bitrate\", \"value\": %d}, {\"id\":\"fmt\", \"value\": \"%s\"}]}", config.station.bitrate, getFormat(config.configFmt)); break;
      case MODE:          sprintf (wsBuf, "{\"payload\":[{\"id\":\"playerwrap\", \"value\": \"%s\"}]}", player.status() == PLAYING ? "playing" : "stopped"); serialCli.info(); break;
      case EQUALIZER:     sprintf (wsBuf, "{\"payload\":[{\"id\":\"bass\", \"value\": %d}, {\"id\": \"middle\", \"value\": %d}, {\"id\": \"trebble\", \"value\": %d}]}", config.store.bass, config.store.middle, config.store.trebble); break;
      case BALANCE:       sprintf (wsBuf, "{\"payload\":[{\"id\": \"balance\", \"value\": %d}]}", config.store.balance); break;
      case WEBSTATUS:     formatWebStatus(wsBuf, sizeof(wsBuf)); break;
      default:          break;
    }
    if (strlen(wsBuf) > 0) {
      if (clientId == 0) { websocket.textAll(wsBuf); }else{ websocket.text(clientId, wsBuf); }
      if (clientId == 0 && (request.type == STATION || request.type == ITEM || request.type == TITLE || request.type == MODE)) mqttPublishStatus();
      if (clientId == 0 && request.type == VOLUME) mqttPublishVolume();
    }
    if (clientId == 0 && websocket.count() > 0 &&
        (request.type == STATIONNAME || request.type == TITLE ||
         request.type == BITRATE || request.type == MODE)) {
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
      if (!bluetoothSourceSelected())
#endif
      {
        formatWebStatus(wsBuf, sizeof(wsBuf));
        if (wsBuf[0]) websocket.textAll(wsBuf);
      }
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
  sprintf(wsBuf, "{\"payload\":[{\"id\":\"volume\",\"value\":%d},{\"id\":\"volume100\",\"value\":%d},{\"id\":\"muted\",\"value\":%d},{\"id\":\"maximumVolume\",\"value\":%d},{\"id\":\"startupMode\",\"value\":%d},{\"id\":\"startupFixedVolume\",\"value\":%d}]}", config.store.volume, config.userVolume, player.isMuted(), config.store.maximumVolume, config.store.startupMode, config.store.startupFixedVolume);
  if(hasWebClients) websocket.textAll(wsBuf);
  serialCli.printf("##CLI.VOL#: %d\n", config.store.volume);
  mqttPublishVolume();
}

void NetServer::loop() {
  if (systemOperationState.audioBlocked() && !systemOperationState.blocksRequests() &&
      systemOperationState.isRadioStopped())
    systemOperationState.releaseFailedUpdateAudio();
  if (systemRestartPending() && (int32_t)(millis() - webUpdateRebootAt) >= 0) {
    Serial.println("Rebooting...");
    ESP.restart();
  }
  if (systemOperationState.blocksRequests()) return;
  // DspTask and the player task can both call loop(); only one may use wsBuf.
  portENTER_CRITICAL(&netserverLoopMux);
  if (netserverLoopActive) {
    portEXIT_CRITICAL(&netserverLoopMux);
    return;
  }
  netserverLoopActive = true;
  portEXIT_CRITICAL(&netserverLoopMux);
  processQueue();
#if defined(VOXONE_PROFILE_SALON)
  voxone::dsp::processDspTransportQueue();
#endif
  processVolumeUpdate();
  websocket.cleanupClients();
  portENTER_CRITICAL(&netserverLoopMux);
  netserverLoopActive = false;
  portEXIT_CRITICAL(&netserverLoopMux);
  //processQueue();
}

void NetServer::onWsMessage(void *arg, uint8_t *data, size_t len, uint32_t clientId) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
#if defined(VOXONE_PROFILE_SALON)
  if (info->index == 0 &&
      voxone::dsp::handleDspWsFrame(data, len, clientId,
          info->final && info->len == len && info->opcode == WS_TEXT)) return;
#endif
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    if (config.parseWsCommand((const char*)data, _wscmd, _wsval, 65)) {
      if (strcmp(_wscmd, "ping") == 0) {
        websocket.text(clientId, "{\"pong\": 1}");
        return;
      }
      if (strcmp(_wscmd, "tone") == 0) {
        int8_t bass, middle, trebble;
        if (len <= 64 && parseToneValue(_wsval, bass, middle, trebble)) {
          config.setTone(bass, middle, trebble);
        }
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
      if(cmd.exec(_wscmd, _wsval, clientId)){
        return;
      }
    }
  }
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
    if (systemOperationState.blocksRequests()) {
      session->error = "Update or restart already in progress";
      return;
    }
    if (activeUpdateRequest && activeUpdateRequest != request) {
      session->error = "Another update is already running";
      return;
    }
    activeUpdateRequest = request;
    request->onDisconnect([request]() {
      if (activeUpdateRequest == request) {
        WebUpdateSession* abandoned = static_cast<WebUpdateSession*>(request->_tempObject);
        Update.abort();
        activeUpdateRequest = nullptr;
        remountFilesystemAfterFailedUpdate();
        if (abandoned && abandoned->audioBlocked) finishFailedUpdateAudio();
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
    if (!systemOperationState.updateStarted()) {
      session->error = "Update already in progress";
      return;
    }
    session->audioBlocked = true;
    network.lostPlaying = false;
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
    sourceManagerStopForUpdate();
#if VOXONE_BT_I2S_RX_ENABLED
    if (!btAudioInput.blockForUpdate()) {
      session->error = "Could not stop Bluetooth audio output";
      return;
    }
#endif
#endif
    player.resetQueue();
    player.sendCommand({PR_STOP, 0});
    display.putRequest(NEWMODE, UPDATING);
    const uint32_t stopStarted = millis();
    while (!systemOperationState.isRadioStopped() && millis() - stopStarted < 2000)
      delay(1);
    if (!systemOperationState.isRadioStopped()) {
      session->error = "Radio did not stop before update";
      return;
    }
    if (session->target == U_SPIFFS) {
      if (!backupWebUpdateData(session->error)) return;
      systemOperationState.filesystemUnmounting();
      SPIFFS.end();
      Serial.println("##[UPDATE]# SPIFFS unmounted");
    }
    if (!Update.begin(session->expected ? session->expected : UPDATE_SIZE_UNKNOWN,
                      session->target)) {
      Serial.printf("Web Update begin failed: %s\n", Update.errorString());
      session->error = "Update.begin failed (see Serial)";
      return;
    }
    session->started = true;
    Serial.printf("Web Update started: %s, %u bytes, limit %u\n",
                  session->target == U_FLASH ? "firmware" : "SPIFFS",
                  static_cast<unsigned>(session->expected),
                  static_cast<unsigned>(session->limit));
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
      Serial.printf("##[UPDATE]# upload complete: %u bytes\n", static_cast<unsigned>(index + len));
      scheduleSystemRestart("UPDATE");
    }
    if (session->finished) activeUpdateRequest = nullptr;
  }
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (systemOperationState.blocksRequests() && request->url() != "/update") return;
  if(request->url()=="/update"){
    handleWebUpdateUpload(request, filename, index, data, len, final);
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT: if (config.store.audioinfo) Serial.printf("[WEBSOCKET] client #%lu connected from %s\n", client->id(), config.ipToStr(client->remoteIP())); break;
    case WS_EVT_DISCONNECT: if (config.store.audioinfo) Serial.printf("[WEBSOCKET] client #%lu disconnected\n", client->id()); break;
    case WS_EVT_DATA: if (!systemOperationState.blocksRequests()) netserver.onWsMessage(arg, data, len, client->id()); break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}
void handleNotFound(AsyncWebServerRequest * request) {
  if (systemOperationState.blocksRequests() &&
      !(request->method() == HTTP_POST && request->url() == "/update")) {
    request->send(503, "text/plain", "Update in progress");
    return;
  }
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
  if (request->method() == HTTP_GET && request->url() == "/update.html") {
    request->redirect("/#update");
    return;
  }
  if (request->method() == HTTP_GET) {
    DBGVB("[%s] client ip=%s request of %s", __func__, config.ipToStr(request->client()->remoteIP()), request->url().c_str());
    if (strcmp(request->url().c_str(), PLAYLIST_PATH) == 0) {
      netserver.chunkedHtmlPage("application/octet-stream", request, request->url().c_str());
      return;
    }
  }// if (request->method() == HTTP_GET)
  
  if (request->method() == HTTP_POST) {
    if(request->url()=="/update"){
#if defined(HTTP_USER) && defined(HTTP_PASS)
      if (network.status == CONNECTED && !request->authenticate(HTTP_USER, HTTP_PASS)) {
        if (activeUpdateRequest == request) {
          Update.abort();
          activeUpdateRequest = nullptr;
          remountFilesystemAfterFailedUpdate();
          WebUpdateSession* session = static_cast<WebUpdateSession*>(request->_tempObject);
          if (session && session->audioBlocked) finishFailedUpdateAudio();
        }
        request->requestAuthentication();
        return;
      }
#endif
      WebUpdateSession* session = static_cast<WebUpdateSession*>(request->_tempObject);
      const bool success = session && session->finished && !session->error;
      if (!success) {
        if (activeUpdateRequest == request) {
          Update.abort();
          activeUpdateRequest = nullptr;
          remountFilesystemAfterFailedUpdate();
          if (session && session->audioBlocked) finishFailedUpdateAudio();
        }
      }
      const char* message = success ? "OK" :
          (session && session->error ? session->error : "No valid update image received");
      AsyncWebServerResponse *response = request->beginResponse(
          success ? 200 : 400, "text/plain", message);
      response->addHeader("Connection", "close");
      response->addHeader("Cache-Control", "no-store");
      request->send(response);
      return;
    }
  }// if (request->method() == HTTP_POST)
  
  if (request->url() == "/variables.js") {
    sprintf (netserver.nsBuf, "var voxOneVersion='%s';\nvar voxOneChannel='%s';\nvar voxOneBuild='%s';\nvar yoRadioVersion='%s';\nvar yoVersion=voxOneVersion;\nvar voxOneProfile='%s';\nvar playMode='%s';\n", VOXONE_VERSION, VOXONE_BUILD_CHANNEL, VOXONE_BUILD_SHA, YOVERSION, VOXONE_BUILD_PROFILE, (network.status == CONNECTED)?"player":"ap");
    request->send(200, "text/html", netserver.nsBuf);
    return;
  }
  Serial.print("Not Found: ");
  Serial.println(request->url());
  request->send(404, "text/plain", "Not found");
}

void handleIndex(AsyncWebServerRequest * request) {
  if (systemOperationState.blocksRequests()) {
    request->send(503, "text/plain", "Update in progress");
    return;
  }
  if (request->url() == "/" && apWifiRecoveryAllowed(network.status == CONNECTED)) {
    if (request->method() == HTTP_GET) {
      request->send_P(200, "text/html", emptyfs_html);
      return;
    }
    if (request->method() == HTTP_POST) {
      const String ssid = request->arg("ssid");
      const String password = request->arg("pass");
      if (!apWifiCredentialsValid(ssid.c_str(), password.c_str())) {
        request->send(400, "text/plain", "Invalid Wi-Fi credentials");
        return;
      }
      if (!config.saveWifiCredentials(ssid.c_str(), password.c_str())) {
        request->send(500, "text/plain", "Could not save Wi-Fi credentials");
        return;
      }
      request->send(200, "text/plain", "Wi-Fi credentials saved. Restarting.");
      scheduleSystemRestart("SYSTEM");
      Serial.println("##[BOOT]# AP Wi-Fi credentials saved; restart scheduled");
      return;
    }
  }
  if(!config.currentWwwReady){
    Serial.print("Not Found: ");
    Serial.println(request->url());
    request->send(404, "text/plain", "Not found");
    return;
  } // end if(!config.currentWwwReady)
#if defined(HTTP_USER) && defined(HTTP_PASS)
  if(network.status == CONNECTED)
    if (!request->authenticate(HTTP_USER, HTTP_PASS)) {
      return request->requestAuthentication();
    }
#endif
  if (strcmp(request->url().c_str(), "/") == 0 && request->params() == 0) {
    request->redirect("/voxone.html");
    return;
  }
  request->send(404, "text/plain", "Not found");
}
