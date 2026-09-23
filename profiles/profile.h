#ifndef YOVOXONE_PROFILE_H
#define YOVOXONE_PROFILE_H

namespace yovoxone {

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

}  // namespace yovoxone

#if (defined(YOVOXONE_PROFILE_DESK) + defined(YOVOXONE_PROFILE_DIN) + \
     defined(YOVOXONE_PROFILE_SALON) + defined(YOVOXONE_PROFILE_SALON_DSP)) != 1
#error "Select exactly one yoVoxOne hardware profile"
#endif

#if defined(YOVOXONE_PROFILE_DESK)
#include "desk.h"
#elif defined(YOVOXONE_PROFILE_DIN)
#include "din.h"
#elif defined(YOVOXONE_PROFILE_SALON)
#include "salon.h"
#elif defined(YOVOXONE_PROFILE_SALON_DSP)
#include "salon_dsp.h"
#endif

#include "unavailable_hardware.h"

namespace yovoxone {

static constexpr HardwareProfile activeProfile = {
  YOVOXONE_PROFILE_NAME,
  YOVOXONE_PROFILE_MCU,
  YOVOXONE_PROFILE_DISPLAY,
  YOVOXONE_PROFILE_AUDIO,
  {
    YOVOXONE_HAS_DISPLAY,
    YOVOXONE_HAS_ENCODER,
    YOVOXONE_HAS_VU,
    YOVOXONE_HAS_BT,
    YOVOXONE_HAS_AUX,
    YOVOXONE_HAS_SPDIF,
    YOVOXONE_HAS_TDA7719,
    YOVOXONE_HAS_LOCAL_UI
  },
  YOVOXONE_PIN_MAP_COMPLETE
};

}  // namespace yovoxone

#if !YOVOXONE_PIN_MAP_COMPLETE
  #if defined(YOVOXONE_PROFILE_DIN)
    #error "DIN profile is not buildable: PCM5102A I2S pin map is not documented"
  #elif defined(YOVOXONE_PROFILE_SALON)
    #error "SALON profile is not buildable: ST7796S, encoder and PCM5102A pin maps are not documented"
  #elif defined(YOVOXONE_PROFILE_SALON_DSP)
    #error "SALON_DSP profile is not buildable: ST7796S, encoder, PCM5102A and TDA7719 pin maps are not documented"
  #endif
#endif

#endif
