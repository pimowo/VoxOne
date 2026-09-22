#include "WebService.h"

#include <cerrno>
#include <cstdlib>
#include <utility>
#include <esp_system.h>

#include "AppConfig.h"
#include "../audio/AudioOutputManager.h"
#include "../ui/DisplayService.h"
#include "../config/ConfigManager.h"
#include "../core/CommandQueue.h"
#include "../diagnostics/Logger.h"
#include "../diagnostics/PerfDiagnostics.h"
#include "WebConfigPage.h"
#include "WiFiService.h"
#include "../core/DeviceIdentity.h"
#include "../radio/StationStore.h"

namespace {
String stationJsonQuote(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    out += char(34);
    for (size_t i = 0; i < value.length(); ++i) {
        const uint8_t ch = static_cast<uint8_t>(value[i]);
        if (ch == 34 || ch == 92) out += char(92);
        if (ch >= 32) out += char(ch);
    }
    out += char(34);
    return out;
}

bool parseBoundedNumber(const String& raw, long minimum, long maximum,
                        long& value) {
    if (raw.isEmpty() || raw.length() > 10) return false;
    errno = 0;
    char* end = nullptr;
    value = strtol(raw.c_str(), &end, 10);
    return errno != ERANGE && end != raw.c_str() && *end == 0 &&
        value >= minimum && value <= maximum;
}
}

WebService::WebService() : _server(AppConfig::HTTP_PORT) {}

void WebService::begin(ConfigManager& config, WiFiService& wifi,
                       AudioOutputManager& audioOutput,
                       DisplayService* display, StationStore& stations,
                       StationHandlers stationHandlers) {
    _config = &config;
    _wifi = &wifi;
    _audioOutput = &audioOutput;
    _display = display;
    _stations = &stations;
    _stationHandlers = std::move(stationHandlers);
    char token[25];
    snprintf(token, sizeof(token), "%08lx%08lx%08lx",
             static_cast<unsigned long>(esp_random()),
             static_cast<unsigned long>(esp_random()),
             static_cast<unsigned long>(esp_random()));
    _token = token;
    routes();
    _server.begin();
    Logger::info("WEB", "HTTP server started");
}

void WebService::prepareDisplayDisable(const RuntimeConfig& candidate) {
    const RuntimeConfig& current = _config->config();
    if (!current.features.displayEnabled || candidate.features.displayEnabled ||
        current.display.type != DisplayType::ST7789 || _display == nullptr ||
        !_display->isInitialized()) {
        return;
    }
    if (!_display->clearToBlack()) {
        Logger::warn("DISPLAY", "Could not clear ST7789 before disable");
    }
}

void WebService::routes() {
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/data/playlist.csv", HTTP_GET,
               [this]() { handleYoRadioPlaylist(); });
    _server.on("/assets/voxone.css", HTTP_GET, [this]() {
        _server.send_P(200, "text/css; charset=utf-8", WebConfigPage::css());
    });
    _server.on("/api/v1/status", HTTP_GET, [this]() { handleStatus(); });
    _server.on("/api/v1/config", HTTP_GET, [this]() { handleConfigGet(); });
    _server.on("/api/v1/config", HTTP_POST, [this]() { handleConfigSave(); });
    _server.on("/api/v1/config/reset", HTTP_POST,
               [this]() { handleResetDefaults(); });
    _server.on("/api/v1/stations", HTTP_GET,
               [this]() { handleStationsGet(); });
    _server.on("/api/v1/stations", HTTP_POST,
               [this]() { handleStationAdd(); });
    _server.on("/api/v1/stations/update", HTTP_POST,
               [this]() { handleStationUpdate(); });
    _server.on("/api/v1/stations/delete", HTTP_POST,
               [this]() { handleStationDelete(); });
    _server.on("/api/v1/stations/play", HTTP_POST,
               [this]() { handleStationPlay(); });
    _server.on("/api/v1/stations/move-up", HTTP_POST,
               [this]() { handleStationMove(true); });
    _server.on("/api/v1/stations/move-down", HTTP_POST,
               [this]() { handleStationMove(false); });
    _server.on("/api/v1/stations/default", HTTP_POST,
               [this]() { handleStationDefault(); });

    _server.on("/wifi/save", HTTP_POST, [this]() { handleSaveWifi(); });
    _server.on("/wifi/clear", HTTP_POST, [this]() { handleClearWifi(); });
    _server.on("/reboot", HTTP_POST, [this]() { handleReboot(); });

    _server.onNotFound([this]() {
        _server.send(404, "text/plain; charset=utf-8", "Not found");
    });
}

void WebService::handleRoot() {
    _server.send_P(200, "text/html; charset=utf-8", WebConfigPage::html());
}

void WebService::handleYoRadioPlaylist() {
    String body;
    if (!_stations) {
        _server.send(503, "text/plain; charset=utf-8", "StationStore unavailable");
        return;
    }
    for (size_t index = 0; index < _stations->count(); ++index) {
        const Station* station = _stations->at(index);
        if (!station || station->name.isEmpty() || station->url.isEmpty()) continue;

        bool valid = true;
        for (const String* field : {&station->name, &station->url}) {
            for (size_t i = 0; i < field->length(); ++i) {
                const char ch = (*field)[i];
                if (ch == '\t' || ch == '\r' || ch == '\n') {
                    valid = false;
                    break;
                }
            }
            if (!valid) break;
        }
        if (!valid) continue;

        // yoRadio calls this file CSV, but its parser is strictly TSV:
        // name<TAB>URL<TAB>ovol. Commas and quotes are therefore literal data;
        // tabs/newlines are rejected because quoting is not supported upstream.
        body += station->name + char(9) + station->url + char(9) +
                String(station->volumeTrim) + char(10);
    }
    _server.sendHeader("Cache-Control", "no-store");
    _server.send(200, "text/csv; charset=utf-8", body);
}

void WebService::handleStationsGet() {
    if (!_stations) {
        sendJson(503, R"({"ok":false,"error":"StationStore unavailable."})");
        return;
    }
    String json;
    json.reserve(256 + _stations->count() * 320);
    json = R"({"ok":true,"maxStations":)";
    json += StationStore::MAX_STATIONS;
    json += R"(,"defaultStationId":)";
    json += _config->config().radio.defaultStation;
    json += R"(,"stations":[)";
    for (size_t i = 0; i < _stations->count(); ++i) {
        const Station* station = _stations->at(i);
        if (i) json += ',';
        json += R"({"position":)";
        json += i + 1;
        json += R"(,"id":)";
        json += station->id;
        json += R"(,"name":)";
        json += stationJsonQuote(station->name);
        json += R"(,"url":)";
        json += stationJsonQuote(station->url);
        json += R"(,"volumeTrim":)";
        json += String(station->volumeTrim);
        json += R"(,"isDefault":)";
        json += _config->config().radio.defaultStation == station->id
            ? "true}" : "false}";
    }
    json += "]}";
    sendJson(200, json);
}

void WebService::handleStationAdd() {
    if (!authorizeAction()) return;
    if (!_stations || !_server.hasArg("name") || !_server.hasArg("url") ||
        !_server.hasArg("volumeTrim")) {
        sendJson(400, R"({"ok":false,"error":"Brak wymaganych pol stacji."})");
        return;
    }
    String name = _server.arg("name");
    String url = _server.arg("url");
    name.trim();
    url.trim();
    long trim = 0;
    String error;
    if (!parseBoundedNumber(_server.arg("volumeTrim"),
            StationStore::MIN_VOLUME_TRIM, StationStore::MAX_VOLUME_TRIM, trim) ||
        !StationStore::validateFields(name, url, trim, &error)) {
        sendJson(400, String(R"({"ok":false,"error":)") +
            stationJsonQuote(error.isEmpty() ? "Niepoprawne dane stacji." : error) + "}");
        return;
    }
    if (_stations->count() >= StationStore::MAX_STATIONS) {
        sendJson(409, R"({"ok":false,"error":"Osiagnieto limit stacji."})");
        return;
    }
    uint16_t id = 0;
    if (!_stations->add(name, url, trim, id)) {
        sendJson(500, R"({"ok":false,"error":"Zapis stacji w NVS nie powiodl sie."})");
        return;
    }
    if (_config->config().radio.defaultStation == 0 &&
        !_config->saveDefaultStation(id)) {
        sendJson(500, R"({"ok":false,"error":"Stacja zapisana, ale zapis domyslnej stacji nie powiodl sie."})");
        return;
    }
    sendJson(201, String(R"({"ok":true,"id":)") + id + "}");
}

void WebService::handleStationUpdate() {
    if (!authorizeAction()) return;
    long rawId = 0, trim = 0;
    if (!_stations || !parseBoundedNumber(_server.arg("id"), 1, 65535, rawId)) {
        sendJson(400, R"({"ok":false,"error":"Niepoprawne ID stacji."})");
        return;
    }
    const uint16_t id = static_cast<uint16_t>(rawId);
    if (!_stations->isValidId(id)) {
        sendJson(404, R"({"ok":false,"error":"Stacja nie istnieje."})");
        return;
    }
    String name = _server.arg("name");
    String url = _server.arg("url");
    name.trim();
    url.trim();
    String error;
    if (!parseBoundedNumber(_server.arg("volumeTrim"),
            StationStore::MIN_VOLUME_TRIM, StationStore::MAX_VOLUME_TRIM, trim) ||
        !StationStore::validateFields(name, url, trim, &error)) {
        sendJson(400, String(R"({"ok":false,"error":)") +
            stationJsonQuote(error.isEmpty() ? "Niepoprawne dane stacji." : error) + "}");
        return;
    }
    if (!_stations->update(id, name, url, trim)) {
        sendJson(500, R"({"ok":false,"error":"Zapis stacji w NVS nie powiodl sie."})");
        return;
    }
    sendJson(200, R"({"ok":true})");
}

void WebService::handleStationDelete() {
    if (!authorizeAction()) return;
    long rawId = 0;
    if (!_stations || !parseBoundedNumber(_server.arg("id"), 1, 65535, rawId)) {
        sendJson(400, R"({"ok":false,"error":"Niepoprawne ID stacji."})");
        return;
    }
    const uint16_t id = static_cast<uint16_t>(rawId);
    if (!_stations->isValidId(id)) {
        sendJson(404, R"({"ok":false,"error":"Stacja nie istnieje."})");
        return;
    }
    const bool wasDefault = _config->config().radio.defaultStation == id;
    if (!_stations->remove(id)) {
        sendJson(500, R"({"ok":false,"error":"Usuniecie stacji z NVS nie powiodlo sie."})");
        return;
    }
    if (_stationHandlers.removed) _stationHandlers.removed(id);
    if (wasDefault) {
        const Station* first = _stations->first();
        if (!_config->saveDefaultStation(first ? first->id : 0)) {
            sendJson(500, R"({"ok":false,"error":"Stacja usunieta, ale aktualizacja domyslnej stacji nie powiodla sie."})");
            return;
        }
    }
    sendJson(200, R"({"ok":true})");
}

void WebService::handleStationPlay() {
    if (!authorizeAction()) return;
    long rawId = 0;
    if (!_stations || !parseBoundedNumber(_server.arg("id"), 1, 65535, rawId)) {
        sendJson(400, R"({"ok":false,"error":"Niepoprawne ID stacji."})");
        return;
    }
    const uint16_t id = static_cast<uint16_t>(rawId);
    if (!_stations->isValidId(id)) {
        sendJson(404, R"({"ok":false,"error":"Stacja nie istnieje."})");
        return;
    }
    if (!_stationHandlers.play || !_stationHandlers.play(id)) {
        sendJson(409, R"({"ok":false,"error":"Radio nie moze teraz uruchomic stacji."})");
        return;
    }
    sendJson(200, R"({"ok":true})");
}

void WebService::handleStationMove(bool up) {
    if (!authorizeAction()) return;
    long rawId = 0;
    if (!_stations || !parseBoundedNumber(_server.arg("id"), 1, 65535, rawId)) {
        sendJson(400, R"({"ok":false,"error":"Niepoprawne ID stacji."})");
        return;
    }
    const uint16_t id = static_cast<uint16_t>(rawId);
    if (!_stations->isValidId(id)) {
        sendJson(404, R"({"ok":false,"error":"Stacja nie istnieje."})");
        return;
    }
    if (!(up ? _stations->moveUp(id) : _stations->moveDown(id))) {
        sendJson(409, R"({"ok":false,"error":"Stacji nie mozna przesunac w tym kierunku."})");
        return;
    }
    sendJson(200, R"({"ok":true})");
}

void WebService::handleStationDefault() {
    if (!authorizeAction()) return;
    long rawId = 0;
    if (!_stations || !parseBoundedNumber(_server.arg("id"), 1, 65535, rawId)) {
        sendJson(400, R"({"ok":false,"error":"Niepoprawne ID stacji."})");
        return;
    }
    const uint16_t id = static_cast<uint16_t>(rawId);
    if (!_stations->isValidId(id)) {
        sendJson(404, R"({"ok":false,"error":"Stacja nie istnieje."})");
        return;
    }
    if (!_config->saveDefaultStation(id)) {
        sendJson(500, R"({"ok":false,"error":"Zapis domyslnej stacji nie powiodl sie."})");
        return;
    }
    Logger::info("STATIONS", String("default id=") + id);
    sendJson(200, R"({"ok":true})");
}

void WebService::sendJson(int status, const String& body) {
    _server.sendHeader("Cache-Control", "no-store");
    _server.send(status, "application/json; charset=utf-8", body);
}

bool WebService::authorizeAction() {
    if (_restartPending) {
        sendJson(409, "{\"ok\":false,\"error\":\"Restart został już zaplanowany.\"}");
        return false;
    }
    if (_token.isEmpty() || !_server.hasArg("_token") ||
        _server.arg("_token") != _token) {
        sendJson(403, "{\"ok\":false,\"error\":\"Brak ważnego tokena formularza.\"}");
        return false;
    }
    return true;
}

void WebService::scheduleRestart() {
    _restartPending = true;
    _restartDeadline = millis() + 1000;
}

void WebService::handleSaveWifi() {
    if (!authorizeAction()) return;
    RuntimeConfig candidate = _config->config();
    if (!_server.hasArg("ssid") || !_server.hasArg("password")) {
        sendJson(400, "{\"ok\":false,\"error\":\"Brak pól Wi-Fi.\"}");
        return;
    }
    auto& first = candidate.network.profiles[0];
    first.ssid = _server.arg("ssid");
    first.ssid.trim();
    first.enabled = true;
    if (first.ssid.isEmpty()) {
        sendJson(400, "{\"ok\":false,\"error\":\"SSID jest wymagane.\"}");
        return;
    }
    if (!_server.arg("password").isEmpty())
        first.password = _server.arg("password");
    candidate.network.wifiSsid = first.ssid;
    candidate.network.wifiPassword = first.password;
    if (!_config->validate(candidate)) {
        sendJson(400, "{\"ok\":false,\"error\":\"Niepoprawna konfiguracja Wi-Fi.\"}");
        return;
    }
    if (!_config->save(candidate)) {
        sendJson(500, "{\"ok\":false,\"error\":\"Zapis NVS nie powiódł się.\"}");
        return;
    }
    sendJson(200, "{\"ok\":true,\"message\":\"Wi-Fi zapisane. VoxOne uruchomi się ponownie.\"}");
    scheduleRestart();
}

void WebService::handleClearWifi() {
    if (!authorizeAction()) return;
    if (_server.arg("confirm") != "YES") {
        sendJson(400, "{\"ok\":false,\"error\":\"Potwierdzenie jest wymagane.\"}");
        return;
    }
    RuntimeConfig candidate = _config->config();
    candidate.network.wifiSsid = "";
    candidate.network.wifiPassword = "";
    for (auto& profile : candidate.network.profiles) {
        profile.ssid = "";
        profile.password = "";
        profile.enabled = false;
    }
    candidate.network.lastGoodIndex = -1;
    if (!_config->validate(candidate)) {
        sendJson(400, "{\"ok\":false,\"error\":\"Niepoprawna konfiguracja.\"}");
        return;
    }
    if (!_config->save(candidate)) {
        sendJson(500, "{\"ok\":false,\"error\":\"Zapis NVS nie powiódł się.\"}");
        return;
    }
    sendJson(200, "{\"ok\":true,\"message\":\"Wi-Fi usunięte. VoxOne uruchomi się ponownie.\"}");
    scheduleRestart();
}

void WebService::handlePlay() {
    if (!authorizeAction()) return;
    CommandQueue::instance().push({CommandType::SetPlay, CommandSource::Web, 0});
    sendJson(200, "{\"ok\":true}");
}

void WebService::handleStop() {
    if (!authorizeAction()) return;
    CommandQueue::instance().push({CommandType::SetStop, CommandSource::Web, 0});
    sendJson(200, "{\"ok\":true}");
}

void WebService::handleVolume() {
    if (!authorizeAction()) return;
    const String raw = _server.arg("v");
    if (raw.isEmpty() || raw.length() > 3) {
        sendJson(400, "{\"ok\":false,\"error\":\"Niepoprawna głośność.\"}");
        return;
    }
    errno = 0;
    char* end = nullptr;
    const long value = strtol(raw.c_str(), &end, 10);
    if (errno == ERANGE || end == raw.c_str() || *end != '\0' ||
        value < 0 || value > _config->maxVolume()) {
        sendJson(400, "{\"ok\":false,\"error\":\"Niepoprawna głośność.\"}");
        return;
    }
    CommandQueue::instance().push({
        CommandType::SetVolumeAbsolute, CommandSource::Web,
        static_cast<int>(value)
    });
    sendJson(200, "{\"ok\":true}");
}

void WebService::handleReboot() {
    if (!authorizeAction()) return;
    if (_server.arg("confirm") != "YES") {
        sendJson(400, "{\"ok\":false,\"error\":\"Potwierdzenie jest wymagane.\"}");
        return;
    }
    sendJson(200, "{\"ok\":true,\"message\":\"VoxOne uruchomi się ponownie.\"}");
    scheduleRestart();
}

void WebService::loop() {
    PerfScope perf(PerfArea::Web);
    _server.handleClient();
    if (_restartPending &&
        static_cast<int32_t>(millis() - _restartDeadline) >= 0) {
        ESP.restart();
    }
}
