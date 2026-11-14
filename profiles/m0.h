#ifndef VOXONE_PROFILE_M0_H
#define VOXONE_PROFILE_M0_H

#include "bases/devkit.h"

#define VOXONE_PROFILE_NAME "M0"
#define VOXONE_PROFILE_DISPLAY voxone::Display::Ssd1322_256x64
#define VOXONE_DISPLAY_CONTROLLER VOXONE_DISPLAY_CONTROLLER_SSD1322
#define DSP_MODEL DSP_SSD1322
#define VOXONE_DISPLAY_PROFILE VOXONE_DISPLAY_PROFILE_OLED_256X64
#define VOXONE_PROFILE_AUDIO voxone::AudioOutput::Pcm5102a

#define VOXONE_HAS_DISPLAY 1
#define VOXONE_HAS_ENCODER 1
#define VOXONE_HAS_VU 0
#define VOXONE_HAS_BT 0
#define VOXONE_HAS_SD 1
#define VOXONE_HAS_IR 1
#define VOXONE_HAS_W5500 0
#define VOXONE_HAS_AUX 1
#define VOXONE_HAS_SPDIF 0
#define VOXONE_HAS_TDA7719 false
#define VOXONE_HAS_LOCAL_UI 1
#define VOXONE_INCLUDE_DSP 0
#define VOXONE_PIN_MAP_COMPLETE 1

// M0 uses the shared DevKit SPI display bus and the same control pins as L0.
#define DSP_HSPI false
#define TFT_DC 9
#define TFT_CS 10
#define TFT_RST -1

#endif
