#ifndef VOXONE_RUNTIME_AUDIO_OUTPUT_H
#define VOXONE_RUNTIME_AUDIO_OUTPUT_H

#include "runtime_hardware_config.h"

namespace voxone {
namespace hardware {

struct RuntimeAudioActivationPlan {
  bool initializeSerialI2s;
  bool localPlaybackAvailable;
};

constexpr RuntimeAudioActivationPlan runtimeAudioActivationPlan(
    RuntimeAudioOutput output) {
  return output == RuntimeAudioOutput::Pcm5102a ||
                 output == RuntimeAudioOutput::Max98357 ||
                 output == RuntimeAudioOutput::Max98357Stereo
             ? RuntimeAudioActivationPlan{true, true}
             : RuntimeAudioActivationPlan{false, false};
}

}  // namespace hardware
}  // namespace voxone

#endif
