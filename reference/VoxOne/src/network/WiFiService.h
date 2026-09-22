#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

class ConfigManager;

enum class WiFiState : uint8_t {
    Idle, Connecting, Connected, RetryWaiting, Error
};

class WiFiService {
public:
    bool begin(ConfigManager& config);
    void loop();
    void reconnect();
    void setRadioNetworkNeeded(bool needed);
    void startAccessPoint();
    bool startupUnavailable() const { return _state == WiFiState::Error; }
    bool consumeConnectedTransition();

private:
    struct DisconnectEvent {
        bool pending = false;
        uint8_t reason = 0;
        uint32_t atMs = 0;
    };

    static WiFiService* _instance;
    static void onWiFiEvent(arduino_event_id_t event, WiFiEventInfo_t info);

    ConfigManager* _config = nullptr;
    WiFiState _state = WiFiState::Idle;
    bool _radioNetworkNeeded = false;
    bool _scanPending = false;
    bool _scanNeeded = false;
    bool _firstDisconnectSeen = false;
    bool _awaitingArduinoRetry = false;
    bool _driverRetryWaitLogged = false;
    bool _cancelingStaleRetry = false;
    uint8_t _selectedProfile = 0;
    uint8_t _triedProfiles = 0;
    uint8_t _visibleProfiles = 0;
    uint8_t _enabledProfiles = 0;
    bool _beginPending = false;
    bool _connectedTransitionPending = false;
    bool _mdnsStarted = false;
    bool _wasConnected = false;
    uint32_t _modeReadyAt = 0;
    uint32_t _staUnavailableSince = 0;
    uint32_t _lastApAttemptMs = 0;
    uint32_t _nextScanAtMs = 0;
    uint32_t _arduinoRetryUntilMs = 0;
    uint32_t _cancelSettleUntilMs = 0;
    uint32_t _connectStarted = 0;
    uint32_t _retryAt = 0;
    uint32_t _retryDelayMs = 0;
    uint32_t _lastStateRefreshMs = 0;
    uint32_t _lastMdnsAttemptMs = 0;
    uint8_t _lastFailureReason = 0;
    uint8_t _consecutiveFailures = 0;
    uint8_t _assocComebackFastRetries = 0;
    String _hostname;
    portMUX_TYPE _eventMux = portMUX_INITIALIZER_UNLOCKED;
    DisconnectEvent _pendingDisconnect;

    String makeDeviceSuffix() const;
    int bestProfile(uint8_t mask) const;
    void finishScan(uint32_t now);
    void startAssociation(uint32_t now);
    void scheduleRetry(uint8_t reason, uint32_t now);
    uint32_t retryDelayForReason(uint8_t reason);
    void refreshState(bool connected);
};
