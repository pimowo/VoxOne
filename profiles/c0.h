#ifndef VOXONE_PROFILE_C0_H
#define VOXONE_PROFILE_C0_H

#define VOXONE_PROFILE_NAME "C0"
#define VOXONE_PROFILE_MCU voxone::Mcu::Esp32S3
#if defined(VOXONE_C0_DISPLAY_SSD1309)
#define VOXONE_PROFILE_DISPLAY voxone::Display::Ssd1309_128x64
#define DSP_MODEL DSP_SSD1305I2C
#else
#define VOXONE_PROFILE_DISPLAY voxone::Display::Ssd1306_128x64
#define DSP_MODEL DSP_SSD1306
#endif
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

#define I2S_BCLK 1
#define I2S_DOUT 2
#define I2S_LRC 3

#define ENC_BTNB 4
#define ENC_BTNR 6
#define ENC_BTNL 5
#define ENC_INTERNALPULLUP false
#define ENC_BUTTON_INTERNALPULLUP true

#define I2C_SDA 7
#define I2C_SCL 8
#define I2C_RST -1
#define SCREEN_ADDRESS 0x3C

#endif
