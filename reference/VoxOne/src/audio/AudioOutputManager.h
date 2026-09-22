#pragma once

#include "AudioOutput.h"
#include <atomic>

enum class AudioOutputOwner : uint8_t {
    None,
    Bluetooth,
    Radio,
    PlayMedia
};

// Shared final PCM gain. Both RADIO and BT write 16-bit PCM through this
// adapter immediately before I2S; AVRCP is not an audio gain stage.
class PcmGainOutput final : public Print {
public:
    void attach(Print& target) { _target = &target; }
    void detach() { _target = nullptr; }
    void setVolume(int logicalVolume);
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t* data, size_t size) override;
private:
    Print* _target = nullptr;
    std::atomic<uint32_t> _gainQ15{0};
};

// Lifecycle calls belong to the App task, never to audio callbacks.
// Source priority and user-visible ownership remain App responsibilities.
class AudioOutputManager {
public:
    AudioOutputManager() = default;
    AudioOutputManager(const AudioOutputManager&) = delete;
    AudioOutputManager& operator=(const AudioOutputManager&) = delete;

    bool begin(int bclk, int lrclk, int dout);
    void setVolume(int volume0to100) { _gain.setVolume(volume0to100); }
    bool acquire(AudioOutputOwner owner);
    bool release(AudioOutputOwner owner);
    AudioOutputOwner owner() const { return _owner; }
    bool isOwnedBy(AudioOutputOwner owner) const {
        return owner != AudioOutputOwner::None && _owner == owner;
    }

    // Pin the lease while an asynchronous producer retains the Print pointer.
    // A producer must stop/join ALL callbacks before detach; then discard its
    // pointer. release refuses to end I2S while a producer is attached.
    Print* attach(AudioOutputOwner owner);
    bool detach(AudioOutputOwner owner);

    // App task only, between PCM writes by the attached lease holder.
    bool configureStereo16(AudioOutputOwner owner, uint32_t sampleRate);

private:
    AudioOutput _output;
    PcmGainOutput _gain;
    AudioOutputOwner _owner = AudioOutputOwner::None;
    bool _begun = false;
    bool _attached = false;
    bool _fault = false;
    int _bclk = -1;
    int _lrclk = -1;
    int _dout = -1;
};
