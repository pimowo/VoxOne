#ifndef YOVOXONE_PROFILE_SALON_H
#define YOVOXONE_PROFILE_SALON_H

#define YOVOXONE_PROFILE_NAME "salon"
#define YOVOXONE_PROFILE_MCU yovoxone::Mcu::Esp32S3
#define YOVOXONE_PROFILE_DISPLAY yovoxone::Display::St7796_480x320
#define YOVOXONE_PROFILE_AUDIO yovoxone::AudioOutput::Pcm5102a

#define YOVOXONE_HAS_DISPLAY 1
#define YOVOXONE_HAS_ENCODER 1
#define YOVOXONE_HAS_VU 1
#define YOVOXONE_HAS_BT 1
#define YOVOXONE_HAS_AUX 1
#define YOVOXONE_HAS_SPDIF 1
#define YOVOXONE_HAS_TDA7719 false
#define YOVOXONE_HAS_LOCAL_UI 1
#define YOVOXONE_PIN_MAP_COMPLETE 0

#define DSP_MODEL DSP_ST7796

// TFT, encoder and PCM5102A pins remain undefined until the SALON schematic is fixed.

#endif
