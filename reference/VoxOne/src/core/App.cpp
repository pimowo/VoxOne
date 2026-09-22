#include "App.h"

#include "AppConfig.h"
#include "BoardConfig.h"
#include "StateStore.h"
#include "CommandQueue.h"
#include "../diagnostics/Logger.h"
#include "../diagnostics/PerfDiagnostics.h"
#include <cstring>
#include <new>
#include <esp_system.h>
#include <WiFi.h>

#ifndef VOXONE_RADIO_TEST_CONTROLS
#define VOXONE_RADIO_TEST_CONTROLS 1
#endif

bool App::begin() {
    Logger::begin(AppConfig::SERIAL_BAUD);
    Logger::info("BOOT", "Reset reason=" + String(static_cast<int>(esp_reset_reason())));

    Logger::info(
        "BOOT",
        String("VoxOne ") +
        AppConfig::FW_VERSION
    );

    Logger::info(
        "BOOT",
        String("Board: ") +
        Board::PROFILE_NAME
    );

    if (!CommandQueue::instance().begin()) {
        Logger::error(
            "CORE",
            "CommandQueue init failed"
        );
        return false;
    }

    if (!_config.begin()) {
        Logger::error(
            "CONFIG",
            "ConfigManager init failed"
        );
        return false;
    }

    Logger::info(
        "CONFIG",
        "Schema v" +
        String(_config.schemaVersion())
    );

    const auto& runtime = _config.config();
    const auto& features = runtime.features;
    Logger::info("BOOT", "mode=NORMAL web=ALWAYS_ON");
    const auto defaultSource = features.radioEnabled ? DefaultSource::Radio :
        features.bluetoothEnabled ? DefaultSource::Bluetooth : DefaultSource::Stop;
    _sourceIntent = AudioSource::Stop;
    Logger::info("BOOT", "VoxOne runtime:");
    Logger::info("BOOT", String("  Radio: ") + (features.radioEnabled ? "ON" : "OFF"));
    Logger::info("BOOT", String("  Bluetooth: ") + (features.bluetoothEnabled ? "ON" : "OFF"));
    Logger::info("BOOT", String("  MQTT: ") + (features.mqttEnabled ? "ON" : "OFF"));
    Logger::info("BOOT", String("  Display: ") + (features.displayEnabled ? "ON" : "OFF"));
    Logger::info("BOOT", String("  Encoder: ") + (features.encoderEnabled ? "ON" : "OFF"));
    const char* sourceName = defaultSource == DefaultSource::Bluetooth ? "BT" :
        defaultSource == DefaultSource::Radio ? "RADIO" : "STOP";
    Logger::info("BOOT", String("  Default source: ") + sourceName);
    if (defaultSource != runtime.audio.defaultSource)
        Logger::info("BOOT", "Runtime source follows enabled RADIO/BT features");
    Logger::info("BOOT", String("  Audio output: ") +
        (runtime.audio.outputType == OutputType::PCM5102A ? "PCM5102A" : "MAX98357A"));
    Logger::info("BOOT", String("  Display type: ") +
        (runtime.display.type == DisplayType::ST7789 ? "ST7789" : "SSD1306"));

    auto s =
        StateStore::instance().snapshot();

    s.volume = _config.volume();
    s.playback = PlaybackState::Stop;
    s.audioSource = AudioSource::Stop;
    s.playMediaActive = false;
    s.bluetoothModuleState = BtModuleState::Disabled;
    s.bluetoothUnavailableNotice = defaultSource == DefaultSource::Bluetooth;
    s.bluetoothReconnectGrace = false;
    s.bluetoothStarted = false;
    s.bluetoothConnected = false;
    s.bluetoothPlaying = false;
    s.bluetoothArtist = "";
    s.bluetoothTitle = "";
    if (s.bluetoothUnavailableNotice) s.lastMessage = "BT_AUDIO_UNAVAILABLE";

    StateStore::instance().update(s);

    if (!_radio.stations().begin()) {
        Logger::error("STATIONS", "StationStore initialization failed");
        return false;
    }
    if (!_radio.stations().isValidId(_config.config().radio.defaultStation)) {
        const Station* first = _radio.stations().first();
        const uint16_t replacement = first ? first->id : 0;
        Logger::warn("STATIONS", String("invalid default id=") +
            _config.config().radio.defaultStation + " repaired=" + replacement);
        if (!_config.saveDefaultStation(replacement)) {
            Logger::error("STATIONS", "Default station repair failed");
            return false;
        }
    }

    if (features.encoderEnabled && Board::HAS_ENCODER) {
        _encoder.begin(runtime.encoder);
    }
    if (features.displayEnabled && Board::HAS_DISPLAY) {
        if (runtime.display.type == DisplayType::ST7789) {
            _display.reset(new (std::nothrow) DisplayService(runtime.display.st7789));
            if (_display) _display->begin();
            else Logger::error("DISPLAY", "ST7789 allocation failed");
        } else {
            Logger::warn("DISPLAY", "SSD1306 not runtime implemented; display skipped");
        }
    }

    _wifi.begin(_config);
    _web.begin(_config, _wifi, _audioOutput, _display.get(), _radio.stations(), {
        [this](uint16_t id) { return selectSource(AudioSource::Radio, id); },
        [this](uint16_t id) { handleStationRemoved(id); }
    });
    _mqtt.begin(runtime, {
        [this](int value) { mqttSetVolume(value); },
        [this]() { mqttToggle(); },
        [this]() { mqttNext(); },
        [this]() { mqttPrevious(); },
        [this]() { mqttPlay(); },
        [this]() { mqttYoRadioStop(); },
        [this]() { mqttYoRadioTurnOn(); },
        [this]() { mqttYoRadioTurnOff(); },
        [this](uint16_t position) {
            const Station* station = position > 0
                ? _radio.stations().at(position - 1) : nullptr;
            return station && selectSource(AudioSource::Radio, station->id);
        },
        [this]() {
            return _radio.stations().positionOfId(_radio.selectedStationId());
        },
        [this](uint16_t position) {
            const Station* station = position > 0
                ? _radio.stations().at(position - 1) : nullptr;
            return station ? station->name : String();
        },
        [this]() { return _yoRadioOn; },
        [this](const String& url) { return queuePlayMediaUrl(url); }
    });
    if (!_audioOutput.begin(runtime.audio.i2sBclk, runtime.audio.i2sLrclk, runtime.audio.i2sDout)) {
        Logger::error("AUDIO", "AudioOutputManager init failed");
        return false;
    }
    _audioOutput.setVolume(s.volume);
    if (runtime.audio.outputType == OutputType::PCM5102A) {
        _playMediaAvailable = _playMedia.begin(_audioOutput);
        if (!_playMediaAvailable)
            Logger::warn("PLAY_MEDIA", "Runtime initialization failed");
    }
    Logger::info("BOOT", String("  PlayMedia/TTS: ") +
        (_playMediaAvailable ? "READY" : "UNAVAILABLE"));
    if (runtime.audio.outputType == OutputType::MAX98357A)
        Logger::warn("AUDIO", "MAX98357A not runtime implemented; audio sources skipped");
    if (features.bluetoothEnabled) {
        // UART control only. The future I2S RX input remains unwired.
        if (_btLink.begin()) {
            _lastBtModuleState = BtModuleState::Waiting;
            s = StateStore::instance().snapshot();
            s.bluetoothModuleState = BtModuleState::Waiting;
            StateStore::instance().update(s);
            Logger::info("BT", "External module waiting for PROTO 1 / READY");
        } else {
            _lastBtModuleState = BtModuleState::Unavailable;
            s = StateStore::instance().snapshot();
            s.bluetoothModuleState = BtModuleState::Unavailable;
            StateStore::instance().update(s);
            Logger::warn("BT", "External module UART unavailable");
        }
    }
    if (features.radioEnabled) {
        _radioAvailable = _radio.begin(_audioOutput);
        if (_radioAvailable) {
            _radio.setVolume(s.volume);
            if (defaultSource == DefaultSource::Radio &&
                runtime.audio.outputType == OutputType::PCM5102A) {
                const int id = runtime.radio.defaultStation;
                if (id <= 0 || !_radio.stations().isValidId(id)) {
                    Logger::warn("RADIO", String("default station invalid id=") + id);
                } else {
                    // Selection is cheap; App.loop starts after STA has an IP.
                    if (_radio.selectStation(static_cast<uint16_t>(id))) {
                        _sourceIntent = AudioSource::Radio;
                        _wifi.setRadioNetworkNeeded(true);
                        auto radioState = StateStore::instance().snapshot();
                        radioState.audioSource = AudioSource::Radio;
                        StateStore::instance().update(radioState);
                    }
                }
            }
        } else Logger::warn("RADIO", "Radio unavailable");
    }
    if (!features.radioEnabled && features.bluetoothEnabled)
        selectSource(AudioSource::Bluetooth, 0, false);
    Logger::info("SOURCE", "Serial: source bt / source radio / source stop");
#if VOXONE_RADIO_TEST_CONTROLS
    Logger::info("RADIO", "Serial test commands: radio start / radio stop");
#endif
    if (runtime.network.ntpEnabled) {
        _time.begin();
        _timeStarted = true;
    }
    Logger::info("UI", "mode=HOME");
    Logger::info(
        "BOOT",
        "VoxOne ready"
    );

    return true;
}

bool App::startRadio(uint16_t stationId) {
    if (_sourceIntent != AudioSource::Radio ||
        _config.config().audio.outputType != OutputType::PCM5102A ||
        !_radioAvailable || !_radio.stations().isValidId(stationId)) return false;
    if (_radio.selectedStationId() != stationId ||
        _radio.state() == RadioState::Idle) {
        if (!_radio.selectStation(stationId)) return false;
    }
    if (!StateStore::instance().snapshot().wifiConnected ||
        WiFi.status() != WL_CONNECTED) return false;
    if (_radioSession && _radio.playingStationId() == stationId) return true;
    _radioSession = true;
    auto s = StateStore::instance().snapshot();
    s.bluetoothUnavailableNotice = false;
    s.audioSource = AudioSource::Radio;
    s.playback = PlaybackState::Stop;
    s.lastMessage = "RADIO_STARTING";
    StateStore::instance().update(s);
    _radio.setVolume(s.volume);
    if (!_radio.startSelected()) {
        Logger::warn("RADIO", "stream failed");
        s = StateStore::instance().snapshot();
        s.playback = PlaybackState::Stop;
        s.lastMessage = _radio.lastError() ? _radio.lastError() : "RADIO_WAITING";
        StateStore::instance().update(s);
        return false; // Keep RADIO intent until an explicit source switch.
    }
    s = StateStore::instance().snapshot();
    s.audioSource = AudioSource::Radio;
    s.playback = PlaybackState::Playing;
    s.lastMessage = "RADIO_RUNNING";
    StateStore::instance().update(s);
    return true;
}

void App::stopRadio() {
    if (!_radioSession && _sourceIntent != AudioSource::Radio &&
        _radio.state() == RadioState::Idle) return;
    _sourceIntent = AudioSource::Stop;
    _wifi.setRadioNetworkNeeded(false);
    _radio.stop(); // Invalidates generation and cancels all pending retries.
    auto s = StateStore::instance().snapshot();
    s.audioSource = AudioSource::Stop;
    s.playback = PlaybackState::Stop;
    s.lastMessage = "RADIO_STOPPED";
    StateStore::instance().update(s);
    if (_audioOutput.owner() != AudioOutputOwner::None) {
        Logger::error("RADIO", "Output lease retained; source switch blocked");
        return;
    }
    _radioSession = false;
}

bool App::selectSourceDuringPlayMedia(AudioSource source, uint16_t stationId) {
    if (!_playMediaOverrideActive || !_playMediaSnapshot.valid) return false;
    auto state = StateStore::instance().snapshot();
    if (source == AudioSource::Radio) {
        if (!_radioAvailable ||
            _config.config().audio.outputType != OutputType::PCM5102A) {
            Logger::warn("SOURCE", "Pending RADIO unavailable");
            return false;
        }
        const uint16_t selected = _radio.selectedStationId();
        const int configured = _config.config().radio.defaultStation;
        const uint16_t id = stationId ? stationId :
            _radio.stations().isValidId(selected) ? selected :
            configured > 0 ? static_cast<uint16_t>(configured) : 0;
        const Station* station = _radio.stations().getById(id);
        if (!station) {
            Logger::warn("RADIO", String("pending station invalid id=") + id);
            return false;
        }
        if (!_radio.selectStation(id)) return false;
        _radio.stop(); // Keep the selection but never take I2S during override.
        _radioSession = false;
        _playMediaSnapshot.baseSource = AudioSource::Radio;
        _playMediaSnapshot.stationId = id;
        _playMediaSnapshot.radioWasPlaying = false;
        _playMediaSnapshot.radioWasPaused = false;
        _playMediaSnapshot.btWasPlaying = false;
        _sourceIntent = AudioSource::Radio;
        _wifi.setRadioNetworkNeeded(true);
        state.audioSource = AudioSource::Radio;
        state.playback = PlaybackState::Stop;
        state.radioStation = station->name;
        state.radioArtist = "";
        state.radioTitle = "";
        state.radioBitrate = 0;
        state.radioCodec = "";
        state.bluetoothArtist = "";
        state.bluetoothTitle = "";
        state.lastMessage = "PLAY_MEDIA_PENDING_RADIO";
        StateStore::instance().update(state);
        Logger::info("PLAY_MEDIA", String("pending base=RADIO station=") + id);
        return true;
    }
    if (source == AudioSource::Bluetooth) {
        _playMediaSnapshot.baseSource = AudioSource::Bluetooth;
        _playMediaSnapshot.radioWasPlaying = false;
        _playMediaSnapshot.radioWasPaused = false;
        _playMediaSnapshot.btWasPlaying = false;
        _sourceIntent = AudioSource::Bluetooth;
        _wifi.setRadioNetworkNeeded(false);
        state.audioSource = AudioSource::Bluetooth;
        state.playback = PlaybackState::Stop;
        state.radioArtist = "";
        state.radioTitle = "";
        state.radioBitrate = 0;
        state.radioCodec = "";
        state.bluetoothArtist = "";
        state.bluetoothTitle = "";
        state.lastMessage = "PLAY_MEDIA_PENDING_BT";
        StateStore::instance().update(state);
        Logger::info("PLAY_MEDIA", "pending base=BT");
        return true;
    }
    if (source == AudioSource::Stop) {
        _playMediaSnapshot.baseSource = AudioSource::Stop;
        _playMediaSnapshot.radioWasPlaying = false;
        _playMediaSnapshot.radioWasPaused = false;
        _playMediaSnapshot.btWasPlaying = false;
        _sourceIntent = AudioSource::Stop;
        _wifi.setRadioNetworkNeeded(false);
        state.audioSource = AudioSource::Stop;
        state.playback = PlaybackState::Stop;
        state.radioArtist = "";
        state.radioTitle = "";
        state.radioBitrate = 0;
        state.radioCodec = "";
        state.bluetoothArtist = "";
        state.bluetoothTitle = "";
        state.lastMessage = "PLAY_MEDIA_PENDING_STOP";
        StateStore::instance().update(state);
        Logger::info("PLAY_MEDIA", "pending base=STOP");
        return true;
    }
    return false;
}

bool App::queuePlayMediaUrl(const String& url) {
    if (!_playMediaAvailable) {
        Logger::warn("PLAY_MEDIA", "Request rejected: runtime unavailable");
        return false;
    }
    if (url.length() < 8 || url.length() > 512 ||
        (!url.startsWith("http://") && !url.startsWith("https://"))) {
        Logger::warn("PLAY_MEDIA", "Request rejected: invalid URL");
        return false;
    }
    _pendingPlayMediaUrl = url;
    return true;
}

void App::startPendingPlayMedia() {
    if (_pendingPlayMediaUrl.isEmpty()) return;
    const String url = _pendingPlayMediaUrl;
    _pendingPlayMediaUrl = "";
    startPlayMedia(url);
}

void App::startPlayMedia(const String& url) {
    if (!_playMediaAvailable) return;
    if (_playMediaOverrideActive) {
        Logger::info("PLAY_MEDIA", "replacing current URL");
        _playMedia.stop();
    } else {
        const auto state = StateStore::instance().snapshot();
        _playMediaSnapshot.valid = true;
        _playMediaSnapshot.baseSource = _sourceIntent;
        _playMediaSnapshot.stationId = _radio.selectedStationId();
        _playMediaSnapshot.radioWasPlaying =
            _sourceIntent == AudioSource::Radio &&
            (_radio.isRunning() || state.playback == PlaybackState::Playing);
        _playMediaSnapshot.radioWasPaused =
            _sourceIntent == AudioSource::Radio &&
            state.playback == PlaybackState::Paused;
        _playMediaSnapshot.btWasPlaying =
            _sourceIntent == AudioSource::Bluetooth && state.bluetoothPlaying;
        _playMediaSnapshot.logicalVolume = state.volume;

        if (_sourceIntent == AudioSource::Radio ||
            _radio.state() != RadioState::Idle) {
            _radio.stop();
            _radioSession = false;
        } else if (_sourceIntent == AudioSource::Bluetooth &&
                   state.bluetoothPlaying &&
                   _btLink.moduleState() == BtModuleState::Ready) {
            _btLink.pause();
        }
        if (_audioOutput.owner() != AudioOutputOwner::None) {
            Logger::warn("PLAY_MEDIA", "Start blocked: audio owner retained");
            restoreAfterPlayMedia();
            return;
        }
        _playMediaOverrideActive = true;
        returnHome();
        auto overrideState = StateStore::instance().snapshot();
        overrideState.playMediaActive = true;
        overrideState.lastMessage = "PLAY_MEDIA_STARTING";
        StateStore::instance().update(overrideState);
        _yoRadioOn = true;
    }

    if (!_playMedia.playUrl(url)) {
        finishPlayMedia("start error");
        return;
    }
    auto state = StateStore::instance().snapshot();
    state.playMediaActive = true;
    state.lastMessage = "PLAY_MEDIA_PLAYING";
    StateStore::instance().update(state);
}

void App::cancelPlayMedia(const char* reason) {
    if (_playMediaOverrideActive) finishPlayMedia(reason);
}

void App::finishPlayMedia(const char* reason) {
    if (!_playMediaOverrideActive) return;
    Logger::info("PLAY_MEDIA", String("finish: ") + reason);
    _playMedia.stop();
    _playMediaOverrideActive = false;
    auto state = StateStore::instance().snapshot();
    state.playMediaActive = false;
    state.lastMessage = "PLAY_MEDIA_FINISHED";
    StateStore::instance().update(state);
    restoreAfterPlayMedia();
}

void App::restoreAfterPlayMedia() {
    if (!_playMediaSnapshot.valid) return;
    const PlayMediaSnapshot snapshot = _playMediaSnapshot;
    _playMediaSnapshot.valid = false;
    if (snapshot.baseSource == AudioSource::Radio) {
        const Station* station = _radio.stations().getById(snapshot.stationId);
        if (!_radioAvailable || !station ||
            _config.config().audio.outputType != OutputType::PCM5102A) {
            Logger::warn("PLAY_MEDIA", "RADIO restore unavailable; using STOP");
            selectSource(AudioSource::Stop);
            return;
        }
        _sourceIntent = AudioSource::Radio;
        _wifi.setRadioNetworkNeeded(true);
        if (!_radio.selectStation(snapshot.stationId)) {
            Logger::warn("PLAY_MEDIA", "RADIO restore selection failed; using STOP");
            selectSource(AudioSource::Stop);
            return;
        }
        auto state = StateStore::instance().snapshot();
        state.audioSource = AudioSource::Radio;
        state.radioStation = station->name;
        state.radioArtist = "";
        state.radioTitle = "";
        state.radioBitrate = 0;
        state.radioCodec = "";
        state.bluetoothArtist = "";
        state.bluetoothTitle = "";
        if (snapshot.radioWasPlaying) {
            state.playback = PlaybackState::Stop;
            state.lastMessage = "RADIO_RESTORING";
            StateStore::instance().update(state);
            if (state.wifiConnected && WiFi.status() == WL_CONNECTED)
                startRadio(snapshot.stationId);
            else Logger::info("RADIO", "waiting for network after PLAY_MEDIA");
        } else {
            _radio.stop();
            _radioSession = false;
            state.playback = snapshot.radioWasPaused
                ? PlaybackState::Paused : PlaybackState::Stop;
            state.lastMessage = snapshot.radioWasPaused
                ? "RADIO_PAUSED" : "RADIO_SELECTED";
            StateStore::instance().update(state);
        }
        Logger::info("PLAY_MEDIA", String("restored RADIO station=") +
            snapshot.stationId +
            (snapshot.radioWasPlaying ? " playing" : " paused"));
        return;
    }
    if (snapshot.baseSource == AudioSource::Bluetooth) {
        if (selectSource(AudioSource::Bluetooth)) {
            if (snapshot.btWasPlaying &&
                _btLink.moduleState() == BtModuleState::Ready)
                _btLink.play();
            Logger::info("PLAY_MEDIA", "restored BT");
            return;
        }
        Logger::warn("PLAY_MEDIA", "BT audio unavailable after restore; using STOP");
    }
    selectSource(AudioSource::Stop);
    Logger::info("PLAY_MEDIA", "restored STOP");
}

bool App::selectSource(AudioSource source, uint16_t stationId, bool startPlayback) {
    if (_playMediaOverrideActive)
        return selectSourceDuringPlayMedia(source, stationId);
    if (source == AudioSource::Radio) {
        if (!_radioAvailable ||
            _config.config().audio.outputType != OutputType::PCM5102A) {
            Logger::warn("SOURCE", "RADIO unavailable");
            return false;
        }
        const uint16_t selected = _radio.selectedStationId();
        const int configured = _config.config().radio.defaultStation;
        const uint16_t id = stationId ? stationId :
            _radio.stations().isValidId(selected) ? selected :
            configured > 0 ? static_cast<uint16_t>(configured) : 0;
        if (!_radio.stations().isValidId(id)) {
            Logger::warn("RADIO", String("station invalid id=") + id);
            return false;
        }
        const auto previousState = StateStore::instance().snapshot();
        if (_sourceIntent == AudioSource::Bluetooth &&
            previousState.bluetoothPlaying &&
            _btLink.moduleState() == BtModuleState::Ready) {
            _btLink.pause();
        }
        if (_sourceIntent == AudioSource::Radio &&
            _radio.state() == RadioState::Playing &&
            _radio.playingStationId() == id) return true;
        if (!_radio.selectStation(id)) {
            Logger::error("SOURCE", "RADIO selection failed");
            return false;
        }
        _sourceIntent = AudioSource::Radio;
        _yoRadioOn = true;
        _wifi.setRadioNetworkNeeded(true);
        auto s = StateStore::instance().snapshot();
        s.audioSource = AudioSource::Radio;
        s.playback = PlaybackState::Stop;
        s.bluetoothUnavailableNotice = false;
        s.bluetoothArtist = "";
        s.bluetoothTitle = "";
        s.lastMessage = "RADIO_SELECTED";
        StateStore::instance().update(s);
        if (startPlayback && s.wifiConnected && WiFi.status() == WL_CONNECTED) {
            startRadio(id);
        } else if (!startPlayback) {
            _radio.stop();
            _radioSession = false;
            s.playback = PlaybackState::Stop;
            s.radioStation = _radio.stations().getById(id)->name;
            s.radioArtist = "";
            s.radioTitle = "";
            s.radioBitrate = 0;
            s.radioCodec = "";
            StateStore::instance().update(s);
        }
        else Logger::info("RADIO", "waiting for network");
        return true;
    }
    if (source == AudioSource::Bluetooth) {
        if (!_config.features().bluetoothEnabled) {
            Logger::info("SOURCE", "BT selection ignored: feature disabled");
            return false;
        }
        if (_sourceIntent == AudioSource::Radio ||
            _radio.state() != RadioState::Idle) {
            stopRadio();
        }
        auto s = StateStore::instance().snapshot();
        const bool btReady = _btLink.moduleState() == BtModuleState::Ready;
        if (btReady && s.bluetoothPlaying) _btLink.pause();
        s.bluetoothUnavailableNotice = !btReady;
        s.lastMessage = !_config.features().bluetoothEnabled ? "BT_DISABLED" :
            !btReady ?
                "BT_MODULE_UNAVAILABLE" : "BT_AUDIO_UNAVAILABLE";
        _sourceIntent = AudioSource::Bluetooth;
        _wifi.setRadioNetworkNeeded(false);
        s.audioSource = AudioSource::Bluetooth;
        s.playback = PlaybackState::Stop;
        s.radioArtist = "";
        s.radioTitle = "";
        s.radioBitrate = 0;
        s.radioCodec = "";
        s.bluetoothArtist = "";
        s.bluetoothTitle = "";
        StateStore::instance().update(s);
        Logger::info("SOURCE", String("BT selected") +
            (btReady ? "" : " (audio unavailable)"));
        return true;
    }
    if (source == AudioSource::Stop) {
        if (_sourceIntent == AudioSource::Radio ||
            _radio.state() != RadioState::Idle) stopRadio();
        _sourceIntent = AudioSource::Stop;
        _wifi.setRadioNetworkNeeded(false);
        auto s = StateStore::instance().snapshot();
        s.audioSource = AudioSource::Stop;
        s.playback = PlaybackState::Stop;
        s.bluetoothUnavailableNotice = false;
        s.radioArtist = "";
        s.radioTitle = "";
        s.radioBitrate = 0;
        s.radioCodec = "";
        s.bluetoothArtist = "";
        s.bluetoothTitle = "";
        s.lastMessage = "SOURCE_STOP";
        StateStore::instance().update(s);
        Logger::info("SOURCE", "STOP selected");
        return true;
    }
    return false;
}

void App::handleStationRemoved(uint16_t id) {
    if (_radio.selectedStationId() != id && _radio.playingStationId() != id)
        return;

    _radio.stop();
    _radioSession = false;
    const Station* replacement = _radio.stations().first();
    if (replacement && _radioAvailable) {
        // Keep a valid selection, but deletion must never auto-start playback.
        _radio.selectStation(replacement->id);
        _radio.stop();
    } else _radio.clearSelection();

    auto state = StateStore::instance().snapshot();
    state.playback = PlaybackState::Stop;
    state.radioStation = replacement ? replacement->name : "";
    state.radioArtist = "";
    state.radioTitle = "";
    state.radioBitrate = 0;
    state.radioCodec = "";
    state.lastMessage = replacement ? "RADIO_SELECTED" : "RADIO_EMPTY";
    StateStore::instance().update(state);
    if (_sourceIntent == AudioSource::Radio)
        _wifi.setRadioNetworkNeeded(replacement != nullptr);
    Logger::info("STATIONS", String("active station removed id=") + id +
        " replacement=" + (replacement ? String(replacement->id) : String(0)));
}

void App::handleEncoderDoubleClick() {
    if (_playMediaOverrideActive) {
        const bool radioEnabled = _config.features().radioEnabled;
        const bool bluetoothEnabled = _config.features().bluetoothEnabled;
        if (!radioEnabled || !bluetoothEnabled || _sourceIntent == AudioSource::Stop) {
            Logger::info("UI", "SOURCE double click ignored: only one source enabled");
            return;
        }
        const AudioSource target = _sourceIntent == AudioSource::Radio
            ? AudioSource::Bluetooth : AudioSource::Radio;
        Logger::info("UI", "double click during PLAY_MEDIA; pending source=" +
            String(target == AudioSource::Radio ? "RADIO" : "BT"));
        selectSource(target, 0, false);
        returnHome();
        return;
    }

    const bool radioEnabled = _config.features().radioEnabled;
    const bool bluetoothEnabled = _config.features().bluetoothEnabled;
    if (!radioEnabled || !bluetoothEnabled || _sourceIntent == AudioSource::Stop) {
        Logger::info("UI", "SOURCE double click ignored: only one source enabled");
        return;
    }

    const AudioSource target = _sourceIntent == AudioSource::Radio
        ? AudioSource::Bluetooth : AudioSource::Radio;
    Logger::info("UI", String("SOURCE ") +
        (_sourceIntent == AudioSource::Radio ? "RADIO -> BT" : "BT -> RADIO"));
    if (selectSource(target, 0, false)) returnHome();
}
void App::processRadioTestCommands() {
    // Bounded, newline-terminated Serial source commands on the App task.
    for (int budget = 0; budget < 24 && Serial.available(); ++budget) {
        const char character = static_cast<char>(Serial.read());
        if (character == '\r') continue;
        if (character == '\n') {
            _radioCommand[_radioCommandLength] = '\0';
            if (!_radioCommandOverflow) {
                if (strcmp(_radioCommand, "source stop") == 0) {
                    selectSource(AudioSource::Stop);
                } else if (strcmp(_radioCommand, "source bt") == 0) {
                    selectSource(AudioSource::Bluetooth);
                } else if (strcmp(_radioCommand, "source radio") == 0) {
                    selectSource(AudioSource::Radio);
                }
#if VOXONE_RADIO_TEST_CONTROLS
                else if (strcmp(_radioCommand, "radio start") == 0) {
                    if (!selectSource(AudioSource::Radio, 1))
                        Logger::warn("RADIO", "Serial start rejected");
                } else if (strcmp(_radioCommand, "radio stop") == 0) {
                    selectSource(AudioSource::Stop);
                }
#endif
            }
            _radioCommandLength = 0;
            _radioCommandOverflow = false;
        } else if (_radioCommandLength < sizeof(_radioCommand) - 1) {
            _radioCommand[_radioCommandLength++] = character;
        } else {
            _radioCommandOverflow = true;
        }
    }
}

void App::updateRemoteBluetooth() {
    _btLink.loop();
    const BtModuleState module = _btLink.moduleState();
    const BtLinkStatus& bt = _btLink.status();
    auto s = StateStore::instance().snapshot();
    if (module != _lastBtModuleState) {
        _lastBtModuleState = module;
        if (module == BtModuleState::Ready) {
            Logger::info("BT", "External module READY; PCM RX still unavailable");
            _btLink.setVolume(s.volume);
        } else if (module == BtModuleState::Unavailable) {
            Logger::info("BT", "External module unavailable");
            if (s.bluetoothUnavailableNotice)
                s.lastMessage = "BT_MODULE_UNAVAILABLE";
        }
    }
    const bool ready = module == BtModuleState::Ready;
    const bool connected = ready && bt.connected;
    const bool playing = connected &&
        bt.playback == BtPlaybackState::Playing;
    const bool connectionChanged = connected != s.bluetoothConnected;
    const bool newConnection = connected && connectionChanged;
    const String artist = connected && playing && !newConnection
        ? String(bt.artist) : String();
    const String title = connected && playing && !newConnection
        ? String(bt.title) : String();
    const String peerName = connected ? String(bt.deviceName) : String();
    if (s.bluetoothModuleState != module ||
        s.bluetoothStarted != ready ||
        s.bluetoothConnected != connected ||
        s.bluetoothPlaying != playing ||
        s.bluetoothPeerName != peerName ||
        s.bluetoothArtist != artist ||
        s.bluetoothTitle != title) {
        s.bluetoothModuleState = module;
        s.bluetoothStarted = ready;
        s.bluetoothConnected = connected;
        s.bluetoothPlaying = playing;
        s.bluetoothPeerName = peerName;
        s.bluetoothArtist = artist;
        s.bluetoothTitle = title;
        s.bluetoothOwnership = playing ? BluetoothOwnershipState::Playing :
            connected ? BluetoothOwnershipState::ConnectedIdle :
                BluetoothOwnershipState::Disconnected;
        s.bluetoothReconnectGrace = false;
        StateStore::instance().update(s);
    }
    // The future source policy must not select BT until I2S RX is tested.
    if (bt.playingEdge) _btLink.consumePlayingEdge();

    if (connectionChanged) {
        if (connected) {
            Logger::info("SOURCE", "BT CONNECTED -> BT selected/stopped");
            selectSource(AudioSource::Bluetooth, 0, false);
        } else if (_config.features().radioEnabled) {
            Logger::info("SOURCE", "BT DISCONNECTED -> RADIO selected/stopped");
            if (!selectSource(AudioSource::Radio, 0, false))
                selectSource(AudioSource::Stop, 0, false);
        } else {
            Logger::info("SOURCE", "BT DISCONNECTED -> STOP selected");
            selectSource(AudioSource::Stop, 0, false);
        }
    }
}

void App::mqttSetVolume(int value) {
    auto state = StateStore::instance().snapshot();
    state.volume = constrain(value, 0, _config.maxVolume());
    _audioOutput.setVolume(state.volume);
    if (_radioAvailable) _radio.setVolume(state.volume);
    if (_btLink.moduleState() == BtModuleState::Ready)
        _btLink.setVolume(state.volume);
    StateStore::instance().update(state);
    _config.saveVolume(state.volume);
}

void App::mqttPlay() {
    if (_playMediaOverrideActive && _playMediaSnapshot.valid) {
        if (_playMediaSnapshot.baseSource == AudioSource::Radio) {
            _playMediaSnapshot.radioWasPlaying = true;
            _playMediaSnapshot.radioWasPaused = false;
        } else if (_playMediaSnapshot.baseSource == AudioSource::Bluetooth) {
            _playMediaSnapshot.btWasPlaying = true;
        }
        return;
    }
    if (_sourceIntent == AudioSource::Radio) resumeRadio();
    else if (_sourceIntent == AudioSource::Bluetooth &&
               _btLink.moduleState() == BtModuleState::Ready)
        _btLink.play();
}

void App::mqttPause() {
    if (_playMediaOverrideActive) {
        cancelPlayMedia("pause/cancel");
        return;
    }
    if (_sourceIntent == AudioSource::Radio) pauseRadio();
    else if (_sourceIntent == AudioSource::Bluetooth &&
        _btLink.moduleState() == BtModuleState::Ready)
        _btLink.pause();
}

void App::mqttToggle() {
    if (_playMediaOverrideActive) {
        cancelPlayMedia("toggle/cancel");
        return;
    }
    toggleBasePlayback();
}

void App::pauseRadio() {
    if (_sourceIntent != AudioSource::Radio ||
        (_radio.state() == RadioState::Idle && !_radio.isRunning())) return;
    Logger::info("RADIO", "pause requested");
    const Station* station = _radio.stations().getById(_radio.selectedStationId());
    _radio.stop();
    _radioSession = false;
    auto state = StateStore::instance().snapshot();
    state.audioSource = AudioSource::Radio;
    state.playback = PlaybackState::Paused;
    state.radioStation = station ? station->name : String();
    state.radioArtist = "";
    state.radioTitle = "";
    state.radioBitrate = 0;
    state.radioCodec = "";
    state.lastMessage = "RADIO_PAUSED";
    StateStore::instance().update(state);
}

void App::resumeRadio() {
    if (_sourceIntent != AudioSource::Radio || _radio.isRunning()) return;
    const uint16_t id = _radio.selectedStationId();
    if (!_radio.stations().isValidId(id)) return;
    Logger::info("RADIO", "resume requested");
    startRadio(id);
}

void App::toggleBasePlayback() {
    const auto state = StateStore::instance().snapshot();
    if (_sourceIntent == AudioSource::Stop) return;
    if (_sourceIntent == AudioSource::Radio) {
        if (_radio.isRunning()) pauseRadio();
        else resumeRadio();
    } else if (_sourceIntent == AudioSource::Bluetooth &&
               _config.features().bluetoothEnabled) {
        if (_btLink.moduleState() == BtModuleState::Ready &&
            state.bluetoothConnected) {
            if (state.bluetoothPlaying) _btLink.pause();
            else _btLink.play();
        } else {
            Logger::info("BT", "BT play/pause unavailable");
        }
    }
}

void App::mqttNext() {
    if (_sourceIntent == AudioSource::Radio) {
        const Station* station = _radio.stations().next(_radio.selectedStationId());
        if (station) selectSource(AudioSource::Radio, station->id);
    } else if (_sourceIntent == AudioSource::Bluetooth &&
               _btLink.moduleState() == BtModuleState::Ready)
        _btLink.next();
}

void App::mqttPrevious() {
    if (_sourceIntent == AudioSource::Radio) {
        const Station* station = _radio.stations().previous(_radio.selectedStationId());
        if (station) selectSource(AudioSource::Radio, station->id);
    } else if (_sourceIntent == AudioSource::Bluetooth &&
               _btLink.moduleState() == BtModuleState::Ready)
        _btLink.previous();
}

void App::mqttYoRadioStop() {
    if (_playMediaOverrideActive) {
        cancelPlayMedia("stop/cancel");
        _yoRadioDuplicateStopArmed = true;
        _yoRadioDuplicateStopUntil =
            millis() + YORADIO_DUPLICATE_STOP_GUARD_MS;
        return;
    }
    if (_yoRadioDuplicateStopArmed) {
        _yoRadioDuplicateStopArmed = false;
        if (static_cast<int32_t>(_yoRadioDuplicateStopUntil - millis()) >= 0) {
            Logger::info("MQTT", "yoRadio duplicate stop ignored after PLAY_MEDIA");
            return;
        }
    }
    if (_sourceIntent == AudioSource::Radio) pauseRadio();
    else if (_sourceIntent == AudioSource::Bluetooth) mqttPause();
}

void App::mqttYoRadioTurnOff() {
    if (!_yoRadioOn) return;
    _yoRadioRestoreSource = _sourceIntent;
    _yoRadioRestoreStationId = _radio.selectedStationId();
    _yoRadioOn = false;
    selectSource(AudioSource::Stop);
    if (_playMediaOverrideActive) cancelPlayMedia("turnoff");
    Logger::info("MQTT", "yoRadio player OFF");
}

void App::mqttYoRadioTurnOn() {
    if (_yoRadioOn) return;
    _yoRadioOn = true;
    if (_yoRadioRestoreSource == AudioSource::Radio &&
        _radio.stations().isValidId(_yoRadioRestoreStationId))
        selectSource(AudioSource::Radio, _yoRadioRestoreStationId);
    Logger::info("MQTT", "yoRadio player ON");
}

void App::touchOverlayTimeout() {
    _overlayActivityMs = millis();
}

void App::enterMode(UiMode mode) {
    if (_uiMode == mode) {
        if (mode != UiMode::Home) touchOverlayTimeout();
        return;
    }
    _uiMode = mode;
    _btNavArtistPending = false;
    if (mode != UiMode::Home) touchOverlayTimeout();
    const char* label = mode == UiMode::Home ? "HOME" :
        mode == UiMode::Volume ? "VOLUME" :
        mode == UiMode::BtTrackNav ? "BT_NAV" : "RADIO_LIST";
    Logger::info("UI", String("mode=") + label);
}

void App::returnHome() {
    enterMode(UiMode::Home);
}

void App::processCommands() {
    PerfScope commandsPerf(PerfArea::Commands);
    Command cmd;

    // A bounded drain keeps Wi-Fi, display and UART polling responsive
    // even when another task keeps adding commands.
    for (int processed = 0;
         processed < 16 && CommandQueue::instance().pop(cmd);
         ++processed) {
        if (cmd.source == CommandSource::Bluetooth &&
            _btLink.moduleState() != BtModuleState::Ready) {
            continue;
        }
        if (cmd.source == CommandSource::Encoder &&
            !(_config.features().encoderEnabled && Board::HAS_ENCODER)) {
            Logger::warn("ENCODER", "Command ignored: encoder disabled");
            continue;
        }

        auto s =
            StateStore::instance().snapshot();

        switch (cmd.type) {
            case CommandType::EncoderRotation:
            case CommandType::VolumeDelta: {
                if (cmd.type == CommandType::EncoderRotation &&
                    _uiMode == UiMode::RadioList) {
                    touchOverlayTimeout();
                    const int step = _config.config().encoder.volumeStep;
                    const int ticks = abs(cmd.value) / step;
                    for (int i = 0; i < ticks; ++i) {
                        const Station* station = cmd.value > 0
                            ? _radio.stations().next(_radioHighlightedStationId)
                            : _radio.stations().previous(_radioHighlightedStationId);
                        if (!station) break;
                        _radioHighlightedStationId = station->id;
                    }
                    continue; // Highlight only; keep the current stream unchanged.
                }
                if (cmd.type == CommandType::EncoderRotation &&
                    _uiMode == UiMode::BtTrackNav) {
                    touchOverlayTimeout();
                    const int step = _config.config().encoder.volumeStep;
                    const int ticks = abs(cmd.value) / step;
                    if (ticks > 0) _btNavArtistPending = true;
                    for (int i = 0; i < ticks; ++i) {
                        if (_btLink.moduleState() != BtModuleState::Ready) break;
                        if (cmd.value > 0) _btLink.next();
                        else _btLink.previous();
                    }
                    continue; // Encoder rotation never changes volume in BT NAV.
                }
                const int oldVolume = s.volume;
                s.volume = constrain(
                    s.volume + cmd.value,
                    0,
                    _config.maxVolume()
                );

                if (_btLink.moduleState() == BtModuleState::Ready)
                    _btLink.setVolume(s.volume);

                _volumeDirty = true;
                _volumeSaveDue =
                    millis() + 1500;
                _audioOutput.setVolume(s.volume);
                if (_radioAvailable) _radio.setVolume(s.volume);
                if (cmd.type == CommandType::EncoderRotation ||
                    s.volume != oldVolume) {
                    if (_uiMode == UiMode::Home) enterMode(UiMode::Volume);
                    else if (_uiMode == UiMode::Volume) touchOverlayTimeout();
                }
                break;
            }

            case CommandType::SetVolumeAbsolute: {
                const int oldVolume = s.volume;
                s.volume = constrain(
                    cmd.value,
                    0,
                    _config.maxVolume()
                );

                if (_btLink.moduleState() == BtModuleState::Ready &&
                    cmd.source != CommandSource::Bluetooth) {
                    _btLink.setVolume(s.volume);
                }

                _volumeDirty = true;
                _volumeSaveDue =
                    millis() + 1500;
                _audioOutput.setVolume(s.volume);
                if (_radioAvailable) _radio.setVolume(s.volume);
                if (s.volume != oldVolume) {
                    if (_uiMode == UiMode::Home) enterMode(UiMode::Volume);
                    else if (_uiMode == UiMode::Volume) touchOverlayTimeout();
                }
                break;
            }

            case CommandType::TogglePlayStop:
                if (_playMediaOverrideActive) {
                    cancelPlayMedia("encoder short click");
                    returnHome();
                    continue;
                }
                if (cmd.source == CommandSource::Encoder &&
                    _uiMode == UiMode::RadioList) {
                    if (_radio.stations().isValidId(_radioHighlightedStationId))
                        selectSource(AudioSource::Radio, _radioHighlightedStationId);
                    returnHome();
                    continue;
                }
                if (cmd.source == CommandSource::Encoder &&
                    _uiMode != UiMode::Home) {
                    returnHome();
                    continue;
                }
                if (_sourceIntent == AudioSource::Radio) {
                    Logger::info("UI", "short click source=RADIO");
                    toggleBasePlayback();
                    continue;
                }
                if (_sourceIntent == AudioSource::Stop) {
                    Logger::info("UI", "short click ignored: source=STOP");
                    continue;
                }
                toggleBasePlayback();
                continue;

            case CommandType::EncoderDoubleClick:
                if (_playMediaOverrideActive) {
                    handleEncoderDoubleClick();
                    continue;
                }
                handleEncoderDoubleClick();
                continue;

            case CommandType::EncoderLongPress:
                if (_playMediaOverrideActive) {
                    Logger::debug("UI", "long press ignored during PLAY_MEDIA");
                    returnHome();
                    continue;
                }
                if (_uiMode != UiMode::Home) {
                    returnHome();
                } else if (s.audioSource == AudioSource::Bluetooth &&
                           _btLink.moduleState() == BtModuleState::Ready) {
                    enterMode(UiMode::BtTrackNav);
                } else if (_sourceIntent == AudioSource::Radio && _radioAvailable) {
                    const StationStore& stations = _radio.stations();
                    const uint16_t playing = _radio.playingStationId();
                    const uint16_t selected = _radio.selectedStationId();
                    const Station* initial = stations.getById(playing);
                    if (!initial) initial = stations.getById(selected);
                    if (!initial) initial = stations.first();
                    if (initial) {
                        _radioHighlightedStationId = initial->id;
                        enterMode(UiMode::RadioList);
                    }
                }
                continue;

            case CommandType::SetPlay:
                if (_btLink.moduleState() == BtModuleState::Ready) _btLink.play();
                break;

            case CommandType::Pause:
                if (_btLink.moduleState() == BtModuleState::Ready) _btLink.pause();
                break;

            case CommandType::SetStop:
                if (_playMediaOverrideActive) {
                    cancelPlayMedia("stop command");
                    continue;
                }
                if (_sourceIntent == AudioSource::Radio) {
                    selectSource(AudioSource::Stop);
                    continue;
                }
                if (_btLink.moduleState() == BtModuleState::Ready) _btLink.pause();
                break;

            case CommandType::Next:
                if (_btLink.moduleState() == BtModuleState::Ready) _btLink.next();
                break;

            case CommandType::Previous:
                if (_btLink.moduleState() == BtModuleState::Ready) _btLink.previous();
                break;
        }

        StateStore::instance().update(s);
    }

    if (
        _volumeDirty &&
        !_web.restartPending() &&
        static_cast<int32_t>(
            millis() -
            _volumeSaveDue
        ) >= 0
    ) {
        _volumeDirty = false;

        _config.saveVolume(
            StateStore::instance()
                .snapshot()
                .volume
        );

        Logger::debug(
            "CONFIG",
            "Volume saved"
        );
    }
}

void App::loop() {
    PerfDiagnostics::beginLoopIteration();
    {
        PerfScope appPerf(PerfArea::AppControl);
        processRadioTestCommands();
    }
    {
        PerfScope playMediaPerf(PerfArea::PlayMedia);
        startPendingPlayMedia();
        if (_playMediaOverrideActive) {
            _playMedia.loop();
            if (_playMedia.state() == PlayMediaState::Completed)
                finishPlayMedia("EOF");
            else if (_playMedia.state() == PlayMediaState::Error)
                finishPlayMedia(_playMedia.lastError() ?
                    _playMedia.lastError() : "stream error");
        }
    }
    {
        PerfScope radioPerf(PerfArea::Radio);
        if (_radioAvailable) {
            if (_sourceIntent == AudioSource::Radio && WiFi.status() != WL_CONNECTED)
                _radio.onNetworkLost();
            _radio.loop();
            if (_radioSession && !_radio.isRunning()) {
                auto radioState = StateStore::instance().snapshot();
                if (radioState.playback != PlaybackState::Stop) {
                    radioState.playback = PlaybackState::Stop;
                    radioState.lastMessage =
                        _radio.state() == RadioState::WaitingForNetwork
                        ? "RADIO_WAITING" : "RADIO_ERROR";
                    StateStore::instance().update(radioState);
                }
            }
        }
    }
    if (_config.features().encoderEnabled && Board::HAS_ENCODER) {
        PerfScope encoderPerf(PerfArea::Encoder);
        _encoder.loop();
    }

    if (_config.features().bluetoothEnabled) {
        PerfScope btPerf(PerfArea::BtLink);
        updateRemoteBluetooth();
    }
    processCommands();
    {
        PerfScope appPerf(PerfArea::AppControl);
        if (_uiMode == UiMode::BtTrackNav &&
            StateStore::instance().snapshot().audioSource !=
                AudioSource::Bluetooth)
            returnHome();
        if (_uiMode == UiMode::RadioList && _sourceIntent != AudioSource::Radio)
            returnHome();
        if (_uiMode != UiMode::Home) {
            const uint32_t timeoutMs = _uiMode == UiMode::Volume
                ? AppConfig::VOLUME_SCREEN_TIMEOUT_MS
                : _config.config().ui.navigationTimeoutMs;
            if (millis() - _overlayActivityMs >= timeoutMs) returnHome();
        }
    }

    {
        PerfScope wifiPerf(PerfArea::Wifi);
        _wifi.loop();
    }
    {
        PerfScope timePerf(PerfArea::Time);
        if (_wifi.consumeConnectedTransition() && _timeStarted)
            _time.onWifiConnected();
    }
    _web.loop();
    _mqtt.loop();
    {
        PerfScope retryPerf(PerfArea::RadioControl);
        if (_radioAvailable && !_playMediaOverrideActive &&
            _sourceIntent == AudioSource::Radio &&
            StateStore::instance().snapshot().wifiConnected &&
            WiFi.status() == WL_CONNECTED &&
            (_radio.state() == RadioState::WaitingForNetwork ||
             _radio.retryReady(millis()))) {
            if (_radio.state() == RadioState::Error)
                Logger::info("RADIO", "retry starting");
            startRadio(_radio.selectedStationId());
        }
    }
    {
        PerfScope timePerf(PerfArea::Time);
        if (_timeStarted) _time.loop();
    }

    if (_display) {
        _display->finishStartup();
        _display->requestUpdate(
            _uiMode, _btNavArtistPending, _radio.stations(),
            _radioHighlightedStationId, _radio.playingStationId());
    }
    // Take a second cheap AB sample after network/display work.
    if (_config.features().encoderEnabled && Board::HAS_ENCODER) {
        PerfScope encoderPerf(PerfArea::Encoder);
        _encoder.loop();
    }

    {
        PerfScope diagnosticsPerf(PerfArea::Diagnostics);
        PerfDiagnostics::reportIfDue(_radioAvailable && _radio.isRunning());
    }
    {
        PerfScope yieldPerf(PerfArea::Yield);
        delay(0);
    }
    PerfDiagnostics::endLoopIteration();
}
