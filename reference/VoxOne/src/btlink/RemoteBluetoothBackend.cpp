#include "RemoteBluetoothBackend.h"
#include "../diagnostics/Logger.h"
#include <cstdlib>
#include <cstring>

bool RemoteBluetoothBackend::begin() {
    if (_moduleState != BtModuleState::Disabled) return true;
    _serial.begin(115200, SERIAL_8N1, _rx, _tx);
    _moduleState = BtModuleState::Waiting;
    _waitingSince = millis();
    return true; // UART initialized; READY is asynchronous and non-blocking.
}

void RemoteBluetoothBackend::send(const char* line) {
    _serial.println(line);
}

void RemoteBluetoothBackend::sendControl(const char* line) {
    if (_moduleState == BtModuleState::Ready) send(line);
}

void RemoteBluetoothBackend::setVolume(uint8_t value) {
    if (_moduleState != BtModuleState::Ready) return;
    _serial.print("SET_VOLUME ");
    _serial.println(value);
}

bool RemoteBluetoothBackend::parseUnsigned(const char* text, uint32_t maximum,
                                            uint32_t& value) {
    if (!text || *text < '0' || *text > '9') return false;
    uint32_t parsed = 0;
    do {
        const uint32_t digit = static_cast<uint32_t>(*text - '0');
        if (parsed > (maximum - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
        ++text;
    } while (*text >= '0' && *text <= '9');
    if (*text != '\0') return false;
    value = parsed;
    return true;
}

void RemoteBluetoothBackend::copyText(char* destination, size_t capacity,
                                      const char* source) {
    if (!destination || capacity == 0) return;
    if (!source) {
        destination[0] = '\0';
        return;
    }
    const size_t length = strnlen(source, capacity - 1);
    memcpy(destination, source, length);
    destination[length] = '\0';
}

const char* RemoteBluetoothBackend::commandResultName(BtCommandResult result) {
    switch (result) {
        case BtCommandResult::Ok: return "OK";
        case BtCommandResult::NotConnected: return "ERR NOT_CONNECTED";
        case BtCommandResult::InvalidValue: return "ERR INVALID_VALUE";
        case BtCommandResult::UnknownCommand: return "ERR UNKNOWN_COMMAND";
        case BtCommandResult::NotImplemented: return "ERR NOT_IMPLEMENTED";
        case BtCommandResult::LineTooLong: return "ERR LINE_TOO_LONG";
        case BtCommandResult::None: default: return "NONE";
    }
}

void RemoteBluetoothBackend::logLine(const char* line, bool warning) const {
    if (warning) Logger::warn("BTLINK", line);
    else Logger::info("BTLINK", line);
}

void RemoteBluetoothBackend::parse(const char* line) {
    if (!line || !line[0]) return;
    if (!strncmp(line, "PROTO ", 6)) {
        uint32_t version = 0;
        if (!parseUnsigned(line + 6, UINT16_MAX, version)) {
            logLine("Invalid PROTO value", true);
            return;
        }
        _status.protocolVersion = static_cast<uint16_t>(version);
        logLine(line, version != 1);
        return;
    }
    if (!strcmp(line, "READY")) {
        const bool firstReady = !_status.ready;
        _status.ready = true;
        _moduleState = BtModuleState::Ready;
        logLine(line);
        if (firstReady) requestStatus();
        return;
    }
    if (!strcmp(line, "CONNECTED")) {
        _status.connected = true;
        logLine(line);
        return;
    }
    if (!strcmp(line, "DISCONNECTED")) {
        _status.connected = false;
        _status.playback = BtPlaybackState::Stopped;
        _status.playingEdge = false;
        _status.deviceName[0] = _status.artist[0] = _status.title[0] =
            _status.album[0] = '\0';
        _status.sampleRateKnown = false;
        _status.sampleRate = 0;
        _status.volumeKnown = false;
        _status.volume = 0;
        logLine(line);
        return;
    }
    if (!strcmp(line, "PLAYING")) {
        _status.playingEdge = _status.playback != BtPlaybackState::Playing;
        _status.playback = BtPlaybackState::Playing;
        logLine(line);
        return;
    }
    if (!strcmp(line, "PAUSED")) {
        _status.playback = BtPlaybackState::Paused;
        logLine(line);
        return;
    }
    if (!strcmp(line, "STOPPED")) {
        _status.playback = BtPlaybackState::Stopped;
        logLine(line);
        return;
    }
    if (!strncmp(line, "DEVICE ", 7)) {
        copyText(_status.deviceName, sizeof(_status.deviceName), line + 7);
        logLine(line);
        return;
    }
    if (!strncmp(line, "ARTIST ", 7)) {
        copyText(_status.artist, sizeof(_status.artist), line + 7);
        logLine(line);
        return;
    }
    if (!strncmp(line, "TITLE ", 6)) {
        copyText(_status.title, sizeof(_status.title), line + 6);
        logLine(line);
        return;
    }
    if (!strncmp(line, "ALBUM ", 6)) {
        copyText(_status.album, sizeof(_status.album), line + 6);
        logLine(line);
        return;
    }
    if (!strncmp(line, "SAMPLE_RATE ", 12)) {
        uint32_t rate = 0;
        if (!parseUnsigned(line + 12, UINT32_MAX, rate) || rate == 0) {
            logLine("Invalid SAMPLE_RATE value", true);
            return;
        }
        _status.sampleRate = rate;
        _status.sampleRateKnown = true;
        logLine(line);
        return;
    }
    if (!strncmp(line, "VOLUME ", 7)) {
        uint32_t value = 0;
        if (!parseUnsigned(line + 7, 127, value)) {
            logLine("Invalid VOLUME value", true);
            return;
        }
        _status.volume = static_cast<uint8_t>(value);
        _status.volumeKnown = true;
        logLine(line);
        return;
    }

    BtCommandResult result = BtCommandResult::None;
    if (!strcmp(line, "OK")) result = BtCommandResult::Ok;
    else if (!strcmp(line, "ERR NOT_CONNECTED")) result = BtCommandResult::NotConnected;
    else if (!strcmp(line, "ERR INVALID_VALUE")) result = BtCommandResult::InvalidValue;
    else if (!strcmp(line, "ERR UNKNOWN_COMMAND")) result = BtCommandResult::UnknownCommand;
    else if (!strcmp(line, "ERR NOT_IMPLEMENTED")) result = BtCommandResult::NotImplemented;
    else if (!strcmp(line, "ERR LINE_TOO_LONG")) result = BtCommandResult::LineTooLong;
    if (result != BtCommandResult::None) {
        _status.lastCommandResult = result;
        logLine(commandResultName(result), result != BtCommandResult::Ok);
        return;
    }
    logLine(line, true);
}

void RemoteBluetoothBackend::loop() {
    if (_moduleState == BtModuleState::Disabled) return;
    for (unsigned budget = 0; budget < 256 && _serial.available(); ++budget) {
        const char c = static_cast<char>(_serial.read());
        if (c == '\r') continue;
        if (c == '\n') {
            if (!_overflow) {
                _line[_length] = '\0';
                parse(_line);
            } else {
                logLine("Line too long; ignored", true);
            }
            _length = 0;
            _overflow = false;
        } else if (!_overflow && _length < sizeof(_line) - 1) {
            _line[_length++] = c;
        } else {
            _overflow = true;
        }
    }
    if (_moduleState == BtModuleState::Waiting &&
        millis() - _waitingSince >= READY_TIMEOUT_MS)
        _moduleState = BtModuleState::Unavailable;
}
