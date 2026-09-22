#include "MqttService.h"

#include <esp_system.h>
#include <cerrno>
#include <cstdlib>
#include <WiFi.h>
#include "../diagnostics/Logger.h"
#include "../diagnostics/PerfDiagnostics.h"
#include "../core/DeviceIdentity.h"

MqttService* MqttService::_instance = nullptr;

namespace {
constexpr size_t MAX_MEDIA_PAYLOAD = 512;
const uint32_t RETRIES[] = {2000, 5000, 10000, 30000, 60000};

String suffixFor(const String& root, const char* suffix) {
    return root + "/" + suffix;
}

String escapeJson(const String& input) {
    String result;
    result.reserve(input.length() + 8);
    for (size_t i = 0; i < input.length(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(input[i]);
        if (ch == 34 || ch == 92) {
            result += char(92);
            result += static_cast<char>(ch);
        } else if (ch < 32) {
            char escaped[7];
            snprintf(escaped, sizeof(escaped), R"(\u%04x)", ch);
            result += escaped;
        } else result += static_cast<char>(ch);
    }
    return result;
}

bool parseNumber(const String& text, long& value, bool decimal = false) {
    if (text.isEmpty() || text.length() > 16) return false;
    errno = 0;
    char* end = nullptr;
    value = strtol(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str()) return false;
    if (decimal && *end == '.') {
        ++end;
        if (*end < '0' || *end > '9') return false;
        while (*end >= '0' && *end <= '9') ++end;
    }
    return *end == 0;
}

}

MqttService::MqttService() : _client(_network) {}

void MqttService::begin(const RuntimeConfig& config, Handlers handlers) {
    _config = &config;
    _handlers = std::move(handlers);
    _root = makeRoot(config);
    _clientId = String("voxone-") + DeviceIdentity::mac6Upper();
    _client.setServer(config.mqtt.host.c_str(), config.mqtt.port);
    _client.setCallback(onMessage);
    if (!_client.setBufferSize(1024))
        Logger::error("MQTT", "packet buffer allocation failed");
    _instance = this;
    _nextConnectMs = 0;
    _nextPublishMs = 0;
    _playlistIp = "";
    _retryStep = 0;
    _lastStatus = "";
    _lastWireVolume = -1;
    Logger::info("MQTT", config.features.mqttEnabled
        ? String("ON host=") + config.mqtt.host + " port=" + config.mqtt.port +
          " root=" + _root
        : "OFF");
}

String MqttService::makeRoot(const RuntimeConfig& config) const {
    if (!config.mqtt.rootTopic.isEmpty()) return config.mqtt.rootTopic;
    return "voxone-" + DeviceIdentity::mac6Upper();
}

String MqttService::topic(const char* suffix) const {
    return suffixFor(_root, suffix);
}

void MqttService::scheduleRetry() {
    _nextConnectMs = millis() + RETRIES[_retryStep < 5 ? _retryStep : 4];
    if (_retryStep < 5) ++_retryStep;
}

bool MqttService::connectNow() {
    if (!_config || !_config->features.mqttEnabled ||
        _config->mqtt.host.isEmpty() || WiFi.status() != WL_CONNECTED)
        return false;
    if (!_client.connect(_clientId.c_str(),
                         _config->mqtt.username.c_str(),
                         _config->mqtt.password.c_str())) {
        Logger::info("MQTT", String("connect failed state=") + _client.state());
        scheduleRetry();
        return false;
    }
    _retryStep = 0;
    _nextConnectMs = 0;
    Logger::info("MQTT", "connected root=" + _root);
    const String commandTopic = topic("command");
    if (!_client.subscribe(commandTopic.c_str(), 1)) {
        Logger::warn("MQTT", "subscribe failed: " + commandTopic);
        _client.disconnect();
        scheduleRetry();
        return false;
    }
    Logger::info("MQTT", "subscribed yoRadio: " + commandTopic);
    _lastStatus = "";
    _lastWireVolume = -1;
    _playlistIp = "";
    publishYoRadio(true);
    return true;
}

bool MqttService::publishChecked(const String& topicName, const char* value) {
    PerfScope perf(PerfArea::MqttPublish);
    if (_client.publish(topicName.c_str(), value, true)) return true;
    Logger::warn("MQTT", "publish failed: " + topicName);
    return false;
}

String MqttService::makeStatus(const DeviceState& state) const {
    const bool playing = state.playMediaActive ||
        (state.audioSource == AudioSource::Radio ?
        state.playback == PlaybackState::Playing :
        state.audioSource == AudioSource::Bluetooth && state.bluetoothPlaying);
    const uint16_t stationId = state.playMediaActive ? 0 :
        (_handlers.stationId ? _handlers.stationId() : 0);
    const bool powerOn = _handlers.powerOn && _handlers.powerOn();
    const String stationName = _handlers.stationName ?
        _handlers.stationName(stationId) : String();
    const String name = state.playMediaActive ? String("Player Media") :
        state.audioSource == AudioSource::Bluetooth ?
        state.bluetoothPeerName :
        state.radioStation.isEmpty() ? stationName : state.radioStation;
    const String artist = state.playMediaActive ? String() :
        state.audioSource == AudioSource::Bluetooth ?
        state.bluetoothArtist : state.radioArtist;
    const String title = state.playMediaActive ? String() :
        state.audioSource == AudioSource::Bluetooth ?
        state.bluetoothTitle : state.radioTitle;
    const String metadata = artist.isEmpty() ? title :
        artist + " - " + title;
    return String(R"({"status":)") + (playing ? 1 : 0) +
        R"(,"station":)" + stationId +
        R"(,"name":")" + escapeJson(name) +
        R"(","title":")" + escapeJson(metadata) +
        R"(","on":)" + (powerOn ? 1 : 0) + "}";
}

bool MqttService::publishStatus(const DeviceState& state, bool force) {
    const String status = makeStatus(state);
    if (!force && status == _lastStatus) return true;
    if (!publishChecked(topic("status"), status.c_str())) return false;
    _lastStatus = status;
    return true;
}

bool MqttService::publishVolume(const DeviceState& state, bool force) {
    const int volume = constrain(state.volume, 0, 100);
    const int wireVolume = (volume * 254 + 50) / 100;
    if (!force && wireVolume == _lastWireVolume) return true;
    const String payload(wireVolume);
    if (!publishChecked(topic("volume"), payload.c_str())) return false;
    _lastWireVolume = wireVolume;
    return true;
}

bool MqttService::publishPlaylist(bool force) {
    const String ip = WiFi.localIP().toString();
    if (!force && ip == _playlistIp) return true;
    const String url = "http://" + ip + "/data/playlist.csv";
    if (!publishChecked(topic("playlist"), url.c_str())) return false;
    _playlistIp = ip;
    Logger::info("MQTT", "yoRadio playlist=" + url);
    return true;
}

void MqttService::publishYoRadio(bool force) {
    if (!_client.connected()) return;
    const DeviceState state = StateStore::instance().snapshot();
    bool ok = publishStatus(state, force);
    ok = publishVolume(state, force) && ok;
    ok = publishPlaylist(force) && ok;
    if (!ok) _nextPublishMs = millis() + 2000;
    else {
        _nextPublishMs = 0;
        if (force) Logger::info("MQTT", "yoRadio state published");
    }
}

void MqttService::dispatchLegacy(const String& payload) {
    const bool mediaUrl = payload.startsWith("http://") ||
        payload.startsWith("https://");
    Logger::info("MQTT", mediaUrl ? "RX yoRadio command=play_media URL" :
        "RX yoRadio command=" + payload);
    if (payload == "prev") { if (_handlers.previous) _handlers.previous(); return; }
    if (payload == "next") { if (_handlers.next) _handlers.next(); return; }
    if (payload == "toggle") { if (_handlers.toggle) _handlers.toggle(); return; }
    if (payload == "stop") { if (_handlers.stop) _handlers.stop(); return; }
    if (payload == "start") { if (_handlers.start) _handlers.start(); return; }
    if (payload == "turnoff") { if (_handlers.turnOff) _handlers.turnOff(); return; }
    if (payload == "turnon") { if (_handlers.turnOn) _handlers.turnOn(); return; }
    if (payload == "volm" || payload == "volp") {
        if (_handlers.volume) _handlers.volume(StateStore::instance().snapshot().volume +
            (payload == "volp" ? 1 : -1));
        return;
    }
    if (payload == "play") {
        if (_handlers.station) _handlers.station(1);
        return;
    }
    if (payload.startsWith("vol ")) {
        long wire = 0;
        if (!parseNumber(payload.substring(4), wire, true)) return;
        wire = constrain(wire, 0L, 254L);
        if (_handlers.volume) _handlers.volume((wire * 100 + 127) / 254);
        return;
    }
    if (payload.startsWith("play ")) {
        long id = 0;
        if (!parseNumber(payload.substring(5), id) || id < 1 || id > 65535) return;
        if (_handlers.station && !_handlers.station(static_cast<uint16_t>(id)))
            Logger::warn("MQTT", String("yoRadio invalid station id=") + id);
        return;
    }
    if (mediaUrl) {
        if (!_handlers.media || !_handlers.media(payload))
            Logger::warn("MQTT", "yoRadio play_media URL rejected");
    }
}

void MqttService::onMessage(char* topicName, uint8_t* payload, unsigned int length) {
    if (!_instance || length == 0) return;
    String topicValue(topicName);
    if (topicValue == _instance->topic("command")) {
        if (length > MAX_MEDIA_PAYLOAD) {
            Logger::warn("MQTT", "yoRadio command payload too long");
            return;
        }
        String value;
        value.reserve(length);
        for (unsigned int i = 0; i < length; ++i) {
            if (payload[i] < 32 || payload[i] > 126) return;
            value += static_cast<char>(payload[i]);
        }
        _instance->dispatchLegacy(value);
    }
}

void MqttService::loop() {
    PerfScope perf(PerfArea::MqttLoop);
    if (!_config || !_config->features.mqttEnabled) return;
    if (WiFi.status() != WL_CONNECTED) {
        if (_client.connected()) {
            _client.disconnect();
        }
        return;
    }
    if (!_client.connected()) {
        if (static_cast<int32_t>(millis() - _nextConnectMs) >= 0) connectNow();
        return;
    }
    if (!_client.loop()) return;
    if (static_cast<int32_t>(millis() - _nextPublishMs) >= 0)
        publishYoRadio(false);
}
