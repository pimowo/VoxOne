#ifndef VOXONE_MCU_BASE_DEVKIT_H
#define VOXONE_MCU_BASE_DEVKIT_H

// ESP32-S3 DevKit N16R8 shared MCU contract. Profile headers only add the
// board-specific pin map and capabilities on top of this base.
#define VOXONE_PROFILE_MCU voxone::Mcu::Esp32S3
#define VOXONE_MCU_BASE voxone::McuBase::DevKit
#define VOXONE_MCU_FLASH_BYTES (16u * 1024u * 1024u)
#define VOXONE_MCU_PSRAM_BYTES (8u * 1024u * 1024u)

// Shared board-level pin contract for the DevKit family (M/L). Optional
// peripherals such as TFT, RTC and VoxOneBT remain in the concrete profile.
#define I2S_DOUT 4
#define I2S_BCLK 5
#define I2S_LRC 6
#define I2S_INTERNAL false
#define VOXONE_DAC_XSMT_PIN 255

#define POWER_BUTTON_PIN 42
#define POWER_RELAY_PIN -1

#define ENC_BTNR 40
#define ENC_BTNL 41
#define ENC_BTNB 39
#define ENC_INTERNALPULLUP false
#define ENC_BUTTON_INTERNALPULLUP false
#define ENC_HALFQUARD false
#define USE_BUILTIN_LED false

#define I2C_SDA 8
#define I2C_SCL 7
#define I2C_RST -1
#define SCREEN_ADDRESS 0x3C
#define I2CFREQ_HZ 400000UL

#endif
