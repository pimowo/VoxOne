#include "AudioOutput.h"

#include "AppConfig.h"
#include "../diagnostics/Logger.h"

namespace {
constexpr uint32_t I2S_DMA_DESC_NUM = 8;
constexpr uint32_t I2S_DMA_FRAME_NUM = 512;
}

bool AudioOutput::begin(int bclk, int lrclk, int dout) {
    if (_ready) return true;

    i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(
        I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channelConfig.dma_desc_num = I2S_DMA_DESC_NUM;
    channelConfig.dma_frame_num = I2S_DMA_FRAME_NUM;
    channelConfig.auto_clear = true;

    if (i2s_new_channel(&channelConfig, &_tx, nullptr) != ESP_OK) {
        Logger::error("AUDIO", "I2S init failed");
        return false;
    }

    i2s_std_config_t standardConfig = {};
    standardConfig.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AppConfig::AUDIO_SAMPLE_RATE);
    standardConfig.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    standardConfig.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    standardConfig.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    standardConfig.gpio_cfg.bclk = static_cast<gpio_num_t>(bclk);
    standardConfig.gpio_cfg.ws = static_cast<gpio_num_t>(lrclk);
    standardConfig.gpio_cfg.dout = static_cast<gpio_num_t>(dout);
    standardConfig.gpio_cfg.din = I2S_GPIO_UNUSED;
    standardConfig.gpio_cfg.invert_flags.mclk_inv = false;
    standardConfig.gpio_cfg.invert_flags.bclk_inv = false;
    standardConfig.gpio_cfg.invert_flags.ws_inv = false;

    if (i2s_channel_init_std_mode(_tx, &standardConfig) != ESP_OK ||
        i2s_channel_enable(_tx) != ESP_OK) {
        i2s_del_channel(_tx);
        _tx = nullptr;
        Logger::error("AUDIO", "I2S init failed");
        return false;
    }

    _sampleRate = AppConfig::AUDIO_SAMPLE_RATE;
    _ready = true;
    Logger::info(
        "AUDIO",
        String("I2S ready BCLK=") + bclk +
        " WS=" + lrclk +
        " DOUT=" + dout +
        " rate=" + AppConfig::AUDIO_SAMPLE_RATE
    );
    return true;
}

bool AudioOutput::end() {
    if (!_ready) return true;
    // Only after the manager has detached a quiescent producer.
    if (i2s_channel_disable(_tx) != ESP_OK || i2s_del_channel(_tx) != ESP_OK) {
        Logger::error("AUDIO", "I2S teardown failed");
        return false;
    }
    _tx = nullptr;
    _sampleRate = 0;
    _ready = false;
    return true;
}

bool AudioOutput::configureTX(uint32_t rate, i2s_data_bit_width_t bits,
                              i2s_slot_mode_t channels,
                              i2s_std_slot_mask_t slotMask) {
    if (!_ready || !_tx || bits != I2S_DATA_BIT_WIDTH_16BIT ||
        channels != I2S_SLOT_MODE_STEREO || slotMask != I2S_STD_SLOT_BOTH) {
        return false;
    }
    if (_sampleRate == rate) return true;

    i2s_std_clk_config_t clockConfig = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    i2s_std_slot_config_t slotConfig = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    slotConfig.slot_mask = I2S_STD_SLOT_BOTH;

    if (i2s_channel_disable(_tx) != ESP_OK ||
        i2s_channel_reconfig_std_clock(_tx, &clockConfig) != ESP_OK ||
        i2s_channel_reconfig_std_slot(_tx, &slotConfig) != ESP_OK ||
        i2s_channel_enable(_tx) != ESP_OK) {
        return false;
    }
    _sampleRate = rate;
    return true;
}

size_t AudioOutput::write(const uint8_t* data, size_t size) {
    if (!_ready || !_tx || !data) return 0;

    size_t written = 0;
    while (written < size) {
        size_t sent = 0;
        const esp_err_t error = i2s_channel_write(
            _tx, data + written, size - written, &sent, getTimeout());
        written += sent;
        if (error != ESP_OK) {
            setWriteError(error);
            break;
        }
    }
    return written;
}
