#ifndef VOXONE_PROFILE_S0_H
#define VOXONE_PROFILE_S0_H

// Current user-wired ESP32-S3 Zero prototype. S1 remains a separate future PCB.
#define VOXONE_PROFILE_NAME "S0"
#define VOXONE_PROFILE_MCU voxone::Mcu::Esp32S3
#define VOXONE_RUNTIME_OLED_128X64_BACKENDS 1
#define VOXONE_PROFILE_DISPLAY voxone::Display::Ssd1306_128x32
#define VOXONE_DISPLAY_CONTROLLER VOXONE_DISPLAY_CONTROLLER_SSD1306
#define DSP_MODEL DSP_SSD1306
#define VOXONE_DISPLAY_PROFILE VOXONE_DISPLAY_PROFILE_OLED_128X32
#define VOXONE_PROFILE_AUDIO voxone::AudioOutput::Max98357

#define VOXONE_HAS_DISPLAY 1
#define VOXONE_HAS_ENCODER 1
#define VOXONE_HAS_VU 0
#define VOXONE_HAS_BT 1
#define VOXONE_BT_FW_UPDATE_ENABLED 0
#define VOXONE_HAS_AUX 0
#define VOXONE_HAS_SPDIF 0
#define VOXONE_HAS_TDA7719 false
#define VOXONE_HAS_LOCAL_UI 1
#define VOXONE_PIN_MAP_COMPLETE 1

#define I2S_BCLK 5
#define I2S_DOUT 6
#define I2S_LRC 4

// External VoxOneBT UART protocol 2 on UART1. GPIO43/44 are the default
// UART0 pins, so the S0 build routes application Serial diagnostics through
// native USB CDC instead. GPIO3 remains reserved for POWER/MENU and GPIO10
// for a future BT TX audio path.
#define VOXONE_BT_UART_RX_PIN 44
#define VOXONE_BT_UART_TX_PIN 43

#define POWER_BUTTON_PIN 3
#define POWER_RELAY_PIN -1

// VoxOneBT is the I2S master; S0 receives stereo S16 PCM on I2S1.
#define VOXONE_BT_I2S_RX_ENABLED 1
#define VOXONE_BT_I2S_BCLK_PIN 11
#define VOXONE_BT_I2S_WS_PIN 12
#define VOXONE_BT_I2S_DATA_PIN 13

#define ENC_BTNB 7
#define ENC_BTNR 8
#define ENC_BTNL 9
#define ENC_INTERNALPULLUP false
#define ENC_BUTTON_INTERNALPULLUP false
// The wired A/B phase on the S3 Zero reports physical CW as a negative delta.
#define VOXONE_EC11_REVERSE_DIRECTION 1

#define I2C_SDA 1
#define I2C_SCL 2
#define I2C_RST -1
#define SCREEN_ADDRESS 0x3C
// The external I2C module must provide SDA/SCL pull-ups.
#define I2CFREQ_HZ 400000UL

#endif
