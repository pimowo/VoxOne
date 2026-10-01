#ifndef VOXONE_PROFILE_SALON_H
#define VOXONE_PROFILE_SALON_H

#define VOXONE_PROFILE_NAME "salon"
#define VOXONE_PROFILE_MCU voxone::Mcu::Esp32S3
#define VOXONE_PROFILE_DISPLAY voxone::Display::St7796_480x320
#define VOXONE_PROFILE_AUDIO voxone::AudioOutput::Pcm5102a

#define VOXONE_HAS_DISPLAY 1
#define VOXONE_HAS_ENCODER 1
#define VOXONE_HAS_VU 1
#define VOXONE_HAS_BT 1
#define VOXONE_HAS_AUX 0
#define VOXONE_HAS_SPDIF 0
#define VOXONE_HAS_TDA7719 false
#define VOXONE_HAS_LOCAL_UI 1
#define VOXONE_PIN_MAP_COMPLETE 1

// ST7796S uses the default ESP32-S3 SPI bus: MOSI 11, SCK 12, SS 10.
// MISO 13 belongs to that bus but is not connected to the LCD.
#define DSP_MODEL DSP_ST7796
#define DSP_HSPI false
#define TFT_DC 9
#define TFT_CS 10
#define TFT_RST -1
#define BRIGHTNESS_PIN 14

// ENCODER_1: S2 = GPIO41, S1 = GPIO40, KEY = GPIO39.
#define ENC_BTNR 40
#define ENC_BTNL 41
#define ENC_BTNB 39
#define ENC_INTERNALPULLUP false
#define ENC_HALFQUARD false
#define USE_BUILTIN_LED false

#define I2S_DOUT 4
#define I2S_BCLK 5
#define I2S_LRC 6
#define I2S_INTERNAL false
// PCM5102A XSMT: no GPIO assigned until the physical connection is confirmed.
#define VOXONE_DAC_XSMT_PIN 255

#define RTC_MODULE DS3231
#define RTC_SDA 8
#define RTC_SCL 7

// VoxOneBT UART uses the NEXTION connector, independently of legacy Nextion.
#define VOXONE_BT_UART_RX_PIN 15
#define VOXONE_BT_UART_TX_PIN 16

// VoxOneBT I2S RX: GPIO1 BCLK, GPIO2 WS, GPIO17 DATA IN; GPIO42 free.
// VoxOneBT TX BCLK/WS/DATA use 22-ohm series resistors at the transmitter.
#define VOXONE_BT_I2S_RX_ENABLED 1
#define VOXONE_BT_I2S_BCLK_PIN 1
#define VOXONE_BT_I2S_WS_PIN 2
#define VOXONE_BT_I2S_DATA_PIN 17
// Future TDA7719 shares the RTC I2C pins: SCL 7, SDA 8.

#endif
