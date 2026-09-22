#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <functional>

#include "../config/ConfigModel.h"
#include "../core/StateStore.h"

class MqttService {
public:
    using VolumeHandler = std::function<void(int)>;
    using ActionHandler = std::function<void()>;
    using StationHandler = std::function<bool(uint16_t)>;
    using StationIdHandler = std::function<uint16_t()>;
    using StationNameHandler = std::function<String(uint16_t)>;
    using PowerOnHandler = std::function<bool()>;
    using MediaHandler = std::function<bool(const String&)>;

    struct Handlers {
        VolumeHandler volume;
        ActionHandler toggle;
        ActionHandler next;
        ActionHandler previous;
        ActionHandler start;
        ActionHandler stop;
        ActionHandler turnOn;
        ActionHandler turnOff;
        StationHandler station;
        StationIdHandler stationId;
        StationNameHandler stationName;
        PowerOnHandler powerOn;
        MediaHandler media;
    };

    MqttService();
    void begin(const RuntimeConfig& config, Handlers handlers);
    void loop();

private:
    WiFiClient _network;
    PubSubClient _client;
    const RuntimeConfig* _config = nullptr;
    Handlers _handlers;
    String _root;
    String _clientId;
    String _playlistIp;
    uint32_t _nextConnectMs = 0;
    uint32_t _nextPublishMs = 0;
    uint8_t _retryStep = 0;
    String _lastStatus;
    int _lastWireVolume = -1;

    static MqttService* _instance;
    static void onMessage(char* topic, uint8_t* payload, unsigned int length);

    String topic(const char* suffix) const;
    String makeRoot(const RuntimeConfig& config) const;
    bool connectNow();
    void scheduleRetry();
    bool publishChecked(const String& topicName, const char* value);
    String makeStatus(const DeviceState& state) const;
    bool publishStatus(const DeviceState& state, bool force);
    bool publishVolume(const DeviceState& state, bool force);
    bool publishPlaylist(bool force);
    void publishYoRadio(bool force);
    void dispatchLegacy(const String& payload);
};
