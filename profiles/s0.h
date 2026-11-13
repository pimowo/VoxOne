#ifndef VOXONE_PROFILE_S0_H
#define VOXONE_PROFILE_S0_H

#include "bases/zero.h"

// Current user-wired ESP32-S3 Zero prototype. S1 remains a separate future PCB.
#define VOXONE_PROFILE_NAME "S0"
#define VOXONE_PROFILE_DISPLAY voxone::Display::Ssd1306_128x64
#define VOXONE_DISPLAY_CONTROLLER VOXONE_DISPLAY_CONTROLLER_SSD1306
#define DSP_MODEL DSP_SSD1306
#define VOXONE_DISPLAY_PROFILE VOXONE_DISPLAY_PROFILE_OLED_128X64
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
#define VOXONE_HAS_DLNA 0
#define VOXONE_INCLUDE_DSP 0
#define VOXONE_PIN_MAP_COMPLETE 1

// External VoxOneBT UART protocol 2 on UART1. GPIO43/44 are the default
// UART0 pins, so the S0 build routes application Serial diagnostics through
// native USB CDC instead. GPIO3 remains reserved for POWER/MENU and GPIO10
// for a future BT TX audio path.
#define VOXONE_BT_UART_RX_PIN 44
#define VOXONE_BT_UART_TX_PIN 43

// VoxOneBT is the I2S master; S0 receives stereo S16 PCM on I2S1.
#define VOXONE_BT_I2S_RX_ENABLED 1
#define VOXONE_BT_I2S_BCLK_PIN 11
#define VOXONE_BT_I2S_WS_PIN 12
#define VOXONE_BT_I2S_DATA_PIN 13

// The external I2C module must provide SDA/SCL pull-ups.

#endif
