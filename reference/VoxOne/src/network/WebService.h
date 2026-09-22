#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <functional>

class ConfigManager;
class WiFiService;
class AudioOutputManager;
class DisplayService;
class StationStore;
struct RuntimeConfig;

class WebService {
public:
    struct StationHandlers {
        std::function<bool(uint16_t)> play;
        std::function<void(uint16_t)> removed;
    };

    WebService();
    void begin(ConfigManager& config, WiFiService& wifi,
               AudioOutputManager& audioOutput, DisplayService* display,
               StationStore& stations, StationHandlers stationHandlers);
    void loop();
    bool restartPending() const { return _restartPending; }

private:
    WebServer _server;
    ConfigManager* _config = nullptr;
    WiFiService* _wifi = nullptr;
    AudioOutputManager* _audioOutput = nullptr;
    DisplayService* _display = nullptr;
    StationStore* _stations = nullptr;
    StationHandlers _stationHandlers;
    String _token;
    bool _restartPending = false;
    uint32_t _restartDeadline = 0;

    void routes();
    void handleRoot();
    void handleYoRadioPlaylist();
    void handleStationsGet();
    void handleStationAdd();
    void handleStationUpdate();
    void handleStationDelete();
    void handleStationPlay();
    void handleStationMove(bool up);
    void handleStationDefault();
    void handleStatus();
    void handleConfigGet();
    void handleConfigSave();
    void handleResetDefaults();
    void handleSaveWifi();
    void handleClearWifi();
    void handlePlay();
    void handleStop();
    void handleVolume();
    void handleReboot();
    void sendJson(int status, const String& body);
    bool authorizeAction();
    void scheduleRestart();
    void prepareDisplayDisable(const RuntimeConfig& candidate);
};
