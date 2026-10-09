#include "options.h"
#include "config.h"
#include "mqtt.h"
#include "mqtt_config.h"
#include "station_metadata.h"
#include "WiFi.h"
#include "player.h"
#include "commandhandler.h"
#include "update_progress.h"
#include "mqtt_update_shutdown_state.h"

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer = nullptr;
char topic[100], status[BUFLEN * 2];

namespace {
bool initialized = false;
bool runtimeEnabled = false;
bool wifiAvailable = false;
char effectiveRoot[64];
IPAddress publishedPlaylistIp;
MqttUpdateShutdownState updateShutdown;

bool makeTopic(const char* suffix) {
  const int length = snprintf(topic, sizeof(topic), "%s/%s", effectiveRoot, suffix);
  return length > 0 && static_cast<size_t>(length) < sizeof(topic);
}
}

bool mqttActive() { return runtimeEnabled; }

void connectToMqtt() {
  if (!updateShutdown.reconnectAllowed() || !runtimeEnabled || !wifiAvailable || !WiFi.isConnected()) return;
  if (mqttReconnectTimer) xTimerStop(mqttReconnectTimer, 0);
  if (!updateShutdown.reconnectAllowed()) return;
  mqttClient.connect();
}

void mqttBeginUpdateShutdown() {
  if (!updateShutdown.begin()) return;
  if (mqttReconnectTimer) xTimerStop(mqttReconnectTimer, 0);
  // Force-close any current MQTT TCP socket; never change persistent settings.
  if (runtimeEnabled) mqttClient.disconnect(true);
}

bool mqttIsConnected() { return runtimeEnabled && mqttClient.connected(); }
bool mqttTcpActive() { return runtimeEnabled && mqttClient.tcpActive(); }

bool mqttUpdateShutdownComplete() {
  return updateShutdown.complete(mqttTcpActive());
}

void mqttInit() {
  if (initialized) return;
  initialized = true;
  wifiAvailable = WiFi.isConnected();
  const MqttSettings& settings = mqttConfig();
  if (!settings.enabled || !settings.host[0] || !mqttValidSettings(settings) ||
      !mqttEffectiveRoot(effectiveRoot, sizeof(effectiveRoot))) return;
  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, nullptr,
      [](TimerHandle_t) { connectToMqtt(); });
  if (!mqttReconnectTimer) return;
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onMessage(onMqttMessage);
  if (settings.username[0]) mqttClient.setCredentials(settings.username, settings.password);
  mqttClient.setServer(settings.host, settings.port);
  runtimeEnabled = true;
  Serial.printf("##[BOOT]# MQTT ON: %s:%u root=%s\n", settings.host,
      static_cast<unsigned>(settings.port), effectiveRoot);
  connectToMqtt();
}

void mqttWifiConnected() {
  wifiAvailable = true;
  if (!updateShutdown.reconnectAllowed()) return;
  if (!initialized) mqttInit();
  if (!runtimeEnabled) return;
  if (mqttClient.connected()) {
    if (publishedPlaylistIp != WiFi.localIP()) mqttPublishPlaylist();
  } else {
    connectToMqtt();
  }
}

void mqttWifiDisconnected() {
  wifiAvailable = false;
  if (!runtimeEnabled || updateShutdown.requested()) return;
  if (mqttReconnectTimer) xTimerStop(mqttReconnectTimer, 0);
  mqttClient.disconnect(true);
}

void zeroBuffer() {
  memset(topic, 0, sizeof(topic));
  memset(status, 0, sizeof(status));
}

void onMqttConnect(bool sessionPresent) {
  if (updateShutdown.requested()) {
    mqttClient.disconnect(true);
    return;
  }
  if (!runtimeEnabled) return;
  zeroBuffer();
  if (makeTopic("command")) mqttClient.subscribe(topic, 2);
  mqttPublishStatus();
  mqttPublishVolume();
  mqttPublishPlaylist();
}

void mqttPublishStatus() {
  if (!runtimeEnabled || !mqttClient.connected()) return;
  zeroBuffer();
  if (!makeTopic("status")) return;
  char name[BUFLEN / 2];
  char title[BUFLEN / 2];
  char interpreted[BUFLEN + 1];
  stationMetaDisplay(config.station.title, config.station.metadataMode == STATION_META_SWAP,
                     interpreted, sizeof(interpreted));
  config.escapeQuotes(config.station.name, name, sizeof(name) - 10);
  config.escapeQuotes(interpreted, title, sizeof(title) - 10);
  sprintf(status, "{\"status\": %d, \"station\": %d, \"name\": \"%s\", \"title\": \"%s\", \"on\": %d}",
      player.status() == PLAYING ? 1 : 0, config.lastStation(), name, title, config.store.dspon);
  mqttClient.publish(topic, 0, true, status);
}

void mqttPublishPlaylist() {
  if (!runtimeEnabled || !mqttClient.connected()) return;
  zeroBuffer();
  if (!makeTopic("playlist")) return;
  const IPAddress ip = WiFi.localIP();
  sprintf(status, "http://%s%s", config.ipToStr(ip), PLAYLIST_PATH);
  mqttClient.publish(topic, 0, true, status);
  publishedPlaylistIp = ip;
}

void mqttPublishVolume() {
  if (!runtimeEnabled || !mqttClient.connected()) return;
  zeroBuffer();
  if (!makeTopic("volume")) return;
  char volume[4];
  snprintf(volume, sizeof(volume), "%u", static_cast<unsigned>(mqttUserToWire(config.userVolume)));
  mqttClient.publish(topic, 0, true, volume);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  if (updateShutdown.reconnectAllowed() && runtimeEnabled && wifiAvailable &&
      WiFi.isConnected() && mqttReconnectTimer)
    xTimerStart(mqttReconnectTimer, 0);
}

void onMqttMessage(char* receivedTopic, char* payload, AsyncMqttClientMessageProperties properties,
                   size_t len, size_t index, size_t total) {
  if (!runtimeEnabled || updateLockActive() || len == 0 || index != 0 || len != total) return;
  if (len < 20) {
    char buf[20];
    memcpy(buf, payload, len);
    buf[len] = '\0';
    if (strcmp(buf, "volm") == 0) { player.stepUserVol(-1); return; }
    if (strcmp(buf, "volp") == 0) { player.stepUserVol(1); return; }
    if (cmd.exec(buf, "")) return;
    if (strcmp(buf, "turnoff") == 0) {
      uint8_t smartstart = config.store.smartstart;
      config.setDspOn(0);
      player.sendCommand({PR_STOP, 0});
      delay(100);
      config.saveValue(&config.store.smartstart, smartstart);
      return;
    }
    if (strcmp(buf, "turnon") == 0) {
      config.setDspOn(1);
      if (config.store.smartstart == 1) player.sendCommand({PR_PLAY, config.lastStation()});
      return;
    }
    int wire;
    if (mqttParseWireVolume(buf, wire)) {
      player.setUserVol(mqttWireToUser(wire));
      return;
    }
    int station;
    if (sscanf(buf, "play %d", &station) == 1) {
      if (station < 1) station = 1;
      uint16_t count = config.playlistLength();
      if (station >= count) station = count;
      player.sendCommand({PR_PLAY, static_cast<uint16_t>(station)});
      return;
    }
  } else {
    player.requestTemporaryUrl(payload, len);
  }
}
