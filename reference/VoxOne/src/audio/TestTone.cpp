#include "TestTone.h"
#include "BoardConfig.h"
#include "AppConfig.h"
#include "../diagnostics/Logger.h"
#include <math.h>

bool TestTone::begin() {
    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chanCfg.auto_clear = true;

    if (i2s_new_channel(&chanCfg, &_tx, nullptr) != ESP_OK) {
        Logger::error("AUDIO", "i2s_new_channel failed");
        return false;
    }

    i2s_std_config_t stdCfg = {};
    stdCfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AppConfig::TEST_SAMPLE_RATE);
    stdCfg.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_STEREO
    );
    stdCfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    stdCfg.gpio_cfg.bclk = (gpio_num_t)Board::I2S_BCLK;
    stdCfg.gpio_cfg.ws = (gpio_num_t)Board::I2S_LRC;
    stdCfg.gpio_cfg.dout = (gpio_num_t)Board::I2S_DOUT;
    stdCfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    stdCfg.gpio_cfg.invert_flags.mclk_inv = false;
    stdCfg.gpio_cfg.invert_flags.bclk_inv = false;
    stdCfg.gpio_cfg.invert_flags.ws_inv = false;

    if (i2s_channel_init_std_mode(_tx, &stdCfg) != ESP_OK) {
        Logger::error("AUDIO", "i2s_channel_init_std_mode failed");
        return false;
    }

    if (i2s_channel_enable(_tx) != ESP_OK) {
        Logger::error("AUDIO", "i2s_channel_enable failed");
        return false;
    }

    Logger::info("AUDIO", "PCM5102A test I2S ready");
    return true;
}

void TestTone::setPlaying(bool playing) {
    _playing = playing;
}

void TestTone::setVolume(int volume) {
    _volume = constrain(volume, 0, 100);
}

void TestTone::loop() {
    if (!_tx) return;

    static int16_t frames[64 * 2];
    const float step = 2.0f * PI * AppConfig::TEST_TONE_HZ / AppConfig::TEST_SAMPLE_RATE;
    const float amplitude = _playing ? (7200.0f * (_volume / 100.0f)) : 0.0f;

    for (size_t i = 0; i < 64; ++i) {
        int16_t sample = (int16_t)(sinf(_phase) * amplitude);
        _phase += step;
        if (_phase >= 2.0f * PI) _phase -= 2.0f * PI;

        frames[i * 2] = sample;
        frames[i * 2 + 1] = sample;
    }

    size_t written = 0;
    i2s_channel_write(_tx, frames, sizeof(frames), &written, 0);
}
