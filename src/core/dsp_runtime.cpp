#include "dsp_runtime.h"

#if defined(VOXONE_PROFILE_A0)
#include <Arduino.h>

namespace voxone {
namespace dsp {
namespace {

DemoBackend demoBackend;
DspService service;

}  // namespace

DspServiceError initDspRuntime(const ToneSnapshot& currentTone) {
  const DspServiceError result = service.init(demoBackend, currentTone);
  const DspRuntimeState& state = service.runtime();
  const char* backend = state.backendStatus == BackendStatus::DemoNoHardware
                            ? "demo" : "error";
  const char* preset = service.presetInfo(service.global().activePresetId).name;
  Serial.printf("[DSP] backend=%s hw=0 preset=%s rev=%lu applied=%lu dirty=%u\n",
                backend, preset ? preset : "?",
                static_cast<unsigned long>(state.revision),
                static_cast<unsigned long>(state.appliedRevision),
                static_cast<unsigned>(service.dirty(currentTone)));
  return result;
}

DspService& activeDspService() { return service; }

}  // namespace dsp
}  // namespace voxone
#endif
