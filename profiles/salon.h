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
#define VOXONE_HAS_AUX 1
#define VOXONE_HAS_SPDIF 1
#define VOXONE_HAS_TDA7719 false
#define VOXONE_HAS_LOCAL_UI 1
#define VOXONE_PIN_MAP_COMPLETE 0

#define DSP_MODEL DSP_ST7796

// TFT, encoder and PCM5102A pins remain undefined until the SALON schematic is fixed.

#endif
