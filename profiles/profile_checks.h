#ifndef YOVOXONE_PROFILE_CHECKS_H
#define YOVOXONE_PROFILE_CHECKS_H

namespace yovoxone {
namespace profile_checks {

constexpr bool displayMatchesLegacy(Display display, int dspModel) {
  return (display == Display::None && dspModel == DSP_DUMMY) ||
         (display == Display::St7789_284x76 && dspModel == DSP_ST7789_76) ||
         (display == Display::St7796_480x320 && dspModel == DSP_ST7796);
}

constexpr bool displaySupportsVu(Display display) {
  return display == Display::St7796_480x320;
}

static_assert(activeProfile.capabilities.hasDisplay == (activeProfile.display != Display::None),
              "Profile capability hasDisplay must match HardwareProfile::display");
static_assert(activeProfile.capabilities.hasDisplay == (DSP_MODEL != DSP_DUMMY),
              "Profile capability hasDisplay must match legacy DSP_MODEL");
static_assert(displayMatchesLegacy(activeProfile.display, DSP_MODEL),
              "HardwareProfile::display does not match legacy DSP_MODEL");

static_assert(!activeProfile.pinMapComplete || !activeProfile.capabilities.hasDisplay ||
              (TFT_CS != 255 && TFT_DC != 255),
              "Complete display profile requires TFT_CS and TFT_DC pins");

static_assert(!activeProfile.pinMapComplete || !activeProfile.capabilities.hasEncoder ||
              (ENC_BTNL != 255 && ENC_BTNR != 255),
              "Complete profile with hasEncoder=true requires ENC_BTNL and ENC_BTNR");

static_assert(!activeProfile.capabilities.hasLocalUi ||
              (activeProfile.capabilities.hasDisplay && activeProfile.capabilities.hasEncoder),
              "Profile with hasLocalUi=true requires display and encoder capabilities");

static_assert(activeProfile.capabilities.hasVu == displaySupportsVu(activeProfile.display),
              "Profile capability hasVu does not match the selected display VU configuration");

#if defined(YOVOXONE_PROFILE_SALON_DSP)
static_assert(activeProfile.capabilities.hasTda7719,
              "SALON_DSP profile requires hasTda7719=true");
#else
static_assert(!activeProfile.capabilities.hasTda7719,
              "hasTda7719=true is only valid for the SALON_DSP profile");
#endif

static_assert(!activeProfile.capabilities.hasTda7719 ||
              activeProfile.audio == AudioOutput::Pcm5102a,
              "TDA7719 profile requires the PCM5102A audio path");

static_assert(!activeProfile.pinMapComplete ||
              activeProfile.audio != AudioOutput::Pcm5102a ||
              (I2S_DOUT != 255 && I2S_BCLK != 255 && I2S_LRC != 255),
              "Complete PCM5102A profile requires I2S_DOUT, I2S_BCLK and I2S_LRC pins");

#if defined(CONFIG_IDF_TARGET_ESP32S3)
static_assert(activeProfile.mcu == Mcu::Esp32S3,
              "HardwareProfile::mcu must match the ESP32-S3 build target");
#elif defined(CONFIG_IDF_TARGET_ESP32)
static_assert(activeProfile.mcu == Mcu::Esp32,
              "HardwareProfile::mcu must match the ESP32 build target");
#endif

}  // namespace profile_checks
}  // namespace yovoxone

#endif
