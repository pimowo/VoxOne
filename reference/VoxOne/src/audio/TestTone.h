#pragma once
#include <Arduino.h>
#include <driver/i2s_std.h>

class TestTone {
public:
    bool begin();
    void setPlaying(bool playing);
    void setVolume(int volume);
    void loop();

private:
    i2s_chan_handle_t _tx = nullptr;
    bool _playing = false;
    int _volume = 25;
    float _phase = 0.0f;
};
