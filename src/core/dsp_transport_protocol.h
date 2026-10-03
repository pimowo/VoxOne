#ifndef VOXONE_DSP_TRANSPORT_PROTOCOL_H
#define VOXONE_DSP_TRANSPORT_PROTOCOL_H

#include "dsp_service.h"

#include <cstddef>
#include <cstdint>

namespace voxone {
namespace dsp {

// WebSocket frames: dsp.set=rid,param,index,value;
// dsp.activate=rid,preset; dsp.restore=rid; dsp.save=rid,user[,name];
// dsp.rename=rid,user,name. IDs are decimal; names are raw UTF-8.
constexpr size_t kDspWsFrameMaxBytes = 80;

enum class DspOperation : uint8_t { Set, Activate, Restore, Save, Rename };
enum class DspParameter : uint8_t {
  MasterTrim = 1, ProcessingEnabled, OutputMode, Crossover, HpfMains,
  SubRouting, OutputTrim, OutputDelay, OutputPolarity, OutputUserMute,
  LoudnessEnabled, LoudnessIntensity, LimiterEnabled, LimiterThreshold,
  LimiterAttack, LimiterRelease, MasterMute, PeqEnabled, PeqFrequency,
  PeqGain, PeqQ, DynamicsEnabled, DynamicsThreshold, DynamicsRatio,
  DynamicsAttack, DynamicsRelease, DynamicsMakeup, SubTrim, SubDelay,
  SubPolarity, SubMute
};

enum class DspParseError : uint8_t {
  Ok, NotDsp, Malformed, TooLarge, InvalidParameter, InvalidName
};

struct DspCommand {
  uint32_t requestId = 0;
  uint32_t clientId = 0;
  float value = 0;
  DspOperation operation = DspOperation::Set;
  DspParameter parameter = DspParameter::MasterTrim;
  uint8_t index = 0;
  PresetId presetId = PresetId::Flat;
  char name[kMaxDspPresetNameBytes + 1]{};
};

static_assert(sizeof(DspCommand) <= 48, "DSP transport queue item too large");

DspParseError parseDspWsFrame(const char* frame, size_t length, DspCommand& out);
DspServiceError applyDspSet(DspService& service, const DspCommand& command);

}  // namespace dsp
}  // namespace voxone

#endif
