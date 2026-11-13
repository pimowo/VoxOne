#ifndef VOXONE_MCU_BASE_ZERO_H
#define VOXONE_MCU_BASE_ZERO_H

// ESP32-S3 Zero N4R2 shared MCU contract. Profile headers only add the
// board-specific pin map and capabilities on top of this base.
#define VOXONE_PROFILE_MCU voxone::Mcu::Esp32S3
#define VOXONE_MCU_BASE voxone::McuBase::Zero
#define VOXONE_MCU_FLASH_BYTES (4u * 1024u * 1024u)
#define VOXONE_MCU_PSRAM_BYTES (2u * 1024u * 1024u)

// Shared board-level pin contract for the Zero family (S/DIN). Optional
// peripherals such as VoxOneBT remain in the concrete profile.
#define I2S_BCLK 5
#define I2S_DOUT 6
#define I2S_LRC 4

#define POWER_BUTTON_PIN 3
#define POWER_RELAY_PIN -1

#define ENC_BTNB 7
#define ENC_BTNR 8
#define ENC_BTNL 9
#define ENC_INTERNALPULLUP false
#define ENC_BUTTON_INTERNALPULLUP false
#define VOXONE_EC11_REVERSE_DIRECTION 1

#define I2C_SDA 1
#define I2C_SCL 2
#define I2C_RST -1
#define SCREEN_ADDRESS 0x3C
#define I2CFREQ_HZ 400000UL

#endif
