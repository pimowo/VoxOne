#ifndef VOXONE_PROFILE_DIN_H
#define VOXONE_PROFILE_DIN_H

#define VOXONE_PROFILE_NAME "din"
#define VOXONE_PROFILE_MCU voxone::Mcu::Esp32S3
#define VOXONE_PROFILE_DISPLAY voxone::Display::None
#define VOXONE_PROFILE_AUDIO voxone::AudioOutput::Pcm5102a

#define VOXONE_HAS_DISPLAY 0
#define VOXONE_HAS_ENCODER 0
#define VOXONE_HAS_VU 0
#define VOXONE_HAS_BT 1
#define VOXONE_HAS_AUX 0
#define VOXONE_HAS_SPDIF 0
#define VOXONE_HAS_TDA7719 false
#define VOXONE_HAS_LOCAL_UI 0
#define VOXONE_PIN_MAP_COMPLETE 0

#define DSP_MODEL DSP_DUMMY

// The PCM5102A pins remain intentionally undefined until the DIN schematic is fixed.

#endif
