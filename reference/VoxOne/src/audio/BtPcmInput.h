#pragma once

#include <Arduino.h>

// Future I2S RX slave boundary. This checkpoint has no wired receiver:
// begin() remains false, active() false, and no input pin or I2S lease is
// acquired. A tested implementation will replace this class per board.
class BtPcmInput {
public:
    virtual ~BtPcmInput() = default;
    virtual bool begin(uint32_t sampleRate) = 0;
    virtual void end() = 0;
    virtual size_t available() const = 0;
    virtual size_t read(uint8_t* buffer, size_t capacity) = 0;
    virtual uint32_t sampleRate() const = 0;
    virtual bool active() const = 0;
};

class UnwiredBtPcmInput final : public BtPcmInput {
public:
    bool begin(uint32_t) override { return false; }
    void end() override {}
    size_t available() const override { return 0; }
    size_t read(uint8_t*, size_t) override { return 0; }
    uint32_t sampleRate() const override { return 0; }
    bool active() const override { return false; }
};
