#ifndef myoptions_h
#define myoptions_h

// YV-M1 DESK hardware profile using only native yoRadio configuration.

// ST7789 284x76 on the ESP32 VSPI bus (SCK=18, MOSI=23).
#define DSP_MODEL DSP_ST7789_76
#define TFT_CS 5
#define TFT_DC 4
#define TFT_RST -1
#define DSP_HSPI false

// PCM5102A through the original audioI2S backend.
#define I2S_DOUT 27
#define I2S_BCLK 26
#define I2S_LRC 25
#define I2S_INTERNAL false
#define VS1053_CS 255

// Native yoRadio encoder mapping. false means 4 transitions per detent.
#define ENC_BTNL 33
#define ENC_BTNB 32
#define ENC_BTNR 35
#define ENC_INTERNALPULLUP false
#define ENC_HALFQUARD false

// Explicitly disable hardware which is absent on DESK.
#define TS_MODEL TS_MODEL_UNDEFINED
#define SDC_CS 255
#define IR_PIN 255
#define RTC_MODULE RTC_MODULE_UNDEFINED
#define RTC_SDA 255
#define RTC_SCL 255
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