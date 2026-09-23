#ifndef YOVOXONE_PROFILE_DESK_H
#define YOVOXONE_PROFILE_DESK_H

#define YOVOXONE_PROFILE_NAME "desk"
#define YOVOXONE_PROFILE_MCU yovoxone::Mcu::Esp32
#define YOVOXONE_PROFILE_DISPLAY yovoxone::Display::St7789_284x76
#define YOVOXONE_PROFILE_AUDIO yovoxone::AudioOutput::Pcm5102a

#define YOVOXONE_HAS_DISPLAY 1
#define YOVOXONE_HAS_ENCODER 1
#define YOVOXONE_HAS_VU 0
#define YOVOXONE_HAS_BT 0
#define YOVOXONE_HAS_AUX 0
#define YOVOXONE_HAS_SPDIF 0
#define YOVOXONE_HAS_TDA7719 false
#define YOVOXONE_HAS_LOCAL_UI 1
#define YOVOXONE_PIN_MAP_COMPLETE 1

// Existing, physically verified YV-M1 DESK pin map.
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
