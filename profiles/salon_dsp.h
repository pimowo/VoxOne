#ifndef VOXONE_PROFILE_SALON_DSP_H
#define VOXONE_PROFILE_SALON_DSP_H

#define VOXONE_PROFILE_NAME "salon_dsp"
#define VOXONE_PROFILE_MCU voxone::Mcu::Esp32S3
#define VOXONE_PROFILE_DISPLAY voxone::Display::St7796_480x320
#define VOXONE_PROFILE_AUDIO voxone::AudioOutput::Pcm5102a

#define VOXONE_HAS_DISPLAY 1
#define VOXONE_HAS_ENCODER 1
#define VOXONE_HAS_VU 1
#define VOXONE_HAS_BT 1
#define VOXONE_HAS_AUX 1
#define VOXONE_HAS_SPDIF 1
#define VOXONE_HAS_TDA7719 true
#define VOXONE_HAS_LOCAL_UI 1
#define VOXONE_PIN_MAP_COMPLETE 0

#define DSP_MODEL DSP_ST7796

// Hardware pins remain undefined until the SALON_DSP schematic is fixed.
// TDA7719 is a declared capability only; no runtime or driver is enabled here.

#endif
