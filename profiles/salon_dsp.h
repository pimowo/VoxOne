#ifndef YOVOXONE_PROFILE_SALON_DSP_H
#define YOVOXONE_PROFILE_SALON_DSP_H

#define YOVOXONE_PROFILE_NAME "salon_dsp"
#define YOVOXONE_PROFILE_MCU yovoxone::Mcu::Esp32S3
#define YOVOXONE_PROFILE_DISPLAY yovoxone::Display::St7796_480x320
#define YOVOXONE_PROFILE_AUDIO yovoxone::AudioOutput::Pcm5102a

#define YOVOXONE_HAS_DISPLAY 1
#define YOVOXONE_HAS_ENCODER 1
#define YOVOXONE_HAS_VU 1
#define YOVOXONE_HAS_BT 1
#define YOVOXONE_HAS_AUX 1
#define YOVOXONE_HAS_SPDIF 1
#define YOVOXONE_HAS_TDA7719 true
#define YOVOXONE_HAS_LOCAL_UI 1
#define YOVOXONE_PIN_MAP_COMPLETE 0

#define DSP_MODEL DSP_ST7796

// Hardware pins remain undefined until the SALON_DSP schematic is fixed.
// TDA7719 is a declared capability only; no runtime or driver is enabled here.

#endif
