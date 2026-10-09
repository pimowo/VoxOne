#ifndef mqtt_h
#define mqtt_h

#include "../async-mqtt-client/AsyncMqttClient.h"

void mqttInit();
bool mqttActive();
void connectToMqtt();
void mqttWifiConnected();
void mqttWifiDisconnected();
void mqttBeginUpdateShutdown();
bool mqttUpdateShutdownComplete();
bool mqttIsConnected();
bool mqttTcpActive();
void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
void mqttPublishStatus();
void mqttPublishPlaylist();
void mqttPublishVolume();
void zeroBuffer();

#endif
