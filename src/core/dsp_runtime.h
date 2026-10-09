#ifndef VOXONE_DSP_RUNTIME_H
#define VOXONE_DSP_RUNTIME_H

#if defined(VOXONE_PROFILE_A0)
#include "dsp_service.h"

namespace voxone {
namespace dsp {

// Called once after Config::init(); Config remains the owner of Tone.
DspServiceError initDspRuntime(const ToneSnapshot& currentTone);

// The service has static lifetime. Callers must use it after initDspRuntime().
DspService& activeDspService();

}  // namespace dsp
}  // namespace voxone
#endif

#endif
