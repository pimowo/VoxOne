#ifndef VOXONE_PROFILE_X0_H
#define VOXONE_PROFILE_X0_H

#define VOXONE_PROFILE_NAME "X0"
#define VOXONE_PROFILE_MCU voxone::Mcu::Esp32
#define VOXONE_PROFILE_DISPLAY voxone::Display::St7789_284x76
#define VOXONE_DISPLAY_PROFILE VOXONE_DISPLAY_PROFILE_ST7789_284X76
#define VOXONE_DISPLAY_CONTROLLER VOXONE_DISPLAY_CONTROLLER_ST7789
#define VOXONE_PROFILE_AUDIO voxone::AudioOutput::Pcm5102a

#define VOXONE_HAS_DISPLAY 1
#define VOXONE_HAS_ENCODER 1
#define VOXONE_HAS_VU 0
#define VOXONE_HAS_BT 0
#define VOXONE_HAS_AUX 0
#define VOXONE_HAS_SPDIF 0
#define VOXONE_HAS_TDA7719 false
#define VOXONE_HAS_LOCAL_UI 1
#define VOXONE_PIN_MAP_COMPLETE 1

// Existing, physically verified X0 pin map.
#define DSP_MODEL DSP_ST7789_76
#define TFT_CS 5
#define TFT_DC 4
#define TFT_RST -1
#define DSP_HSPI false

#define I2S_DOUT 27
#define I2S_BCLK 26
#define I2S_LRC 25
#define I2S_INTERNAL false

// Native yoRadio mapping; false means four transitions per detent.
#define ENC_BTNL 33
#define ENC_BTNB 32
#define ENC_BTNR 35
#define ENC_INTERNALPULLUP false
#define ENC_HALFQUARD false

#endif
