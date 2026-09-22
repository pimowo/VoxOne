#include "WiFiService.h"
#include "AppConfig.h"
#include "../config/ConfigManager.h"
#include "../core/StateStore.h"
#include "../diagnostics/Logger.h"
#include "../core/DeviceIdentity.h"
#include <esp_wifi.h>

namespace {
constexpr uint32_t STA_SETTLE_MS = 100;
constexpr uint32_t RETRY_FAST_MS = 1000;
constexpr uint32_t RETRY_ASSOC_MS = 3000;
constexpr uint32_t RETRY_DEFAULT_MS = 5000;
constexpr uint32_t RETRY_NO_AP_MS = 8000;
constexpr uint32_t RADIO_RETRY_MAX_MS = 10000;
constexpr uint32_t RADIO_RETRY_FIRST_MS = 3000;
constexpr uint32_t RADIO_RETRY_SECOND_MS = 5000;
constexpr uint32_t RETRY_AUTH_MS = 15000;
constexpr uint32_t DHCP_RECHECK_MS = 2000;
constexpr uint32_t AP_FALLBACK_MS = 10000;
constexpr uint32_t AP_RETRY_MS = 5000;
constexpr uint32_t SCAN_COOLDOWN_MS = 30000;
}

WiFiService* WiFiService::_instance = nullptr;

String WiFiService::makeDeviceSuffix() const {
    uint64_t mac = ESP.getEfuseMac();
    char buf[7];
    snprintf(buf, sizeof(buf), "%06llX",
             static_cast<unsigned long long>(mac & 0xFFFFFFULL));
    return String(buf);
}

int WiFiService::bestProfile(uint8_t mask) const {
    const auto& network = _config->config().network;
    int best = -1;
    for (uint8_t i = 0; i < NetworkConfig::PROFILE_COUNT; ++i) {
        if (!(mask & (1u << i))) continue;
        if (best < 0 || network.profiles[i].priority > network.profiles[best].priority ||
            (network.profiles[i].priority == network.profiles[best].priority &&
             i == network.lastGoodIndex)) best = i;
    }
    return best;
}

void WiFiService::finishScan(uint32_t now) {
    const int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) return;
    _scanPending = false;
    _visibleProfiles = 0;
    if (count >= 0) {
        const auto& network = _config->config().network;
        for (int result = 0; result < count; ++result) {
            const String ssid = WiFi.SSID(result);
            for (uint8_t i = 0; i < NetworkConfig::PROFILE_COUNT; ++i) {
                if ((_enabledProfiles & (1u << i)) &&
                    network.profiles[i].ssid == ssid) _visibleProfiles |= 1u << i;
            }
        }
    } else Logger::warn("WIFI", "known-network scan failed");
    WiFi.scanDelete();
    const int next = bestProfile(_visibleProfiles & ~_triedProfiles);
    if (next >= 0) {
        _selectedProfile = next;
        _scanNeeded = false;
        _retryAt = now;
        Logger::info("WIFI", "next profile index=" + String(next));
    } else {
        _visibleProfiles = 0;
        _triedProfiles = 0;
        _scanNeeded = !_radioNetworkNeeded;
        _retryAt = _scanNeeded ? _nextScanAtMs : now;
    }
}

bool WiFiService::begin(ConfigManager& config) {
    _config = &config;
    _staUnavailableSince = millis();
    _hostname = String("voxone-") + DeviceIdentity::mac6Lower();

    WiFi.persistent(false);
    // The hostname must precede the first Wi-Fi mode/driver start.
    WiFi.setHostname(_hostname.c_str());
    _instance = this;
    WiFi.onEvent(onWiFiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    auto s = StateStore::instance().snapshot();
    s.hostname = _hostname;
    const auto& network = _config->config().network;
    s.wifiLastGoodProfile = network.lastGoodIndex;
    for (uint8_t i = 0; i < NetworkConfig::PROFILE_COUNT; ++i) {
        if (network.profiles[i].enabled && !network.profiles[i].ssid.isEmpty()) {
            _enabledProfiles |= 1u << i;
            ++s.wifiEnabledProfiles;
        }
    }
    const uint8_t last = network.lastGoodIndex;
    _selectedProfile = last < NetworkConfig::PROFILE_COUNT &&
        (_enabledProfiles & (1u << last)) ? last : bestProfile(_enabledProfiles);
    StateStore::instance().update(s);

    if (_enabledProfiles == 0) {
        // Provisioning is available at boot even without STA credentials.
        startAccessPoint();
    } else if (!WiFi.mode(WIFI_STA)) {
        _state = WiFiState::RetryWaiting;
        _retryAt = millis() + RETRY_DEFAULT_MS;
        Logger::warn("WIFI", "STA mode start failed; retry scheduled");
    }

    WiFi.setAutoReconnect(false);
    WiFi.setSleep(false);
    if (_enabledProfiles != 0 && _state == WiFiState::Idle) {
        _modeReadyAt = millis();
        _beginPending = true;
        Logger::info("WIFI", "STA mode ready; association scheduled");
    }
    return true;
}

void WiFiService::startAccessPoint() {
    if (StateStore::instance().snapshot().apMode) return;

    _lastApAttemptMs = millis();

    const bool modeReady = WiFi.mode(_enabledProfiles ? WIFI_AP_STA : WIFI_AP);
    if (!modeReady) Logger::warn("WIFI", "AP mode start failed");
    const String ap = String(AppConfig::DEVICE_PREFIX) + "-" + makeDeviceSuffix();
    bool ok = false;
    if (modeReady) {
        if (String(AppConfig::AP_PASSWORD).isEmpty()) {
            ok = WiFi.softAP(ap.c_str(), nullptr, AppConfig::AP_CHANNEL);
        } else {
            ok = WiFi.softAP(ap.c_str(), AppConfig::AP_PASSWORD,
                             AppConfig::AP_CHANNEL);
        }
    }

    auto s = StateStore::instance().snapshot();
    s.apMode = ok;
    s.apSsid = ap;
    s.apIp = ok ? WiFi.softAPIP().toString() : String();
    // Enabling fallback AP must not hide an STA address if it arrives late.
    StateStore::instance().update(s);

    if (ok)
        Logger::info("WIFI", "AP ready " + s.apIp + " (" + ap + ")");
    else
        Logger::warn("WIFI", "AP start failed");
}

void WiFiService::onWiFiEvent(arduino_event_id_t event, WiFiEventInfo_t info) {
    if (!_instance || event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;
    portENTER_CRITICAL(&_instance->_eventMux);
    _instance->_pendingDisconnect.reason = info.wifi_sta_disconnected.reason;
    _instance->_pendingDisconnect.atMs = millis();
    _instance->_pendingDisconnect.pending = true;
    portEXIT_CRITICAL(&_instance->_eventMux);
}

bool WiFiService::consumeConnectedTransition() {
    const bool pending = _connectedTransitionPending;
    _connectedTransitionPending = false;
    return pending;
}

void WiFiService::setRadioNetworkNeeded(bool needed) {
    if (_radioNetworkNeeded == needed) return;
    _radioNetworkNeeded = needed;
    if (!needed || _state != WiFiState::RetryWaiting) return;
    const uint32_t now = millis();
    // A source switch must not inherit a previously scheduled 60 s wait.
    if (static_cast<int32_t>(_retryAt - now) > static_cast<int32_t>(RADIO_RETRY_FIRST_MS))
        _retryAt = now + RADIO_RETRY_FIRST_MS;
    if (static_cast<int32_t>(_nextScanAtMs - now) >
        static_cast<int32_t>(RADIO_RETRY_MAX_MS))
        _nextScanAtMs = now + RADIO_RETRY_MAX_MS;
}

uint32_t WiFiService::retryDelayForReason(uint8_t reason) {
    if (reason == WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG &&
        _assocComebackFastRetries < 3) {
        ++_assocComebackFastRetries;
        return RETRY_FAST_MS;
    }
    if (reason != WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG)
        _assocComebackFastRetries = 0;

    if (reason == _lastFailureReason) {
        if (_consecutiveFailures < 6) ++_consecutiveFailures;
    } else {
        _lastFailureReason = reason;
        _consecutiveFailures = 1;
    }

    if (_radioNetworkNeeded &&
        reason == WIFI_REASON_NO_AP_FOUND) {
        if (_consecutiveFailures == 1) return RADIO_RETRY_FIRST_MS;
        if (_consecutiveFailures == 2) return RADIO_RETRY_SECOND_MS;
        return RADIO_RETRY_MAX_MS;
    }

    const uint32_t maxInterval = _radioNetworkNeeded
        ? RADIO_RETRY_MAX_MS : AppConfig::WIFI_RETRY_MAX_INTERVAL_MS;
    uint32_t delayMs = RETRY_DEFAULT_MS;
    switch (reason) {
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_ASSOC_EXPIRE:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            delayMs = RETRY_ASSOC_MS;
            break;
        case WIFI_REASON_NO_AP_FOUND:
            delayMs = RETRY_NO_AP_MS;
            break;
        case WIFI_REASON_AUTH_FAIL:
            delayMs = RETRY_AUTH_MS;
            break;
        default:
            break;
    }
    for (uint8_t attempt = 1; attempt < _consecutiveFailures &&
         delayMs < maxInterval; ++attempt) {
        delayMs = delayMs > maxInterval / 2
            ? maxInterval : delayMs * 2;
    }
    return delayMs > maxInterval ? maxInterval : delayMs;
}

void WiFiService::scheduleRetry(uint8_t reason, uint32_t now) {
    _retryDelayMs = retryDelayForReason(reason);
    _retryAt = now + _retryDelayMs;
    _state = WiFiState::RetryWaiting;
    if (_enabledProfiles != 0) {
        _triedProfiles |= 1u << _selectedProfile;
        if (_enabledProfiles & ~_triedProfiles) _scanNeeded = true;
        Logger::warn("WIFI", "profile failed index=" +
            String(_selectedProfile) + " reason=" + String(reason));
    }
    Logger::info("WIFI", "state RETRY_WAITING; retry scheduled in " +
        String(_retryDelayMs) + " ms");
}

void WiFiService::startAssociation(uint32_t now) {
    if (!_config || _enabledProfiles == 0) return;
    const wifi_mode_t mode =
        StateStore::instance().snapshot().apMode ? WIFI_AP_STA : WIFI_STA;
    if (WiFi.getMode() != mode && !WiFi.mode(mode)) {
        Logger::warn("WIFI", "STA mode unavailable");
        scheduleRetry(0, now);
        return;
    }

    _state = WiFiState::Connecting;
    _connectStarted = millis();
    const auto& profile = _config->config().network.profiles[_selectedProfile];
    Logger::info("WIFI", "trying profile=" + String(_selectedProfile) +
        " ssid=" + profile.ssid);
    WiFi.begin(profile.ssid.c_str(), profile.password.c_str());
}

void WiFiService::reconnect() {
    if (!_config) return;
    if (_enabledProfiles == 0) {
        startAccessPoint();
        return;
    }
    if (_state == WiFiState::Connecting) return;
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(false, false);
    _state = WiFiState::RetryWaiting;
    _retryAt = millis();
}

void WiFiService::refreshState(bool connected) {
    const uint32_t now = millis();
    auto s = StateStore::instance().snapshot();
    if (connected != _wasConnected) {
        _wasConnected = connected;
        if (connected) {
            Logger::info("WIFI", "STA connected");
        } else {
            _staUnavailableSince = now;
            s.ip = "";
            _lastMdnsAttemptMs = 0;
            if (_mdnsStarted) {
                MDNS.end();
                _mdnsStarted = false;
            }
        }
    }

    if (connected) {
        s.wifiConnected = true;
        s.wifiActiveProfile = _selectedProfile;
        s.wifiLastGoodProfile = _config->config().network.lastGoodIndex;
        s.wifiSsid = WiFi.SSID();
        s.ip = WiFi.localIP().toString();
        s.wifiRssi = WiFi.RSSI();
        if (!_mdnsStarted &&
            (_lastMdnsAttemptMs == 0 ||
             now - _lastMdnsAttemptMs >= AppConfig::WIFI_RETRY_INTERVAL_MS)) {
            _lastMdnsAttemptMs = now;
            if (MDNS.begin(_hostname.c_str())) {
                MDNS.addService("http", "tcp", AppConfig::HTTP_PORT);
                _mdnsStarted = true;
                Logger::info("MDNS", _hostname + ".local");
            } else {
                Logger::warn("MDNS", "MDNS.begin failed");
            }
        }
    } else {
        s.wifiConnected = false;
        s.wifiActiveProfile = -1;
        s.wifiSsid = "";
    }
    StateStore::instance().update(s);
}

void WiFiService::loop() {
    if (!_config) return;
    const uint32_t now = millis();
    DisconnectEvent event;
    portENTER_CRITICAL(&_eventMux);
    if (_pendingDisconnect.pending) {
        event = _pendingDisconnect;
        _pendingDisconnect.pending = false;
    }
    portEXIT_CRITICAL(&_eventMux);

    // A late IP always wins, including after a timeout or retry decision.
    const wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
        if (_scanPending) {
            esp_wifi_scan_stop();
            WiFi.scanDelete();
            _scanPending = false;
        }
        if (_state != WiFiState::Connected) {
            const bool late = _state == WiFiState::RetryWaiting ||
                _state == WiFiState::Error ||
                (_connectStarted != 0 &&
                 now - _connectStarted >= AppConfig::WIFI_SHORT_STARTUP_TIMEOUT_MS);
            _state = WiFiState::Connected;
            _beginPending = false;
            _retryDelayMs = 0;
            _lastFailureReason = 0;
            _consecutiveFailures = 0;
            _assocComebackFastRetries = 0;
            _connectedTransitionPending = true;
            _awaitingArduinoRetry = false;
            _driverRetryWaitLogged = false;
            _cancelingStaleRetry = false;
            _scanNeeded = false;
            _scanPending = false;
            _triedProfiles = 0;
            _visibleProfiles = 0;
            Logger::info("WIFI", "profile connected index=" + String(_selectedProfile));
            if (!_config->saveLastGoodProfile(_selectedProfile))
                Logger::warn("WIFI", "last good profile NVS update failed");
            if (late) Logger::info("WIFI", "late connect accepted");
        }
        if (!_wasConnected ||
            now - _lastStateRefreshMs >= 1000) {
            _lastStateRefreshMs = now;
            refreshState(true);
        }
        return;
    }

    if (event.pending && _state != WiFiState::Idle &&
        _state != WiFiState::Error) {
        Logger::warn("WIFI", "disconnected reason=" + String(event.reason) +
            " at=" + String(event.atMs));
        const bool firstDisconnect = !_firstDisconnectSeen;
        _firstDisconnectSeen = true;
        if (_awaitingArduinoRetry) {
            _awaitingArduinoRetry = false;
            _driverRetryWaitLogged = false;
            Logger::info("WIFI", "Arduino first reconnect ended");
        }
        if (_cancelingStaleRetry) _cancelingStaleRetry = false;
        // Arduino-ESP32 reconnects once after its first disconnect, even
        // with setAutoReconnect(false). Do not race that attempt after 208.
        if (firstDisconnect &&
            event.reason == WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG) {
            _awaitingArduinoRetry = true;
            _arduinoRetryUntilMs = now + AppConfig::WIFI_CONNECT_TIMEOUT_MS;
        }
        scheduleRetry(event.reason, now);
    } else if (_state == WiFiState::Connected) {
        Logger::warn("WIFI", "disconnected reason=unknown");
        scheduleRetry(0, now);
    }

    if (_wasConnected || now - _lastStateRefreshMs >= 1000) {
        _lastStateRefreshMs = now;
        refreshState(false);
    }

    if (!StateStore::instance().snapshot().apMode &&
        (_enabledProfiles == 0 ||
         now - _staUnavailableSince >= AP_FALLBACK_MS) &&
        (_lastApAttemptMs == 0 ||
         now - _lastApAttemptMs >= AP_RETRY_MS)) {
        Logger::warn("WIFI", "STA unavailable; starting fallback AP");
        startAccessPoint();
    }

    if (_scanPending) {
        finishScan(now);
        if (_scanPending) return;
    }

    if (_beginPending && now - _modeReadyAt >= STA_SETTLE_MS) {
        _beginPending = false;
        startAssociation(now);
        return;
    }

    if (_state == WiFiState::Connecting &&
        now - _connectStarted >= AppConfig::WIFI_CONNECT_TIMEOUT_MS) {
        Logger::warn("WIFI", "STA association timeout; radio stays on");
        scheduleRetry(0, now);
    }

    if (_state == WiFiState::RetryWaiting &&
        static_cast<int32_t>(now - _retryAt) >= 0) {
        // Link is up but DHCP has not supplied an IP yet; don't restart STA.
        if (status == WL_IDLE_STATUS) {
            _retryAt = now + DHCP_RECHECK_MS;
            return;
        }
        if (_awaitingArduinoRetry) {
            if (static_cast<int32_t>(now - _arduinoRetryUntilMs) < 0) {
                if (!_driverRetryWaitLogged) {
                    _driverRetryWaitLogged = true;
                    Logger::info("WIFI", "retry deferred: STA still associating");
                }
                _retryAt = now + 250;
                return;
            }
            // No completion event: explicitly stop the stale attempt before
            // another WiFi.begin(), without turning the radio off.
            const esp_err_t err = esp_wifi_disconnect();
            if (err != ESP_OK) {
                Logger::warn("WIFI", "stale STA cancel failed=" +
                    String(static_cast<int>(err)));
                _arduinoRetryUntilMs = now + RETRY_FAST_MS;
                _retryAt = _arduinoRetryUntilMs;
                return;
            }
            Logger::warn("WIFI", "stale STA attempt canceled before retry");
            _awaitingArduinoRetry = false;
            _driverRetryWaitLogged = false;
            _cancelingStaleRetry = true;
            _cancelSettleUntilMs = now + RETRY_FAST_MS;
            _retryAt = _cancelSettleUntilMs;
            return;
        }
        if (_cancelingStaleRetry) {
            if (static_cast<int32_t>(now - _cancelSettleUntilMs) < 0) {
                _retryAt = _cancelSettleUntilMs;
                return;
            }
            _cancelingStaleRetry = false;
        }
        if ((_enabledProfiles & ~_triedProfiles) == 0) {
            _triedProfiles = 0;
            _visibleProfiles = 0;
            _scanNeeded = (_enabledProfiles & (_enabledProfiles - 1)) != 0;
        }
        if (_enabledProfiles & ~_triedProfiles) {
            const int next = bestProfile(_visibleProfiles & ~_triedProfiles);
            if (next >= 0) {
                _selectedProfile = next;
                _scanNeeded = false;
                Logger::info("WIFI", "next profile index=" + String(next));
            }
        }
        if (_scanNeeded && (_enabledProfiles & ~_triedProfiles)) {
            if (static_cast<int32_t>(now - _nextScanAtMs) < 0) {
                _retryAt = _nextScanAtMs;
                return;
            }
            Logger::info("WIFI", "scanning known networks");
            _nextScanAtMs = now + (_radioNetworkNeeded
                ? RADIO_RETRY_MAX_MS : SCAN_COOLDOWN_MS);
            if (WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING) {
                _scanPending = true;
                return;
            }
            _scanNeeded = false;
            Logger::warn("WIFI", "scan start failed; retrying current profile");
        }
        Logger::info("WIFI", "STA retry starting");
        startAssociation(now);
    }
}
