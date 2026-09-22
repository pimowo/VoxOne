#pragma once

#include <Arduino.h>

// Future source contract only. No App or AudioOutputManager wiring yet.
// Radio/PlayMedia produce PCM; remote BT supplies PCM through I2S RX slave.
enum class PcmRoute : uint8_t { None, Radio, BluetoothRx, PlayMedia };

class PcmInput {
public:
    virtual ~PcmInput() = default;
    virtual uint32_t sampleRate() const = 0;
    virtual size_t read(uint8_t* dst, size_t capacity) = 0;
};

class AudioRouter {
public:
    virtual ~AudioRouter() = default;
    // App task only; must quiesce the previous producer before route change.
    virtual bool select(PcmRoute route, PcmInput* input) = 0;
    virtual void loop() = 0; // bounded work; no blocking waits for absent BT.
    virtual void setVolume(int logical0to100) = 0;
    virtual PcmRoute route() const = 0;
};
