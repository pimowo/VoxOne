#ifndef VOXONE_PROFILE_H
#define VOXONE_PROFILE_H

namespace voxone {

enum class Mcu {
  Esp32,
  Esp32S3
};

enum class Display {
  None,
  St7789_284x76,
  St7796_480x320
};

enum class AudioOutput {
  Pcm5102a
};

struct Capabilities {
  bool hasDisplay;
  bool hasEncoder;
  bool hasVu;
  bool hasBt;
  bool hasAux;
  bool hasSpdif;
  bool hasTda7719;
  bool hasLocalUi;
};

struct HardwareProfile {
  const char* name;
  Mcu mcu;
  Display display;
  AudioOutput audio;
  Capabilities capabilities;
  bool pinMapComplete;
};

}  // namespace voxone

#if (defined(VOXONE_PROFILE_DESK) + defined(VOXONE_PROFILE_DIN) + \
     defined(VOXONE_PROFILE_SALON) + defined(VOXONE_PROFILE_SALON_DSP)) != 1
#error "Select exactly one VoxOne hardware profile"
#endif

#if defined(VOXONE_PROFILE_DESK)
#include "desk.h"
#elif defined(VOXONE_PROFILE_DIN)
#include "din.h"
#elif defined(VOXONE_PROFILE_SALON)
#include "salon.h"
#elif defined(VOXONE_PROFILE_SALON_DSP)
#include "salon_dsp.h"
#endif

#include "unavailable_hardware.h"

namespace voxone {

static constexpr HardwareProfile activeProfile = {
  VOXONE_PROFILE_NAME,
  VOXONE_PROFILE_MCU,
  VOXONE_PROFILE_DISPLAY,
  VOXONE_PROFILE_AUDIO,
  {
    VOXONE_HAS_DISPLAY,
    VOXONE_HAS_ENCODER,
    VOXONE_HAS_VU,
    VOXONE_HAS_BT,
    VOXONE_HAS_AUX,
    VOXONE_HAS_SPDIF,
    VOXONE_HAS_TDA7719,
    VOXONE_HAS_LOCAL_UI
  },
  VOXONE_PIN_MAP_COMPLETE
};

}  // namespace voxone

#if !VOXONE_PIN_MAP_COMPLETE
  #if defined(VOXONE_PROFILE_DIN)
    #error "DIN profile is not buildable: PCM5102A I2S pin map is not documented"
  #elif defined(VOXONE_PROFILE_SALON)
    #error "SALON profile is not buildable: ST7796S, encoder and PCM5102A pin maps are not documented"
  #elif defined(VOXONE_PROFILE_SALON_DSP)
    #error "SALON_DSP profile is not buildable: ST7796S, encoder, PCM5102A and TDA7719 pin maps are not documented"
  #endif
#endif

#endif
