#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <libhelix-mp3/mp3dec.h>

#include "../audio/AudioOutputManager.h"

enum class PlayMediaState : uint8_t {
    Idle,
    Starting,
    Playing,
    Completed,
    Error
};

// Direct HTTP MP3 player used only for the temporary PLAY_MEDIA override.
// All lifecycle and loop calls belong to the App task.
class PlayMediaService {
public:
    PlayMediaService() = default;
    PlayMediaService(const PlayMediaService&) = delete;
    PlayMediaService& operator=(const PlayMediaService&) = delete;

    bool begin(AudioOutputManager& output);
    bool playUrl(const String& url);
    void stop();
    void loop();

    bool isPlaying() const { return _state == PlayMediaState::Playing; }
    PlayMediaState state() const { return _state; }
    const char* lastError() const { return _error; }

private:
    static constexpr size_t MAX_URL_BYTES = 512;
    static constexpr size_t INPUT_BYTES = 8192;
    static constexpr size_t PCM_SAMPLES = 2304;
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 3000;
    static constexpr uint32_t READ_TIMEOUT_MS = 1000;
    static constexpr uint32_t STALL_TIMEOUT_MS = 10000;

    AudioOutputManager* _manager = nullptr;
    TaskHandle_t _appTask = nullptr;
    // Client must outlive HTTPClient (members are destroyed in reverse order).
    NetworkClient _client;
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
    bool _lease = false;
    bool _attached = false;
    bool _fault = false;
    PlayMediaState _state = PlayMediaState::Idle;
    const char* _error = nullptr;

    bool onAppTask() const;
    bool validUrl(const String& url) const;
    void cleanup();
    void fail(const char* error);
    void complete();
    void consume(size_t bytes);
};
