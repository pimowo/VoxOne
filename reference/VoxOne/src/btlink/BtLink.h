#pragma once

#include <Arduino.h>

enum class BtModuleState : uint8_t { Disabled, Waiting, Ready, Unavailable };
enum class BtPlaybackState : uint8_t { Stopped, Playing, Paused };
enum class BtCommandResult : uint8_t {
    None,
    Ok,
    NotConnected,
    InvalidValue,
    UnknownCommand,
    NotImplemented,
    LineTooLong
};

// Control/status boundary only. Remote PCM is a separate I2S RX path.
// Implementations and all methods are called on the App task.
struct BtLinkStatus {
    bool ready = false;
    uint16_t protocolVersion = 0;
    bool connected = false;
    BtPlaybackState playback = BtPlaybackState::Stopped;
    bool playingEdge = false; // BT_STATE_PLAYING; consume after policy handles it.
    char deviceName[97] = {};
    char artist[193] = {};
    char title[193] = {};
    char album[193] = {};
    bool sampleRateKnown = false;
    uint32_t sampleRate = 0;
    bool volumeKnown = false;
    uint8_t volume = 0;
    BtCommandResult lastCommandResult = BtCommandResult::None;
};

class BtLink {
public:
    virtual ~BtLink() = default;
    virtual bool begin() = 0;
    virtual void loop() = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void next() = 0;
    virtual void previous() = 0;
    virtual void setVolume(uint8_t volume0to127) = 0;
    virtual void requestStatus() = 0;
    virtual const BtLinkStatus& status() const = 0;
    virtual BtModuleState moduleState() const = 0;
    virtual void consumePlayingEdge() = 0;

    bool connected() const { return status().connected; }
    bool playing() const {
        return status().playback == BtPlaybackState::Playing;
    }
    const char* artist() const { return status().artist; }
    const char* title() const { return status().title; }
    uint32_t sampleRate() const { return status().sampleRate; }
};
