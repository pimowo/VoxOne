#ifndef VOXONE_PROFILE_CHECKS_H
#define VOXONE_PROFILE_CHECKS_H

namespace voxone {
namespace profile_checks {

constexpr bool displayMatchesLegacy(Display display, int dspModel) {
  return (display == Display::None && dspModel == DSP_DUMMY) ||
         (display == Display::Ssd1306_128x64 && dspModel == DSP_SSD1306) ||
         (display == Display::Ssd1309_128x64 && dspModel == DSP_SSD1305I2C) ||
         (display == Display::St7789_284x76 && dspModel == DSP_ST7789_76) ||
         (display == Display::St7796_480x320 && dspModel == DSP_ST7796);
}

constexpr bool displaySupportsVu(Display display) {
  return display == Display::St7796_480x320;
}

static_assert(display_profile::activeProfile() ==
                  display_profile::profileFor(VOXONE_PROFILE_DISPLAY),
              "Display layout profile must match the selected backend");
static_assert(display_profile::activeController() ==
                  display_profile::controllerFor(VOXONE_PROFILE_DISPLAY),
              "Display controller must match the selected backend");
static_assert((display_profile::activeProfile() ==
                   display_profile::ProfileId::None) ==
                  !bool(VOXONE_HAS_DISPLAY),
              "Headless display profile must match the display compile gate");

static_assert(bool(VOXONE_HAS_DISPLAY) == (VOXONE_PROFILE_DISPLAY != Display::None),
              "Display compile gate must match selected display backend");
static_assert(bool(VOXONE_HAS_DISPLAY) == (DSP_MODEL != DSP_DUMMY),
              "Display compile gate must match legacy DSP_MODEL");
static_assert(displayMatchesLegacy(VOXONE_PROFILE_DISPLAY, DSP_MODEL),
              "Selected display backend does not match legacy DSP_MODEL");

static_assert(!VOXONE_PIN_MAP_COMPLETE ||
              (VOXONE_PROFILE_DISPLAY != Display::St7789_284x76 &&
               VOXONE_PROFILE_DISPLAY != Display::St7796_480x320) ||
              (TFT_CS != 255 && TFT_DC != 255),
              "Complete SPI display profile requires TFT_CS and TFT_DC pins");

static_assert(!VOXONE_PIN_MAP_COMPLETE ||
              (VOXONE_PROFILE_DISPLAY != Display::Ssd1306_128x64 &&
               VOXONE_PROFILE_DISPLAY != Display::Ssd1309_128x64) ||
              (I2C_SDA != 255 && I2C_SDA != -1 &&
               I2C_SCL != 255 && I2C_SCL != -1),
              "Complete I2C OLED profile requires I2C_SDA and I2C_SCL pins");

static_assert(!VOXONE_PIN_MAP_COMPLETE || !VOXONE_HAS_ENCODER ||
              (ENC_BTNL != 255 && ENC_BTNR != 255),
              "Encoder compile gate requires ENC_BTNL and ENC_BTNR");

static_assert(!VOXONE_HAS_LOCAL_UI ||
              (VOXONE_HAS_DISPLAY && VOXONE_HAS_ENCODER),
              "Local UI compile gate requires display and encoder");

static_assert(bool(VOXONE_HAS_VU) == displaySupportsVu(VOXONE_PROFILE_DISPLAY),
              "VU compile gate does not match the selected display configuration");

#if defined(VOXONE_PROFILE_A0_DSP)
static_assert(VOXONE_HAS_TDA7719,
              "A0_DSP profile requires VOXONE_HAS_TDA7719=true");
#else
static_assert(!VOXONE_HAS_TDA7719,
              "VOXONE_HAS_TDA7719=true is only valid for A0_DSP");
#endif

static_assert(!VOXONE_HAS_TDA7719 ||
              VOXONE_PROFILE_AUDIO == AudioOutput::Pcm5102a,
              "TDA7719 profile requires the PCM5102A audio path");

static_assert(!VOXONE_PIN_MAP_COMPLETE ||
              VOXONE_PROFILE_AUDIO != AudioOutput::Pcm5102a ||
              (I2S_DOUT != 255 && I2S_BCLK != 255 && I2S_LRC != 255),
              "Complete PCM5102A profile requires I2S_DOUT, I2S_BCLK and I2S_LRC pins");

#if defined(CONFIG_IDF_TARGET_ESP32S3)
static_assert(VOXONE_PROFILE_MCU == Mcu::Esp32S3,
              "Selected MCU must match the ESP32-S3 build target");
#elif defined(CONFIG_IDF_TARGET_ESP32)
static_assert(VOXONE_PROFILE_MCU == Mcu::Esp32,
              "Selected MCU must match the ESP32 build target");
#endif

}  // namespace profile_checks
}  // namespace voxone

#endif
