#ifndef YOVOXONE_PROFILE_DIN_H
#define YOVOXONE_PROFILE_DIN_H

#define YOVOXONE_PROFILE_NAME "din"
#define YOVOXONE_PROFILE_MCU yovoxone::Mcu::Esp32S3
#define YOVOXONE_PROFILE_DISPLAY yovoxone::Display::None
#define YOVOXONE_PROFILE_AUDIO yovoxone::AudioOutput::Pcm5102a

#define YOVOXONE_HAS_DISPLAY 0
#define YOVOXONE_HAS_ENCODER 0
#define YOVOXONE_HAS_VU 0
#define YOVOXONE_HAS_BT 1
#define YOVOXONE_HAS_AUX 0
#define YOVOXONE_HAS_SPDIF 0
#define YOVOXONE_HAS_TDA7719 false
#define YOVOXONE_HAS_LOCAL_UI 0
#define YOVOXONE_PIN_MAP_COMPLETE 0

#define DSP_MODEL DSP_DUMMY

// The PCM5102A pins remain intentionally undefined until the DIN schematic is fixed.

#endif
