#ifndef VOXONE_UNAVAILABLE_HARDWARE_H
#define VOXONE_UNAVAILABLE_HARDWARE_H

// Disable optional yoRadio hardware unless the selected profile defines it.
#ifndef TFT_CS
#define TFT_CS 255
#endif
#ifndef TFT_DC
#define TFT_DC 255
#endif
#ifndef TFT_RST
#define TFT_RST -1
#endif
#ifndef DSP_HSPI
#define DSP_HSPI false
#endif

#ifndef I2S_DOUT
#define I2S_DOUT 255
#endif
#ifndef I2S_BCLK
#define I2S_BCLK 255
#endif
#ifndef I2S_LRC
#define I2S_LRC 255
#endif
#ifndef I2S_INTERNAL
#define I2S_INTERNAL false
#endif
#ifndef VOXONE_BT_I2S_RX_ENABLED
#define VOXONE_BT_I2S_RX_ENABLED 0
#endif
#ifndef VOXONE_BT_I2S_BCLK_PIN
#define VOXONE_BT_I2S_BCLK_PIN 255
#endif
#ifndef VOXONE_BT_I2S_WS_PIN
#define VOXONE_BT_I2S_WS_PIN 255
#endif
#ifndef VOXONE_BT_I2S_DATA_PIN
#define VOXONE_BT_I2S_DATA_PIN 255
#endif
#ifndef VS1053_CS
#define VS1053_CS 255
#endif

#ifndef ENC_BTNL
#define ENC_BTNL 255
#endif
#ifndef ENC_BTNB
#define ENC_BTNB 255
#endif
#ifndef ENC_BTNR
#define ENC_BTNR 255
#endif
#ifndef ENC_INTERNALPULLUP
#define ENC_INTERNALPULLUP true
#endif
#ifndef ENC_HALFQUARD
#define ENC_HALFQUARD false
#endif

#define TS_MODEL TS_MODEL_UNDEFINED
#define SDC_CS 255
#define IR_PIN 255
#ifndef RTC_MODULE
#define RTC_MODULE RTC_MODULE_UNDEFINED
#endif
#ifndef RTC_SDA
#define RTC_SDA 255
#endif
#ifndef RTC_SCL
#define RTC_SCL 255
#endif
#define NEXTION_RX 255
#define NEXTION_TX 255

#define ENC2_BTNL 255
#define ENC2_BTNB 255
#define ENC2_BTNR 255

#define BTN_LEFT 255
#define BTN_CENTER 255
#define BTN_RIGHT 255
#define BTN_UP 255
#define BTN_DOWN 255
#define BTN_MODE 255

#endif
