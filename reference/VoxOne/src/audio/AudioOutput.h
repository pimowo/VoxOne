#pragma once

#include <Arduino.h>
#include <driver/i2s_std.h>

class AudioOutput final : public Stream {
private:
    friend class AudioOutputManager;
    bool begin(int bclk, int lrclk, int dout);
    bool end();
    bool ready() const { return _ready; }
    AudioOutput& stream() { return *this; }
    bool configureTX(uint32_t rate, i2s_data_bit_width_t bits,
                     i2s_slot_mode_t channels, i2s_std_slot_mask_t slotMask);

    size_t write(uint8_t data) override { return write(&data, 1); }
    size_t write(const uint8_t* data, size_t size) override;
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }

    i2s_chan_handle_t _tx = nullptr;
    uint32_t _sampleRate = 0;
    bool _ready = false;
};
