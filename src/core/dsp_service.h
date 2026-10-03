#ifndef VOXONE_DSP_SERVICE_H
#define VOXONE_DSP_SERVICE_H

#include "dsp_model.h"

namespace voxone {
namespace dsp {

enum class DspServiceError : uint8_t {
  Ok, NotInitialized, InvalidValue, InvalidIndex, InvalidPreset,
  ReadOnlyPreset, NoActiveSubwoofer, InvalidName, StaleActivation,
  BackendError
};

struct PresetInfo {
  PresetId id;
  const char* name;
  bool writable;
  bool initialized;
};

struct SubMuteValue {
  bool active = false;
  bool mixed = false;
  bool value = false;
  SubMuteValue() = default;
  SubMuteValue(bool isActive, bool isMixed, bool current)
      : active(isActive), mixed(isMixed), value(current) {}
};

// Prepare before changing Config Tone, then commit with the Tone actually applied.
struct PresetActivation {
  PresetId id = PresetId::Flat;
  ToneSnapshot toneToApply{};
  uint32_t expectedRevision = 0;
};

class DspService {
 public:
  DspService() = default;
  DspService(const DspService&) = delete;
  DspService& operator=(const DspService&) = delete;
  DspServiceError init(IDspBackend& backend, const ToneSnapshot& currentTone);

  const DspGlobal& global() const { return global_; }
  const DspWorkingState& working() const { return working_; }
  const DspRuntimeState& runtime() const { return runtime_; }
  bool dirty(const ToneSnapshot& currentTone) const;
  PresetInfo presetInfo(PresetId id) const;
  DspPreset preset(PresetId id) const;

  DspServiceError setMasterTrim(float value);
  DspServiceError setProcessingEnabled(bool value);
  DspServiceError setOutputMode(OutputMode value);
  DspServiceError setSubRouting(SubRouting value);
  DspServiceError setCrossover(uint16_t value);
  DspServiceError setHpfMains(bool value);
  DspServiceError setOutputTrim(size_t index, float value);
  DspServiceError setOutputDelay(size_t index, float value);
  DspServiceError setOutputPolarity(size_t index, uint16_t value);
  DspServiceError setOutputMute(size_t index, bool value);
  DspServiceError setLoudness(const DspLoudness& value);
  DspServiceError setLimiter(const DspLimiter& value);
  DspServiceError setPeqBand(size_t index, const PeqBand& value);
  DspServiceError setDynamics(const Dynamics& value);
  DspServiceError setMasterMute(bool value);
  DspServiceError notifySharedToneChanged(const ToneSnapshot& currentTone);

  DspServiceError setSubTrim(float value);
  DspServiceError setSubDelay(float value);
  DspServiceError setSubPolarity(uint16_t value);
  DspServiceError setSubMute(bool value);
  SubGroupValue subValue(SubField field) const;
  SubMuteValue subMute() const;

  OutputRole role(size_t index) const;
  bool outputEffectiveMute(size_t index) const;
  SubRouting outputEffectiveSubRouting() const;

  DspServiceError prepareActivation(PresetId id, PresetActivation& out) const;
  DspServiceError prepareRestore(PresetActivation& out) const;
  DspServiceError commitActivation(const PresetActivation& plan,
                                   const ToneSnapshot& appliedTone);
  DspServiceError saveUserPreset(PresetId id, const ToneSnapshot& currentTone,
                                 const char* name = nullptr);
  DspServiceError renameUserPreset(PresetId id, const char* name);
  DspServiceError retryApply();

 private:
  DspServiceError updateGlobal(const DspGlobal& candidate);
  DspServiceError updateWorking(const DspWorkingState& candidate);
  DspServiceError setSubField(SubField field, float value);
  DspServiceError applyChange();
  static bool sameTone(const ToneSnapshot& a, const ToneSnapshot& b);
  bool ready() const { return initialized_; }

  IDspBackend* backend_ = nullptr;
  bool initialized_ = false;
  DspGlobal global_{};
  DspWorkingState working_{};
  DspRuntimeState runtime_{};
  DspPreset users_[2]{};
  char userNames_[2][kMaxDspPresetNameBytes + 1]{};
  ToneSnapshot lastObservedTone_{};  // Projection of Config Tone, never its owner.
};

}  // namespace dsp
}  // namespace voxone

#endif
