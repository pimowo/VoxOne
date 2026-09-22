#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <libhelix-mp3/mp3dec.h>

#include "../audio/AudioOutputManager.h"
#include "StationStore.h"

// All calls (including stop) run on the App task. No worker or PCM callbacks.
// App suspends Bluetooth before start and resumes it only after lease release.
enum class RadioState : uint8_t { Idle, WaitingForNetwork, Starting, Playing, Error };

class RadioService {
public:
    RadioService() = default;
    RadioService(const RadioService&) = delete;
    RadioService& operator=(const RadioService&) = delete;

    bool begin(AudioOutputManager& output);
    bool selectStation(uint16_t id);
    bool nextStation();
    bool previousStation();
    bool startSelected();
    void onNetworkLost();
    bool retryReady(uint32_t now) const;
    void stop();
    void clearSelection();
    StationStore& stations() { return _stations; }
    const StationStore& stations() const { return _stations; }
    RadioState state() const { return _state; }
    uint16_t selectedStationId() const { return _selectedStationId; }
    uint16_t playingStationId() const { return _playingStationId; }
    uint32_t generation() const { return _generation; }
    void loop();
    bool isRunning() const { return _running; }
    void setVolume(int volume) { if (_manager) _manager->setVolume(volume); }
    const char* lastError() const { return _error; }

private:
    static constexpr size_t INPUT_BYTES = 8192;
    static constexpr size_t PCM_SAMPLES = 2304;
    static constexpr size_t MAX_METADATA_CAPTURE = 512;
    static constexpr size_t MAX_STREAM_TITLE_BYTES = 256;
    static constexpr uint32_t MAX_METAINT = 1024 * 1024;
    static constexpr uint32_t STALL_MS = 10000;
    static constexpr uint8_t MAX_REDIRECTS = 5;
    static constexpr uint32_t CONNECT_BUDGET_MS = 15000;
    enum class IcyStage : uint8_t { Audio, Length, Metadata };

    StationStore _stations;
    RadioState _state = RadioState::Idle;
    uint16_t _selectedStationId = 0;
    uint16_t _playingStationId = 0;
    uint32_t _generation = 0;
    uint32_t _retryGeneration = 0;
    uint32_t _retryDue = 0;
    uint8_t _retryAttempt = 0;
    bool _retryScheduled = false;
    AudioOutputManager* _manager = nullptr;
    TaskHandle_t _appTask = nullptr;
    // Both clients must outlive HTTPClient (members are destroyed in reverse order).
    NetworkClient _plainClient;
    NetworkClientSecure _secureClient;
    NetworkClient* _activeClient = nullptr;
    bool _activeSecure = false;
    HTTPClient _http;
    Print* _output = nullptr;
    HMP3Decoder _decoder = nullptr;
    uint8_t* _input = nullptr;
    int16_t* _pcm = nullptr;
    size_t _used = 0;
    size_t _pcmBytes = 0;
    size_t _pcmOffset = 0;
    size_t _skipped = 0;
    int _bodyRemaining = -1;
    uint32_t _lastData = 0;
    uint32_t _lastFrame = 0;
    uint32_t _sampleRate = 0;
    uint8_t _reservoirMisses = 0;
    bool _bufferStarvationActive = false;
    bool _decoderWaitingActive = false;
    String _icyName;
    String _codec;
    String _streamTitle;
    uint32_t _icyBitrate = 0;
    uint32_t _icyMetaint = 0;
    uint32_t _audioUntilMetadata = 0;
    uint16_t _metadataRemaining = 0;
    size_t _metadataUsed = 0;
    bool _metadataOverflow = false;
    IcyStage _icyStage = IcyStage::Audio;
    char _metadata[MAX_METADATA_CAPTURE + 1] = {};

    bool _lease = false;
    bool _attached = false;
    bool _running = false;
    bool _fault = false;
    const char* _error = nullptr;

    bool onAppTask() const;
    bool start(const char* url);
    bool openStream(const String& initialUrl);
    void closeTransport();
    void stopStream();
    void cancelRetry();
    void fail(const char* error);
    void consume(size_t bytes);
    void resetIcyBody();
    size_t filterIcyAudio(uint8_t* data, size_t count, uint32_t sessionGeneration);
    void publishIcyMetadata(uint32_t sessionGeneration);
    void clearTrackMetadata();
};
