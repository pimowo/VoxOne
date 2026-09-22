#include "RadioService.h"

#include <cstdlib>
#include <cstring>
#include <WiFi.h>
#include "../core/StateStore.h"
#include "../diagnostics/Logger.h"
#include "../diagnostics/PerfDiagnostics.h"

namespace {
constexpr size_t MAX_NETWORK_READ_BYTES = 4096;
constexpr size_t MAX_PCM_WRITE_BYTES = 4608;

bool parseUnsignedHeader(const String& text, uint32_t maximum, uint32_t& value) {
    const char* cursor = text.c_str();
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (*cursor < '0' || *cursor > '9') return false;
    uint32_t parsed = 0;
    do {
        const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
        if (parsed > (maximum - digit) / 10) return false;
        parsed = parsed * 10 + digit;
        ++cursor;
    } while (*cursor >= '0' && *cursor <= '9');
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (*cursor != '\0') return false;
    value = parsed;
    return true;
}

String displayCodec(const String& contentType) {
    String type = contentType;
    type.toLowerCase();
    const int parameters = type.indexOf(';');
    if (parameters >= 0) type.remove(parameters);
    type.trim();
    if (type == "audio/mpeg" || type == "audio/mp3" ||
        type == "audio/x-mpeg" || type == "audio/mpeg3" ||
        type == "audio/x-mpeg3" || type == "application/x-mpeg" ||
        type == "application/octet-stream")
        return "MP3";
    if (type.startsWith("audio/aac") || type.startsWith("audio/aacp") ||
        type.startsWith("audio/x-aac"))
        return "AAC";
    return String();
}

bool isMp3ContentType(const String& type) {
    return type == "audio/mpeg" || type == "audio/mp3" ||
        type == "audio/x-mpeg" || type == "audio/mpeg3" ||
        type == "audio/x-mpeg3" || type == "application/x-mpeg" ||
        type == "application/octet-stream";
}

bool validStreamUrl(const String& url, bool* secure = nullptr) {
    const bool isHttp = url.startsWith("http://");
    const bool isHttps = url.startsWith("https://");
    const size_t prefixLength = isHttps ? 8 : 7;
    if ((!isHttp && !isHttps) || url.length() <= prefixLength ||
        url.length() > 512) return false;
    for (size_t i = 0; i < url.length(); ++i) {
        const uint8_t ch = static_cast<uint8_t>(url[i]);
        if (ch <= 32 || ch == 127) return false;
    }
    if (secure) *secure = isHttps;
    return true;
}

bool redirectStatus(int status) {
    return status == HTTP_CODE_MOVED_PERMANENTLY ||
        status == HTTP_CODE_FOUND || status == HTTP_CODE_SEE_OTHER ||
        status == HTTP_CODE_TEMPORARY_REDIRECT ||
        status == HTTP_CODE_PERMANENT_REDIRECT;
}

bool resolveRedirectUrl(const String& current, String location, String& result) {
    location.trim();
    if (location.isEmpty() || location.length() > 512) return false;
    for (size_t i = 0; i < location.length(); ++i) {
        const uint8_t ch = static_cast<uint8_t>(location[i]);
        if (ch <= 32 || ch == 127) return false;
    }

    if (location.startsWith("http://") || location.startsWith("https://")) {
        result = location;
    } else {
        const int schemeEnd = current.indexOf("://");
        if (schemeEnd < 0) return false;
        const int authorityStart = schemeEnd + 3;
        int authorityEnd = current.indexOf('/', authorityStart);
        if (authorityEnd < 0) authorityEnd = current.length();
        const String origin = current.substring(0, authorityEnd);
        if (location.startsWith("//")) {
            result = current.substring(0, schemeEnd) + ":" + location;
        } else if (location[0] == '/') {
            result = origin + location;
        } else if (location[0] == '?') {
            String base = current;
            const int query = base.indexOf('?');
            const int fragment = base.indexOf('#');
            int cut = query >= 0 ? query : fragment;
            if (fragment >= 0 && (cut < 0 || fragment < cut)) cut = fragment;
            if (cut >= 0) base.remove(cut);
            result = base + location;
        } else {
            if (location[0] == '#') return false;
            String base = current;
            int cut = base.indexOf('?');
            const int fragment = base.indexOf('#');
            if (fragment >= 0 && (cut < 0 || fragment < cut)) cut = fragment;
            if (cut >= 0) base.remove(cut);
            const int slash = base.lastIndexOf('/');
            result = slash < authorityEnd
                ? origin + "/" + location
                : base.substring(0, slash + 1) + location;
        }
    }
    const int fragment = result.indexOf('#');
    if (fragment >= 0) result.remove(fragment);
    return validStreamUrl(result);
}

String redirectUrlForLog(const String& url) {
    String value = url;
    int cut = value.indexOf('?');
    const int fragment = value.indexOf('#');
    if (fragment >= 0 && (cut < 0 || fragment < cut)) cut = fragment;
    if (cut >= 0) value.remove(cut);
    if (value.length() > 96) value = value.substring(0, 93) + "...";
    return value;
}

bool isStreamTitleKey(const char* text, size_t length) {
    constexpr char key[] = "streamtitle";
    if (length != sizeof(key) - 1) return false;
    for (size_t i = 0; i < length; ++i) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != key[i]) return false;
    }
    return true;
}

enum class TitleResult : uint8_t { Absent, Found, TooLong };

TitleResult extractStreamTitle(const char* block, size_t length,
                               char* title, size_t capacity) {
    size_t pos = 0;
    while (pos < length) {
        while (pos < length &&
               (block[pos] == ';' || block[pos] == ' ' || block[pos] == '\t'))
            ++pos;
        const size_t keyStart = pos;
        while (pos < length && block[pos] != '=' && block[pos] != ';') ++pos;
        size_t keyEnd = pos;
        while (keyEnd > keyStart &&
               (block[keyEnd - 1] == ' ' || block[keyEnd - 1] == '\t'))
            --keyEnd;
        if (pos >= length || block[pos] != '=') continue;
        const bool wanted = isStreamTitleKey(block + keyStart, keyEnd - keyStart);
        ++pos;
        while (pos < length && (block[pos] == ' ' || block[pos] == '\t')) ++pos;
        const char quote = pos < length &&
            (block[pos] == '\'' || block[pos] == '"') ? block[pos++] : '\0';
        const size_t valueStart = pos;
        size_t valueEnd = pos;
        if (quote) {
            // A quote counts as closing only at the end of a field. An
            // apostrophe inside a title such as O'Connor stays intact.
            bool closed = false;
            for (size_t scan = pos; scan < length; ++scan) {
                if (block[scan] != quote) continue;
                size_t after = scan + 1;
                while (after < length &&
                       (block[after] == ' ' || block[after] == '\t')) ++after;
                if (after == length || block[after] == ';') {
                    valueEnd = scan;
                    pos = after;
                    closed = true;
                    break;
                }
            }
            if (!closed) {
                while (pos < length && block[pos] != ';') ++pos;
                valueEnd = pos; // Malformed quote: keep the field safely.
            }
        } else {
            while (pos < length && block[pos] != ';') ++pos;
            valueEnd = pos;
            while (valueEnd > valueStart &&
                   (block[valueEnd - 1] == ' ' || block[valueEnd - 1] == '\t'))
                --valueEnd;
        }
        if (!wanted) {
            while (pos < length && block[pos] != ';') ++pos;
            continue;
        }
        const size_t bytes = valueEnd - valueStart;
        if (bytes >= capacity) return TitleResult::TooLong;
        memcpy(title, block + valueStart, bytes);
        title[bytes] = '\0';
        return TitleResult::Found;
    }
    return TitleResult::Absent;
}
}

// Use the decoder's allocator interface directly. Return nullptr on OOM,
// unlike the upstream C++ wrapper allocator, which loops forever on failure.
extern "C" void* helix_malloc(int size) {
    return size > 0 ? malloc(static_cast<size_t>(size)) : nullptr;
}
extern "C" void helix_free(void* pointer) { free(pointer); }

bool RadioService::onAppTask() const {
    return _appTask && _appTask == xTaskGetCurrentTaskHandle();
}

bool RadioService::begin(AudioOutputManager& output) {
    if (_manager) return _manager == &output && onAppTask();
    _manager = &output;
    _appTask = xTaskGetCurrentTaskHandle();
    return true;
}

bool RadioService::selectStation(uint16_t id) {
    if (!onAppTask() || !_manager) return false;
    const Station* station = _stations.getById(id);
    if (!station) return false;
    if (_selectedStationId == id &&
        (_state == RadioState::Playing || _state == RadioState::WaitingForNetwork))
        return true;
    if (_running || _lease) stopStream();
    if (_fault) return false;
    cancelRetry();
    _retryAttempt = 0;
    ++_generation; // Invalidate work belonging to the previous station/session.
    _selectedStationId = id;
    _playingStationId = 0;
    _state = RadioState::WaitingForNetwork;
    _error = nullptr;
    _streamTitle = "";
    auto s = StateStore::instance().snapshot();
    s.radioStation = station->name;
    s.radioArtist = "";
    s.radioTitle = "";
    s.radioBitrate = 0;
    s.radioCodec = "";
    StateStore::instance().update(s);
    Logger::info("RADIO", String("selected station id=") + id +
        " name=" + station->name);
    if (WiFi.status() != WL_CONNECTED)
        Logger::info("RADIO", "waiting for Wi-Fi");
    return true;
}

bool RadioService::nextStation() {
    const Station* station = _stations.next(_selectedStationId);
    return station && selectStation(station->id);
}

bool RadioService::previousStation() {
    const Station* station = _stations.previous(_selectedStationId);
    return station && selectStation(station->id);
}

bool RadioService::startSelected() {
    if (!onAppTask() || !_manager || _fault) return false;
    const Station* station = _stations.getById(_selectedStationId);
    if (!station) return false;
    if (_state == RadioState::Playing && _playingStationId == station->id)
        return true;
    if (WiFi.status() != WL_CONNECTED) {
        if (_state != RadioState::WaitingForNetwork)
            Logger::info("RADIO", "waiting for Wi-Fi");
        _state = RadioState::WaitingForNetwork;
        return false;
    }
    cancelRetry();
    ++_generation; // Each HTTP attempt is a new metadata/session token.
    _state = RadioState::Starting;
    Logger::info("RADIO", String("starting station id=") + station->id);
    if (!start(station->url.c_str())) return false;
    _playingStationId = station->id;
    _state = RadioState::Playing;
    Logger::info("RADIO", "stream started");
    return true;
}

void RadioService::closeTransport() {
    _http.end();
    _plainClient.stop();
    _secureClient.stop();
    _activeClient = nullptr;
    _activeSecure = false;
}

bool RadioService::openStream(const String& initialUrl) {
    String current = initialUrl;
    String visited[MAX_REDIRECTS + 1];
    uint8_t visitedCount = 0;
    uint8_t redirects = 0;
    const uint32_t started = millis();
    bool tlsWarningLogged = false;
    const char* headers[] = {
        "Content-Type", "Content-Encoding", "Transfer-Encoding",
        "icy-name", "icy-br", "icy-metaint", "Location"
    };

    while (true) {
        if (millis() - started >= CONNECT_BUDGET_MS) {
            fail("HTTP/HTTPS connect budget exceeded");
            return false;
        }
        for (uint8_t i = 0; i < visitedCount; ++i) {
            if (visited[i] == current) {
                fail("HTTP redirect loop detected");
                return false;
            }
        }
        visited[visitedCount++] = current;

        bool secure = false;
        if (!validStreamUrl(current, &secure)) {
            fail("Invalid HTTP/HTTPS stream URL");
            return false;
        }
        closeTransport();
        _activeSecure = secure;
        if (secure) {
            _secureClient.setInsecure();
            _activeClient = &_secureClient;
            if (!tlsWarningLogged) {
                Logger::warn("RADIO", "HTTPS TLS verification disabled");
                tlsWarningLogged = true;
            }
        } else {
            _activeClient = &_plainClient;
        }

        _http.setReuse(false);
        _http.useHTTP10(true);
        _http.setConnectTimeout(secure ? 6000 : 5000);
        _http.setTimeout(2000);
        _http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        if (!_http.begin(*_activeClient, current)) {
            fail("HTTP/HTTPS begin failed");
            return false;
        }
        _http.collectHeaders(headers, sizeof(headers) / sizeof(headers[0]));
        _http.addHeader("Icy-MetaData", "1");
        const int status = _http.GET();
        if (status == HTTP_CODE_OK) {
#ifdef VOXONE_DEBUG
            const String contentType = _http.header("Content-Type");
            const int contentLength = _http.getSize();
            Logger::debug("RADIO", String("response status=") + status +
                " (HTTPClient parsed final status line)");
            Logger::debug("RADIO", "response Content-Type=[" + contentType +
                "] Content-Length=" +
                (contentLength >= 0 ? String(contentLength) : String("<absent>")));
            Logger::debug("RADIO", "response icy-name=[" +
                _http.header("icy-name") + "] icy-br=[" +
                _http.header("icy-br") + "] icy-metaint=[" +
                _http.header("icy-metaint") + "]");
#endif
            return true;
        }
        if (!redirectStatus(status)) {
            Logger::warn("RADIO", String("HTTP status=") + status);
            fail("HTTP/HTTPS GET failed");
            return false;
        }
        if (redirects >= MAX_REDIRECTS) {
            fail("HTTP redirect limit exceeded");
            return false;
        }
        String next;
        if (!resolveRedirectUrl(current, _http.header("Location"), next)) {
            fail("Invalid HTTP redirect Location");
            return false;
        }
        ++redirects;
        Logger::info("RADIO", String("redirect ") + redirects + " -> " +
            redirectUrlForLog(next));
        current = next;
    }
}

bool RadioService::start(const char* url) {
    if (!onAppTask() || _running || _lease || _fault) return false;
    _error = nullptr;
    const String initialUrl = url ? String(url) : String();
    if (!validStreamUrl(initialUrl) || WiFi.status() != WL_CONNECTED) {
        fail("HTTP/HTTPS URL or Wi-Fi unavailable");
        return false;
    }
    if (!openStream(initialUrl)) return false;
    _icyName = "";
    _codec = "";
    _streamTitle = "";
    _icyBitrate = 0;
    auto bitrateState = StateStore::instance().snapshot();
    bitrateState.radioBitrate = 0;
    bitrateState.radioCodec = "";
    StateStore::instance().update(bitrateState);
    _icyMetaint = 0;
    String type = _http.header("Content-Type");
    if (type.length() > 64) {
        fail("Unsupported codec/content type");
        return false;
    }
    type.trim();
    _codec = type;
    type.toLowerCase();
    const int parameters = type.indexOf(';');
    if (parameters >= 0) {
        type.remove(parameters);
        type.trim();
    }
    String encoding = _http.header("Content-Encoding");
    String transfer = _http.header("Transfer-Encoding");
    encoding.toLowerCase();
    transfer.toLowerCase();
    const bool mp3Type = isMp3ContentType(type);
    if (!mp3Type) {
        Logger::warn("RADIO", type.isEmpty()
            ? "Content-Type missing; MP3 fallback not enabled without header probe"
            : "Rejected Content-Type=[" + type + "]");
        fail("Unsupported codec/content type");
        return false;
    }
    if ((encoding.length() && encoding != "identity") ||
        (transfer.length() && transfer != "identity")) {
        fail("Unsupported HTTP transfer/content encoding");
        return false;
    }

    _input = static_cast<uint8_t*>(malloc(INPUT_BYTES));
    if (!_input) {
        fail("MP3 input buffer allocation failed");
        return false;
    }
    _pcm = static_cast<int16_t*>(malloc(PCM_SAMPLES * sizeof(int16_t)));
    if (!_pcm) {
        fail("MP3 PCM buffer allocation failed");
        return false;
    }
    _decoder = MP3InitDecoder();
    if (!_decoder) {
        fail("MP3 Helix decoder allocation failed");
        return false;
    }
    if (_codec.length())
        Logger::info("RADIO", "ICY codec=" + _codec);
    auto codecState = StateStore::instance().snapshot();
    codecState.radioCodec = displayCodec(_codec);
    StateStore::instance().update(codecState);
    String name = _http.header("icy-name");
    name.trim();
    if (name.length() > 0 && name.length() <= 96) {
        _icyName = name;
        Logger::info("RADIO", "ICY name=" + _icyName);
    } else if (name.length() > 96) {
        Logger::warn("RADIO", "ICY name too long; ignoring");
    }
    const Station* station = _stations.getById(_selectedStationId);
    if ((!station || station->name.isEmpty()) && !_icyName.isEmpty()) {
        auto s = StateStore::instance().snapshot();
        s.radioStation = _icyName;
        StateStore::instance().update(s);
    }
    const String bitrate = _http.header("icy-br");
    if (bitrate.length()) {
        if (parseUnsignedHeader(bitrate, 10000, _icyBitrate)) {
            Logger::info("RADIO", "ICY bitrate=" + String(_icyBitrate));
            auto state = StateStore::instance().snapshot();
            state.radioBitrate = _icyBitrate;
            StateStore::instance().update(state);
        } else Logger::warn("RADIO", "ICY bitrate invalid; ignoring");
    }
    const String interval = _http.header("icy-metaint");
    if (interval.length()) {
        if (parseUnsignedHeader(interval, MAX_METAINT, _icyMetaint)) {
            Logger::info("RADIO", "ICY metaint=" + String(_icyMetaint));
        } else {
            Logger::warn("RADIO", "ICY metaint invalid; trying audio-only");
            _icyMetaint = 0;
        }
    }
    resetIcyBody();
    // No I2S lease is taken on allocation or HTTP failure.
    if (_manager->owner() != AudioOutputOwner::None ||
        !_manager->acquire(AudioOutputOwner::Radio)) {
        fail("I2S acquire failed");
        return false;
    }
    _lease = true;
    _output = _manager->attach(AudioOutputOwner::Radio);
    if (!_output) {
        fail("I2S attach failed");
        return false;
    }
    _attached = true;
    _bodyRemaining = _http.getSize();
    _used = _pcmBytes = _pcmOffset = _skipped = 0;
    _sampleRate = 0;
    _reservoirMisses = 0;
    _bufferStarvationActive = false;
    _decoderWaitingActive = false;
    _lastData = _lastFrame = millis();
    _running = true;
    Logger::info("RADIO", String(_activeSecure ? "HTTPS" : "HTTP") +
        " MP3 started; heap=" + ESP.getFreeHeap());
    return true;
}

void RadioService::stopStream() {
    if (!onAppTask()) return;
    clearTrackMetadata();
    if (_fault) return; // A teardown fault requires a reboot.
    // Same task as loop: no in-flight producer remains after this point.
    _running = false;
    closeTransport();
    if (_decoder) MP3FreeDecoder(_decoder);
    _decoder = nullptr;
    free(_input);
    free(_pcm);
    _input = nullptr;
    _pcm = nullptr;
    _used = _pcmBytes = _pcmOffset = 0;
    _bufferStarvationActive = false;
    _decoderWaitingActive = false;
    _output = nullptr;
    _icyMetaint = 0;
    resetIcyBody();
    if (_attached) {
        if (!_manager->detach(AudioOutputOwner::Radio)) {
            _fault = true;
            _error = "I2S detach failed; lease retained";
            Logger::error("RADIO", _error);
            return;
        }
        _attached = false;
    }
    if (_lease) {
        if (!_manager->release(AudioOutputOwner::Radio)) {
            _fault = true;
            _error = "I2S release failed; lease retained";
            Logger::error("RADIO", _error);
            return;
        }
        _lease = false;
        Logger::info("RADIO", String("Stopped; heap=") + ESP.getFreeHeap());
    }
}

void RadioService::cancelRetry() {
    if (_retryScheduled) Logger::info("RADIO", "reconnect cancelled");
    _retryScheduled = false;
}

void RadioService::onNetworkLost() {
    if (!onAppTask() || !_manager || _state == RadioState::Idle ||
        _state == RadioState::WaitingForNetwork) return;
    stopStream();
    cancelRetry();
    _retryAttempt = 0;
    if (_fault) {
        _state = RadioState::Error;
        return;
    }
    ++_generation; // Invalidate callbacks from the interrupted stream.
    _playingStationId = 0;
    _state = RadioState::WaitingForNetwork;
    _error = nullptr;
    auto state = StateStore::instance().snapshot();
    state.radioArtist = "";
    state.radioTitle = "";
    state.radioBitrate = 0;
    state.radioCodec = "";
    StateStore::instance().update(state);
    Logger::info("RADIO", "waiting for network");
}

bool RadioService::retryReady(uint32_t now) const {
    return _state == RadioState::Error && _retryScheduled &&
        _retryGeneration == _generation &&
        _stations.isValidId(_selectedStationId) &&
        WiFi.status() == WL_CONNECTED &&
        static_cast<int32_t>(now - _retryDue) >= 0;
}

void RadioService::stop() {
    if (!onAppTask() || _fault) return;
    const bool active = _state != RadioState::Idle;
    stopStream();
    if (_fault) return;
    cancelRetry();
    _retryAttempt = 0;
    if (active) ++_generation;
    _state = RadioState::Idle;
    _playingStationId = 0;
    auto s = StateStore::instance().snapshot();
    s.radioStation = "";
    s.radioArtist = "";
    s.radioTitle = "";
    s.radioBitrate = 0;
    s.radioCodec = "";
    StateStore::instance().update(s);
    if (active) Logger::info("RADIO", "stopped");
}

void RadioService::clearSelection() {
    if (!onAppTask()) return;
    stop();
    if (!_fault) _selectedStationId = 0;
}

void RadioService::fail(const char* error) {
    _error = error;
    Logger::error("RADIO", error);
    stopStream();
    _playingStationId = 0;
    auto state = StateStore::instance().snapshot();
    state.radioArtist = "";
    state.radioTitle = "";
    state.radioBitrate = 0;
    state.radioCodec = "";
    StateStore::instance().update(state);
    if (_fault) {
        _state = RadioState::Error;
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        onNetworkLost();
        return;
    }
    _state = RadioState::Error;
    if (!_stations.isValidId(_selectedStationId)) return;
    static constexpr uint32_t delays[] = {2000, 5000, 10000, 30000};
    const uint8_t index = _retryAttempt < 4 ? _retryAttempt : 3;
    const uint32_t delayMs = delays[index];
    if (_retryAttempt < 4) ++_retryAttempt;
    _retryGeneration = _generation;
    _retryDue = millis() + delayMs;
    _retryScheduled = true;
    Logger::info("RADIO", String("reconnect scheduled in ") + delayMs + " ms");
}

void RadioService::consume(size_t bytes) {
    _used -= bytes;
    memmove(_input, _input + bytes, _used);
}

void RadioService::resetIcyBody() {
    _icyStage = IcyStage::Audio;
    _audioUntilMetadata = _icyMetaint;
    _metadataRemaining = 0;
    _metadataUsed = 0;
    _metadataOverflow = false;
}

void RadioService::clearTrackMetadata() {
    _streamTitle = "";
    auto s = StateStore::instance().snapshot();
    if (s.radioArtist.isEmpty() && s.radioTitle.isEmpty()) return;
    s.radioArtist = "";
    s.radioTitle = "";
    StateStore::instance().update(s);
}

void RadioService::publishIcyMetadata(uint32_t sessionGeneration) {
    if (sessionGeneration != _generation ||
        (_state != RadioState::Playing && _state != RadioState::Starting))
        return;
    if (_metadataOverflow) {
        Logger::warn("RADIO", "ICY metadata block too long; skipped");
        return;
    }
    // ICY padding is NUL. Never let it or another malformed NUL reach UI.
    size_t length = 0;
    while (length < _metadataUsed && _metadata[length] != '\0') ++length;
    char title[MAX_STREAM_TITLE_BYTES + 1];
    const TitleResult result = extractStreamTitle(
        _metadata, length, title, sizeof(title));
    if (result == TitleResult::Absent) return;
    if (result == TitleResult::TooLong) {
        Logger::warn("RADIO", "ICY StreamTitle too long; skipped");
        return;
    }
    if (_streamTitle == title) return;
    _streamTitle = title; // Full, unmodified title up to the byte limit.
    const int split = _streamTitle.indexOf(" - ");
    auto s = StateStore::instance().snapshot();
    if (split >= 0) {
        s.radioArtist = _streamTitle.substring(0, split);
        s.radioTitle = _streamTitle.substring(split + 3);
    } else {
        s.radioArtist = "";
        s.radioTitle = _streamTitle;
    }
    StateStore::instance().update(s);
    Logger::info("RADIO", "title=" + _streamTitle);
    Logger::info("RADIO", "artist=" + s.radioArtist);
}

size_t RadioService::filterIcyAudio(uint8_t* data, size_t count,
                                    uint32_t sessionGeneration) {
    if (sessionGeneration != _generation) return 0;
    if (!_icyMetaint) return count;
    size_t read = 0;
    size_t audio = 0;
    while (read < count) {
        if (_icyStage == IcyStage::Audio) {
            const size_t part = min(count - read,
                static_cast<size_t>(_audioUntilMetadata));
            if (part) {
                memmove(data + audio, data + read, part);
                read += part;
                audio += part;
                _audioUntilMetadata -= part;
            }
            if (_audioUntilMetadata == 0) _icyStage = IcyStage::Length;
        } else if (_icyStage == IcyStage::Length) {
            _metadataRemaining = static_cast<uint16_t>(data[read++]) * 16u;
            _metadataUsed = 0;
            _metadataOverflow = _metadataRemaining > MAX_METADATA_CAPTURE;
            if (_metadataRemaining) _icyStage = IcyStage::Metadata;
            else {
                _audioUntilMetadata = _icyMetaint;
                _icyStage = IcyStage::Audio;
            }
        } else {
            const size_t part = min(count - read,
                static_cast<size_t>(_metadataRemaining));
            if (!_metadataOverflow) {
                memcpy(_metadata + _metadataUsed, data + read, part);
                _metadataUsed += part;
            }
            read += part;
            _metadataRemaining -= part;
            if (_metadataRemaining == 0) {
                _metadata[_metadataUsed] = '\0';
                publishIcyMetadata(sessionGeneration);
                _audioUntilMetadata = _icyMetaint;
                _icyStage = IcyStage::Audio;
            }
        }
    }
    return audio;
}

void RadioService::loop() {
    if (!onAppTask() || !_running) return;
    PerfDiagnostics::recordRadioBuffer(_used);
    if (!_activeClient) {
        fail("HTTP/HTTPS transport unavailable");
        return;
    }
    if (!_manager->isOwnedBy(AudioOutputOwner::Radio)) {
        fail("I2S ownership lost");
        return;
    }
    // Read only bytes already available; never wait for a whole network frame.
    uint32_t operationStarted = micros();
    int available = _activeClient->available();
    PerfDiagnostics::recordNetworkAvailable(micros() - operationStarted);
    if (available > 0 && _used < INPUT_BYTES && _bodyRemaining != 0) {
        size_t count = min(static_cast<size_t>(available), INPUT_BYTES - _used);
        count = min(count, MAX_NETWORK_READ_BYTES);
        if (_bodyRemaining > 0) count = min(count, static_cast<size_t>(_bodyRemaining));
        const uint32_t sessionGeneration = _generation;
        operationStarted = micros();
        const int received = _activeClient->read(_input + _used, count);
        PerfDiagnostics::recordNetworkRead(micros() - operationStarted);
        if (received > 0) {
            if (sessionGeneration != _generation) return;
            size_t audioBytes = 0;
            {
                PerfScope icyPerf(PerfArea::RadioIcy);
                audioBytes = filterIcyAudio(_input + _used,
                    static_cast<size_t>(received), sessionGeneration);
            }
            _used += audioBytes;
            if (_bodyRemaining > 0) _bodyRemaining -= received;
            _lastData = millis();
            PerfDiagnostics::recordRadioBuffer(_used);
        }
    }
    // Allow one complete MPEG-1 stereo frame in a single App iteration.
    if (_pcmOffset < _pcmBytes) {
        const size_t count = min(_pcmBytes - _pcmOffset, MAX_PCM_WRITE_BYTES);
        const size_t written = _output->write(
            reinterpret_cast<uint8_t*>(_pcm) + _pcmOffset, count);
        if (written != count) {
            fail("I2S PCM write failed");
            return;
        }
        _pcmOffset += written;
        return;
    }
    operationStarted = micros();
    const int remainingAvailable = _activeClient->available();
    PerfDiagnostics::recordNetworkAvailable(micros() - operationStarted);
    bool transportConnected = true;
    if (_bodyRemaining != 0) {
        operationStarted = micros();
        transportConnected = _activeClient->connected();
        PerfDiagnostics::recordNetworkConnected(micros() - operationStarted);
    }
    const bool ended = _bodyRemaining == 0 ||
        (!transportConnected && remainingAvailable == 0);
    MP3FrameInfo info{};
    size_t frameBytes = 0;
    {
        PerfScope framePerf(PerfArea::RadioFrame);
        if (_used < 6) {
            if (!ended && !_bufferStarvationActive) {
                _bufferStarvationActive = true;
                PerfDiagnostics::recordRadioStarvation();
            }
            if (ended) fail("HTTP stream ended");
            else if (millis() - _lastData >= STALL_MS) fail("HTTP stream stalled");
            return;
        }
        _bufferStarvationActive = false;
        if (millis() - _lastFrame >= STALL_MS) {
            fail("No decodable MP3 frame");
            return;
        }
        const int offset = MP3FindSyncWord(_input, static_cast<int>(_used));
        if (offset != 0) {
            const size_t skipped =
                offset < 0 ? _used - 1 : static_cast<size_t>(offset);
            consume(skipped); // Keep the last byte for a split sync word.
            _skipped += skipped;
            if (_skipped > 65536) fail("MP3 sync not found");
            return;
        }
        // Validate a complete, bounded Layer III frame before calling Helix.
        const int version = (_input[1] >> 3) & 3;
        if (version < 2 || ((_input[1] >> 1) & 3) != 1 ||
            (_input[2] >> 4) == 0 || (_input[2] >> 4) == 15 ||
            ((_input[2] >> 2) & 3) == 3) {
            consume(1);
            ++_skipped;
            return;
        }
        if (MP3GetNextFrameInfo(_decoder, &info, _input) != ERR_MP3_NONE ||
            info.samprate < 16000 || info.samprate > 48000 ||
            info.bitrate <= 0) {
            fail("Invalid MP3 header");
            return;
        }
        frameBytes = (version == 3 ? 144 : 72) * info.bitrate /
            info.samprate + ((_input[2] >> 1) & 1);
        const size_t sideBytes =
            version == 3 ? (info.nChans == 1 ? 17 : 32) :
            (info.nChans == 1 ? 9 : 17);
        if (frameBytes < 4 + ((_input[1] & 1) ? 0 : 2) + sideBytes ||
            frameBytes > 2048) {
            fail("Unsupported MP3 frame size");
            return;
        }
        if (_used < frameBytes) {
            if (!_decoderWaitingActive) {
                _decoderWaitingActive = true;
                PerfDiagnostics::recordDecoderWaiting();
            }
            if (ended) fail("Truncated MP3 frame");
            else if (millis() - _lastData >= STALL_MS)
                fail("HTTP stream stalled");
            return;
        }
    }
    _decoderWaitingActive = false;
    unsigned char* cursor = _input;
    int bytesLeft = static_cast<int>(frameBytes);
    int result = 0;
    {
        PerfScope decodePerf(PerfArea::RadioDecode);
        result = MP3Decode(_decoder, &cursor, &bytesLeft, _pcm, 0);
    }
    PerfScope pcmPerf(PerfArea::RadioPcm);
    consume(frameBytes);
    PerfDiagnostics::recordRadioBuffer(_used);
    if (result == ERR_MP3_MAINDATA_UNDERFLOW && ++_reservoirMisses <= 8) return;
    if (result != ERR_MP3_NONE) {
        Logger::warn("RADIO", String("Helix error: ") + result);
        fail("MP3 decode failed");
        return;
    }
    _reservoirMisses = 0;
    _skipped = 0;
    MP3GetLastFrameInfo(_decoder, &info);
    if (info.bitsPerSample != 16 || (info.nChans != 1 && info.nChans != 2) ||
        info.outputSamps <= 0 || info.outputSamps > static_cast<int>(PCM_SAMPLES) ||
        (info.nChans == 1 && info.outputSamps > static_cast<int>(PCM_SAMPLES / 2)) ||
        info.outputSamps % info.nChans) {
        fail("Unsupported decoded PCM");
        return;
    }
    if (_sampleRate != static_cast<uint32_t>(info.samprate)) {
        if (!_manager->configureStereo16(AudioOutputOwner::Radio, info.samprate)) {
            fail("I2S PCM configuration failed");
            return;
        }
        _sampleRate = info.samprate;
        Logger::info("RADIO", String("PCM rate=") + _sampleRate +
            " channels=" + info.nChans);
    }
    // Stereo output is fixed. Expand mono backwards in the same PCM buffer.
    if (info.nChans == 1) {
        for (int i = info.outputSamps - 1; i >= 0; --i) {
            const int16_t value = _pcm[i];
            _pcm[2 * i] = _pcm[2 * i + 1] = value;
        }
        _pcmBytes = info.outputSamps * 2 * sizeof(int16_t);
    } else {
        // Stereo PCM stays full-scale until the shared output gain.
        _pcmBytes = info.outputSamps * sizeof(int16_t);
    }
    _pcmOffset = 0;
    _lastFrame = millis();
    if (_retryAttempt) {
        _retryAttempt = 0;
        Logger::info("RADIO", "stream recovered");
    }
}
