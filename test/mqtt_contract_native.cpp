#include <cassert>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define options_h
#define config_h
#define mqtt_h
#define VOXONE_MQTT_CONFIG_H
#define VOXONE_STATION_METADATA_H
#define player_h
#define commandhandler_h
#define VOXONE_UPDATE_PROGRESS_H
#define VOXONE_MQTT_UPDATE_SHUTDOWN_STATE_H

#define BUFLEN 170
#define PLAYLIST_PATH "/data/stations.tsv"
#define pdFALSE 0
#define pdMS_TO_TICKS(value) (value)

#include "WiFi.h"
NativeWiFiMock WiFi;

using TimerHandle_t = void*;

template <typename Callback>
TimerHandle_t xTimerCreate(const char*, uint32_t, int, void*, Callback) {
  return reinterpret_cast<TimerHandle_t>(1);
}
inline void xTimerStop(TimerHandle_t, uint32_t) {}
inline void xTimerStart(TimerHandle_t, uint32_t) {}

struct NativeSerialMock {
  template <typename... Args>
  void printf(const char*, Args...) {}
};
NativeSerialMock Serial;

struct MqttSettings {
  uint8_t enabled = 1;
  char host[128] = "broker.local";
  uint16_t port = 1883;
  char username[64]{};
  char password[128]{};
  char rootTopic[64] = "voxone-test";
};

MqttSettings settings;
const MqttSettings& mqttConfig() { return settings; }
bool mqttValidSettings(const MqttSettings&) { return true; }
bool mqttEffectiveRoot(char* output, size_t capacity) {
  return std::snprintf(output, capacity, "%s", settings.rootTopic) > 0;
}
uint8_t mqttUserToWire(uint8_t value) { return value; }
uint8_t mqttWireToUser(int value) { return static_cast<uint8_t>(value); }
bool mqttParseWireVolume(const char*, int&) { return false; }

constexpr uint8_t STATION_META_SWAP = 1;
void stationMetaDisplay(const char* input, bool, char* output, size_t capacity) {
  std::snprintf(output, capacity, "%s", input);
}

struct ConfigStoreMock {
  uint8_t smartstart = 0;
  uint8_t dspon = 0;
};

struct StationMock {
  char name[64] = "Station";
  char title[64] = "Artist - Title";
  uint8_t metadataMode = 0;
};

struct ConfigMock {
  ConfigStoreMock store;
  StationMock station;
  uint8_t userVolume = 50;
  uint16_t stationId = 3;
  unsigned dspCalls = 0;
  int lastDspValue = -1;
  unsigned saveCalls = 0;
  uint8_t savedSmartstart = 0;

  uint16_t lastStation() const { return stationId; }
  uint16_t playlistLength() const { return 12; }
  const char* ipToStr(const IPAddress&) const { return "192.168.1.42"; }
  void escapeQuotes(const char* input, char* output, size_t capacity) const {
    size_t length = std::strlen(input);
    if (length > capacity) length = capacity;
    std::memcpy(output, input, length);
    output[length] = '\0';
  }
  void setDspOn(int value) {
    ++dspCalls;
    lastDspValue = value;
    store.dspon = static_cast<uint8_t>(value);
  }
  void saveValue(uint8_t* target, uint8_t value) {
    ++saveCalls;
    savedSmartstart = value;
    *target = value;
  }
};
ConfigMock config;

enum plStatus_e : uint8_t { PLAYING = 1, STOPPED = 2 };
enum playerRequestType_e : uint8_t { PR_PLAY = 1, PR_STOP = 2 };
struct playerRequestParams_t {
  playerRequestType_e type;
  int payload;
};

struct PlayerMock {
  plStatus_e currentStatus = STOPPED;
  std::vector<playerRequestParams_t> commands;
  plStatus_e status() const { return currentStatus; }
  void stepUserVol(int) {}
  void setUserVol(uint8_t) {}
  void sendCommand(playerRequestParams_t request) { commands.push_back(request); }
  void requestTemporaryUrl(const char*, size_t) {}
};
PlayerMock player;

struct CommandHandlerMock {
  bool exec(const char*, const char*) { return false; }
};
CommandHandlerMock cmd;

bool updateLockActive() { return false; }
unsigned delayedMs = 0;
void delay(unsigned value) { delayedMs = value; }

class MqttUpdateShutdownState {
 public:
  bool begin() {
    if (requested_) return false;
    requested_ = true;
    return true;
  }
  bool requested() const { return requested_; }
  bool reconnectAllowed() const { return !requested_; }
  bool complete(bool tcpActive) const { return requested_ && !tcpActive; }

 private:
  bool requested_ = false;
};

enum class AsyncMqttClientDisconnectReason : uint8_t { TcpDisconnected };
struct AsyncMqttClientMessageProperties {};

struct PublishedMessage {
  std::string topic;
  uint8_t qos;
  bool retained;
  std::string payload;
};

class AsyncMqttClient {
 public:
  template <typename Callback> void onConnect(Callback) {}
  template <typename Callback> void onDisconnect(Callback) {}
  template <typename Callback> void onMessage(Callback) {}
  void setCredentials(const char*, const char*) {}
  void setServer(const char*, uint16_t) {}
  void connect() { connected_ = true; }
  void disconnect(bool) { connected_ = false; }
  bool connected() const { return connected_; }
  bool tcpActive() const { return connected_; }
  uint16_t subscribe(const char*, uint8_t) { return 1; }
  uint16_t publish(const char* publishedTopic, uint8_t qos, bool retained,
                   const char* payload) {
    published.push_back({publishedTopic, qos, retained, payload});
    return 1;
  }

  bool connected_ = false;
  std::vector<PublishedMessage> published;
};

void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttMessage(char* topic, char* payload,
                   AsyncMqttClientMessageProperties properties, size_t len,
                   size_t index, size_t total);
void mqttPublishStatus();
void mqttPublishPlaylist();
void mqttPublishVolume();

#include "../src/core/mqtt.cpp"

namespace {

const PublishedMessage& publishStatus(plStatus_e playerStatus, uint8_t dspOn) {
  player.currentStatus = playerStatus;
  config.store.dspon = dspOn;
  mqttClient.published.clear();
  mqttPublishStatus();
  assert(mqttClient.published.size() == 1);
  return mqttClient.published.front();
}

void testLegacyStatusJsonContract() {
  const PublishedMessage playingOff = publishStatus(PLAYING, 0);
  assert(playingOff.topic == "voxone-test/status");
  assert(playingOff.qos == 0 && playingOff.retained);
  assert(playingOff.payload ==
      "{\"status\": 1, \"station\": 3, \"name\": \"Station\", "
      "\"title\": \"Artist - Title\", \"on\": 0}");

  const PublishedMessage stoppedOn = publishStatus(STOPPED, 1);
  assert(stoppedOn.payload ==
      "{\"status\": 0, \"station\": 3, \"name\": \"Station\", "
      "\"title\": \"Artist - Title\", \"on\": 1}");

  assert(publishStatus(PLAYING, 1).payload.find("\"status\": 1") !=
         std::string::npos);
  assert(publishStatus(PLAYING, 1).payload.find("\"on\": 1") !=
         std::string::npos);
  assert(publishStatus(STOPPED, 0).payload.find("\"status\": 0") !=
         std::string::npos);
  assert(publishStatus(STOPPED, 0).payload.find("\"on\": 0") !=
         std::string::npos);
}

void sendMessage(const char* text) {
  char payload[20]{};
  const size_t length = std::strlen(text);
  assert(length < sizeof(payload));
  std::memcpy(payload, text, length);
  onMqttMessage(nullptr, payload, AsyncMqttClientMessageProperties{},
                length, 0, length);
}

void resetCommandState(uint8_t smartstart) {
  config.store.smartstart = smartstart;
  config.dspCalls = 0;
  config.lastDspValue = -1;
  config.saveCalls = 0;
  config.savedSmartstart = 0;
  player.commands.clear();
  delayedMs = 0;
}

void testLegacyTurnOff() {
  resetCommandState(1);
  sendMessage("turnoff");
  assert(config.dspCalls == 1 && config.lastDspValue == 0);
  assert(player.commands.size() == 1);
  assert(player.commands[0].type == PR_STOP && player.commands[0].payload == 0);
  assert(delayedMs == 100);
  assert(config.saveCalls == 1 && config.savedSmartstart == 1);
  assert(config.store.smartstart == 1);
}

void testLegacyTurnOn() {
  resetCommandState(1);
  sendMessage("turnon");
  assert(config.dspCalls == 1 && config.lastDspValue == 1);
  assert(player.commands.size() == 1);
  assert(player.commands[0].type == PR_PLAY && player.commands[0].payload == 3);

  resetCommandState(0);
  sendMessage("turnon");
  assert(config.dspCalls == 1 && config.lastDspValue == 1);
  assert(player.commands.empty());
}

}  // namespace

int main() {
  mqttInit();
  assert(mqttActive() && mqttClient.connected());
  testLegacyStatusJsonContract();
  testLegacyTurnOff();
  testLegacyTurnOn();
  return 0;
}
