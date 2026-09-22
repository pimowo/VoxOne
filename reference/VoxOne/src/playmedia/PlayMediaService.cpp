#include "PlayMediaService.h"

#include <cstdlib>
#include <cstring>
#include <WiFi.h>

#include "../diagnostics/Logger.h"
#include "../diagnostics/PerfDiagnostics.h"

bool PlayMediaService::onAppTask() const {
    return _appTask && _appTask == xTaskGetCurrentTaskHandle();
}

bool PlayMediaService::begin(AudioOutputManager& output) {
    if (_manager) return _manager == &output && onAppTask();
    _manager = &output;
    _appTask = xTaskGetCurrentTaskHandle();
    return true;
}

bool PlayMediaService::validUrl(const String& url) const {
    if (url.length() < 8 || url.length() > MAX_URL_BYTES ||
        !url.startsWith("http://")) return false;
    for (size_t i = 0; i < url.length(); ++i) {
        const uint8_t ch = static_cast<uint8_t>(url[i]);
        if (ch <= 0x20 || ch == 0x7f) return false;
    }
    return true;
}

bool PlayMediaService::playUrl(const String& url) {
    if (!onAppTask() || !_manager || _fault) return false;
    if (_state != PlayMediaState::Idle) stop();
    if (_fault) return false;
    _error = nullptr;
    _state = PlayMediaState::Starting;
    if (!validUrl(url)) {
        fail("Only bounded direct HTTP URLs are supported");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        fail("Wi-Fi unavailable");
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

    _http.setReuse(false);
    _http.useHTTP10(true);
    _http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    _http.setTimeout(READ_TIMEOUT_MS);
    _http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!_http.begin(_client, url)) {
        fail("HTTP begin failed");
        return false;
    }
    const char* headers[] = {
        "Content-Type", "Content-Encoding", "Transfer-Encoding"
    };
    _http.collectHeaders(headers, sizeof(headers) / sizeof(headers[0]));
    const int status = _http.GET();
    if (status != HTTP_CODE_OK) {
        Logger::warn("PLAY_MEDIA", String("HTTP status=") + status);
        fail("HTTP GET failed (direct 200 required)");
        return false;
    }
    String type = _http.header("Content-Type");
    String encoding = _http.header("Content-Encoding");
    String transfer = _http.header("Transfer-Encoding");
    type.toLowerCase();
    encoding.toLowerCase();
    transfer.toLowerCase();
    if ((type.length() && !type.startsWith("audio/mpeg") &&
         !type.startsWith("audio/mp3") &&
         !type.startsWith("application/octet-stream")) ||
        (encoding.length() && encoding != "identity") ||
        (transfer.length() && transfer != "identity")) {
        fail("Unsupported HTTP body (direct MP3 identity required)");
        return false;
    }

    // Acquire I2S only after all network/decoder setup succeeded.
    if (_manager->owner() != AudioOutputOwner::None ||
        !_manager->acquire(AudioOutputOwner::PlayMedia)) {
        fail("I2S acquire failed");
        return false;
    }
    _lease = true;
    _output = _manager->attach(AudioOutputOwner::PlayMedia);
    if (!_output) {
        fail("I2S attach failed");
        return false;
    }
    _attached = true;
    _bodyRemaining = _http.getSize();
    _used = _pcmBytes = _pcmOffset = _skipped = 0;
    _sampleRate = 0;
    _reservoirMisses = 0;
    _lastData = _lastFrame = millis();
    _state = PlayMediaState::Playing;
    Logger::info("PLAY_MEDIA", String("HTTP MP3 started; heap=") + ESP.getFreeHeap());
    return true;
}

void PlayMediaService::cleanup() {
    if (!onAppTask()) return;
    _http.end();
    _client.stop();
    if (_decoder) MP3FreeDecoder(_decoder);
    _decoder = nullptr;
    free(_input);
    free(_pcm);
    _input = nullptr;
    _pcm = nullptr;
    _used = _pcmBytes = _pcmOffset = 0;
    _output = nullptr;
    if (_attached) {
        if (!_manager->detach(AudioOutputOwner::PlayMedia)) {
            _fault = true;
            _error = "I2S detach failed; lease retained";
            Logger::error("PLAY_MEDIA", _error);
            return;
        }
        _attached = false;
    }
    if (_lease) {
        if (!_manager->release(AudioOutputOwner::PlayMedia)) {
            _fault = true;
            _error = "I2S release failed; lease retained";
            Logger::error("PLAY_MEDIA", _error);
            return;
        }
        _lease = false;
    }
}

void PlayMediaService::fail(const char* error) {
    _error = error;
    Logger::warn("PLAY_MEDIA", error);
    cleanup();
    _state = PlayMediaState::Error;
}

void PlayMediaService::complete() {
    cleanup();
    _state = _fault ? PlayMediaState::Error : PlayMediaState::Completed;
    if (!_fault) Logger::info("PLAY_MEDIA", "stream completed");
}

void PlayMediaService::stop() {
    if (!onAppTask()) return;
    cleanup();
    _state = _fault ? PlayMediaState::Error : PlayMediaState::Idle;
}

void PlayMediaService::consume(size_t bytes) {
    _used -= bytes;
    memmove(_input, _input + bytes, _used);
}

void PlayMediaService::loop() {
    if (!onAppTask() || _state != PlayMediaState::Playing) return;
    if (WiFi.status() != WL_CONNECTED) {
        fail("Wi-Fi lost");
        return;
    }
    if (!_manager->isOwnedBy(AudioOutputOwner::PlayMedia)) {
        fail("I2S ownership lost");
        return;
    }

    {
        PerfScope networkPerf(PerfArea::PlayMediaNetwork);
        const int available = _client.available();
        if (available > 0 && _used < INPUT_BYTES && _bodyRemaining != 0) {
            size_t count =
                min(static_cast<size_t>(available), INPUT_BYTES - _used);
            count = min(count, static_cast<size_t>(1024));
            if (_bodyRemaining > 0)
                count = min(count, static_cast<size_t>(_bodyRemaining));
            const int received = _client.read(_input + _used, count);
            if (received > 0) {
                _used += static_cast<size_t>(received);
                if (_bodyRemaining > 0) _bodyRemaining -= received;
                _lastData = millis();
            }
        }
    }

    // Keep every App-loop I2S write bounded, like RadioService.
    if (_pcmOffset < _pcmBytes) {
        const size_t count = min(_pcmBytes - _pcmOffset, static_cast<size_t>(1024));
        const size_t written = _output->write(
            reinterpret_cast<uint8_t*>(_pcm) + _pcmOffset, count);
        if (written != count) {
            fail("I2S PCM write failed");
            return;
        }
        _pcmOffset += written;
        return;
    }

    bool ended = false;
    {
        PerfScope networkPerf(PerfArea::PlayMediaNetwork);
        ended = _bodyRemaining == 0 ||
            (!_client.connected() && _client.available() == 0);
    }
    if (_used < 6) {
        if (ended) complete();
        else if (millis() - _lastData >= STALL_TIMEOUT_MS)
            fail("HTTP stream stalled");
        return;
    }
    if (millis() - _lastFrame >= STALL_TIMEOUT_MS) {
        fail("No decodable MP3 frame");
        return;
    }
    const int offset = MP3FindSyncWord(_input, static_cast<int>(_used));
    if (offset != 0) {
        const size_t skipped = offset < 0 ? _used - 1 : static_cast<size_t>(offset);
        consume(skipped);
        _skipped += skipped;
        if (_skipped > 65536) fail("MP3 sync not found");
        return;
    }

    const int version = (_input[1] >> 3) & 3;
    if (version < 2 || ((_input[1] >> 1) & 3) != 1 ||
        (_input[2] >> 4) == 0 || (_input[2] >> 4) == 15 ||
        ((_input[2] >> 2) & 3) == 3) {
        consume(1);
        ++_skipped;
        return;
    }
    MP3FrameInfo info{};
    if (MP3GetNextFrameInfo(_decoder, &info, _input) != ERR_MP3_NONE ||
        info.samprate < 16000 || info.samprate > 48000 || info.bitrate <= 0) {
        fail("Invalid MP3 header");
        return;
    }
    const size_t frameBytes = (version == 3 ? 144 : 72) * info.bitrate /
        info.samprate + ((_input[2] >> 1) & 1);
    const size_t sideBytes = version == 3 ? (info.nChans == 1 ? 17 : 32) :
        (info.nChans == 1 ? 9 : 17);
    if (frameBytes < 4 + ((_input[1] & 1) ? 0 : 2) + sideBytes ||
        frameBytes > 2048) {
        fail("Unsupported MP3 frame size");
        return;
    }
    if (_used < frameBytes) {
        if (ended) fail("Truncated MP3 frame");
        else if (millis() - _lastData >= STALL_TIMEOUT_MS)
            fail("HTTP stream stalled");
        return;
    }

    unsigned char* cursor = _input;
    int bytesLeft = static_cast<int>(frameBytes);
    int result = 0;
    {
        PerfScope decodePerf(PerfArea::PlayMediaDecode);
        result = MP3Decode(_decoder, &cursor, &bytesLeft, _pcm, 0);
    }
    PerfScope pcmPerf(PerfArea::PlayMediaPcm);
    consume(frameBytes);
    if (result == ERR_MP3_MAINDATA_UNDERFLOW && ++_reservoirMisses <= 8) return;
    if (result != ERR_MP3_NONE) {
        Logger::warn("PLAY_MEDIA", String("Helix error=") + result);
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
        if (!_manager->configureStereo16(AudioOutputOwner::PlayMedia, info.samprate)) {
            fail("I2S PCM configuration failed");
            return;
        }
        _sampleRate = info.samprate;
        Logger::info("PLAY_MEDIA", String("PCM rate=") + _sampleRate +
            " channels=" + info.nChans);
    }
    if (info.nChans == 1) {
        for (int i = info.outputSamps - 1; i >= 0; --i) {
            const int16_t value = _pcm[i];
            _pcm[2 * i] = _pcm[2 * i + 1] = value;
        }
        _pcmBytes = info.outputSamps * 2 * sizeof(int16_t);
    } else {
        _pcmBytes = info.outputSamps * sizeof(int16_t);
    }
    _pcmOffset = 0;
    _lastFrame = millis();
}
