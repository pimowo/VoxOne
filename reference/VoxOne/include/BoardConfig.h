#pragma once
#include <Arduino.h>

namespace Board {

// ---- profil: istniejące radio yoRadio / ESP32-U ----
static constexpr const char* PROFILE_NAME = "yoradio-esp32u-st7789-76-pcm5102a";

// MCU / flash
static constexpr uint32_t FLASH_BYTES = 4UL * 1024UL * 1024UL;

// Capabilities
static constexpr bool HAS_DISPLAY = true;
static constexpr bool HAS_ENCODER = true;
static constexpr bool HAS_BUTTONS = false;
static constexpr bool HAS_AMP_MUTE = false;
static constexpr bool HAS_PSRAM = false;

// TFT ST7789 284x76, zgodny z DSP_ST7789_76 w yoRadio
static constexpr int TFT_SCK  = 18;
static constexpr int TFT_MOSI = 23;
static constexpr int TFT_CS   = 5;
static constexpr int TFT_DC   = 4;
static constexpr int TFT_RST  = -1;
static constexpr int TFT_INIT_W = 76;
static constexpr int TFT_INIT_H = 284;
static constexpr int TFT_ROTATION = 1;

// Enkoder 1
static constexpr int ENC_RIGHT  = 35;
static constexpr int ENC_LEFT   = 33;
static constexpr int ENC_BUTTON = 32;
static constexpr bool ENC_INTERNAL_PULLUP = false;
// Potwierdzone testem: fizyczny kierunek jest odwrócony
static constexpr int ENC_DIRECTION = -1;

// PCM5102A / I2S
static constexpr int I2S_DOUT = 27;
static constexpr int I2S_BCLK = 26;
static constexpr int I2S_LRC  = 25;

// Proposed, unconnected VoxOneBT UART2 pins. Initialized only when
// features.bluetoothEnabled is ON in NORMAL mode. No I2S RX pins are used yet.
static constexpr int BT_UART_TX = 17;
static constexpr int BT_UART_RX = 16;

} // namespace Board
