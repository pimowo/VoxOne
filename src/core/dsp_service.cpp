#include "dsp_service.h"

#include <cmath>
#include <cstring>

namespace voxone {
namespace dsp {
namespace {

int32_t units(float value, float scale) {
  return static_cast<int32_t>(std::lround(value * scale));
}

bool sameOutput(const DspOutput& a, const DspOutput& b) {
  return units(a.trimDb, 2) == units(b.trimDb, 2) &&
         units(a.delayMs, 20) == units(b.delayMs, 20) &&
         a.polarity == b.polarity && a.userMute == b.userMute;
}

bool sameGlobal(const DspGlobal& a, const DspGlobal& b) {
  if (units(a.masterTrimDb, 2) != units(b.masterTrimDb, 2) ||
      a.processingEnabled != b.processingEnabled || a.outputMode != b.outputMode ||
      a.crossoverHz != b.crossoverHz || a.hpfMains != b.hpfMains ||
      a.subRouting != b.subRouting || a.activePresetId != b.activePresetId ||
      a.loudness.enabled != b.loudness.enabled ||
      a.loudness.intensity != b.loudness.intensity ||
      a.limiter.enabled != b.limiter.enabled ||
      units(a.limiter.thresholdDbfs, 2) != units(b.limiter.thresholdDbfs, 2) ||
      units(a.limiter.attackMs, 2) != units(b.limiter.attackMs, 2) ||
      units(a.limiter.releaseMs, .1f) != units(b.limiter.releaseMs, .1f)) return false;
  for (size_t i = 0; i < kOutputCount; ++i)
    if (!sameOutput(a.outputs[i], b.outputs[i])) return false;
  return true;
}

bool sameBand(const PeqBand& a, const PeqBand& b) {
  return a.enabled == b.enabled && a.frequencyHz == b.frequencyHz &&
         units(a.gainDb, 2) == units(b.gainDb, 2) &&
         units(a.q, 10) == units(b.q, 10);
}

bool sameDynamics(const Dynamics& a, const Dynamics& b) {
  return a.enabled == b.enabled &&
         units(a.thresholdDbfs, 2) == units(b.thresholdDbfs, 2) &&
         units(a.ratio, 10) == units(b.ratio, 10) &&
         units(a.attackMs, 1) == units(b.attackMs, 1) &&
         units(a.releaseMs, .1f) == units(b.releaseMs, .1f) &&
         units(a.makeupGainDb, 2) == units(b.makeupGainDb, 2);
}

bool sameWorking(const DspWorkingState& a, const DspWorkingState& b) {
  for (size_t i = 0; i < kPeqBands; ++i)
    if (!sameBand(a.peq[i], b.peq[i])) return false;
  return sameDynamics(a.dynamics, b.dynamics);
}

size_t userIndex(PresetId id) { return id == PresetId::User1 ? 0 : 1; }

}  // namespace

bool DspService::sameTone(const ToneSnapshot& a, const ToneSnapshot& b) {
  return a.bass == b.bass && a.middle == b.middle && a.treble == b.treble;
}

DspServiceError DspService::init(IDspBackend& backend,
                                 const ToneSnapshot& currentTone) {
  if (!validTone(currentTone)) return DspServiceError::InvalidValue;
  initialized_ = false;
  backend_ = &backend;
  global_ = defaultGlobal();
  working_ = factoryPreset(PresetId::Flat).working;
  runtime_ = {};
  runtime_.revision = 1;
  lastObservedTone_ = currentTone;
  for (size_t i = 0; i < 2; ++i) {
    const PresetId id = i == 0 ? PresetId::User1 : PresetId::User2;
    users_[i] = factoryPreset(PresetId::Flat);
    users_[i].id = id;
    users_[i].writable = true;
    std::strcpy(userNames_[i], i == 0 ? "User 1" : "User 2");
    users_[i].name = userNames_[i];
  }
  if (!backend_->init()) {
    runtime_.backendStatus = BackendStatus::Error;
    runtime_.lastError = backend_->lastError();
    return DspServiceError::BackendError;
  }
  initialized_ = true;
  return retryApply();
}

bool DspService::dirty(const ToneSnapshot& currentTone) const {
  const DspPreset active = preset(global_.activePresetId);
  return !active.name || !sameTone(currentTone, active.toneSnapshot) ||
         !sameWorking(working_, active.working);
}

PresetInfo DspService::presetInfo(PresetId id) const {
  const DspPreset p = preset(id);
  return {id, p.name, p.writable, p.name != nullptr};
}

DspPreset DspService::preset(PresetId id) const {
  if (!validPresetId(id)) {
    DspPreset invalid{};
    invalid.name = nullptr;
    return invalid;
  }
  return presetWritable(id) ? users_[userIndex(id)] : factoryPreset(id);
}

DspServiceError DspService::retryApply() {
  if (!ready()) return DspServiceError::NotInitialized;
  if (backend_->apply(global_, working_, runtime_)) {
    runtime_.appliedRevision = runtime_.revision;
    runtime_.backendStatus = backend_->status();
    runtime_.lastError = nullptr;
    return DspServiceError::Ok;
  }
  runtime_.backendStatus = BackendStatus::Error;
  runtime_.lastError = backend_->lastError();
  return DspServiceError::BackendError;
}

DspServiceError DspService::applyChange() {
  ++runtime_.revision;
  return retryApply();
}

DspServiceError DspService::updateGlobal(const DspGlobal& candidate) {
  if (!ready()) return DspServiceError::NotInitialized;
  if (!validGlobal(candidate) || candidate.activePresetId != global_.activePresetId)
    return DspServiceError::InvalidValue;
  if (sameGlobal(global_, candidate)) return DspServiceError::Ok;
  global_ = candidate;
  return applyChange();
}

DspServiceError DspService::updateWorking(const DspWorkingState& candidate) {
  if (!ready()) return DspServiceError::NotInitialized;
  if (!validWorking(candidate)) return DspServiceError::InvalidValue;
  if (sameWorking(working_, candidate)) return DspServiceError::Ok;
  working_ = candidate;
  return applyChange();
}

DspServiceError DspService::setMasterTrim(float value) {
  DspGlobal next = global_; next.masterTrimDb = value; return updateGlobal(next);
}
DspServiceError DspService::setProcessingEnabled(bool value) {
  DspGlobal next = global_; next.processingEnabled = value; return updateGlobal(next);
}
DspServiceError DspService::setOutputMode(OutputMode value) {
  DspGlobal next = global_; next.outputMode = value; return updateGlobal(next);
}
DspServiceError DspService::setSubRouting(SubRouting value) {
  DspGlobal next = global_; next.subRouting = value; return updateGlobal(next);
}
DspServiceError DspService::setCrossover(uint16_t value) {
  DspGlobal next = global_; next.crossoverHz = value; return updateGlobal(next);
}
DspServiceError DspService::setHpfMains(bool value) {
  DspGlobal next = global_; next.hpfMains = value; return updateGlobal(next);
}
DspServiceError DspService::setOutputTrim(size_t index, float value) {
  if (index >= kOutputCount) return DspServiceError::InvalidIndex;
  DspGlobal next = global_; next.outputs[index].trimDb = value; return updateGlobal(next);
}
DspServiceError DspService::setOutputDelay(size_t index, float value) {
  if (index >= kOutputCount) return DspServiceError::InvalidIndex;
  DspGlobal next = global_; next.outputs[index].delayMs = value; return updateGlobal(next);
}
DspServiceError DspService::setOutputPolarity(size_t index, uint16_t value) {
  if (index >= kOutputCount) return DspServiceError::InvalidIndex;
  DspGlobal next = global_; next.outputs[index].polarity = value; return updateGlobal(next);
}
DspServiceError DspService::setOutputMute(size_t index, bool value) {
  if (index >= kOutputCount) return DspServiceError::InvalidIndex;
  DspGlobal next = global_; next.outputs[index].userMute = value; return updateGlobal(next);
}
DspServiceError DspService::setLoudness(const DspLoudness& value) {
  DspGlobal next = global_; next.loudness = value; return updateGlobal(next);
}
DspServiceError DspService::setLimiter(const DspLimiter& value) {
  DspGlobal next = global_; next.limiter = value; return updateGlobal(next);
}
DspServiceError DspService::setPeqBand(size_t index, const PeqBand& value) {
  if (index >= kPeqBands) return DspServiceError::InvalidIndex;
  DspWorkingState next = working_; next.peq[index] = value; return updateWorking(next);
}
DspServiceError DspService::setDynamics(const Dynamics& value) {
  DspWorkingState next = working_; next.dynamics = value; return updateWorking(next);
}
DspServiceError DspService::setMasterMute(bool value) {
  if (!ready()) return DspServiceError::NotInitialized;
  if (runtime_.masterMute == value) return DspServiceError::Ok;
  runtime_.masterMute = value;
  return applyChange();
}

DspServiceError DspService::notifySharedToneChanged(const ToneSnapshot& currentTone) {
  if (!ready()) return DspServiceError::NotInitialized;
  if (!validTone(currentTone)) return DspServiceError::InvalidValue;
  if (sameTone(lastObservedTone_, currentTone)) return DspServiceError::Ok;
  lastObservedTone_ = currentTone;
  return applyChange();
}

DspServiceError DspService::setSubField(SubField field, float value) {
  if (!ready()) return DspServiceError::NotInitialized;
  if (global_.outputMode == OutputMode::Stereo20)
    return DspServiceError::NoActiveSubwoofer;
  DspGlobal next = global_;
  if (!writeSubGroup(next, field, value)) return DspServiceError::InvalidValue;
  return updateGlobal(next);
}
DspServiceError DspService::setSubTrim(float value) {
  return setSubField(SubField::Trim, value);
}
DspServiceError DspService::setSubDelay(float value) {
  return setSubField(SubField::Delay, value);
}
DspServiceError DspService::setSubPolarity(uint16_t value) {
  return setSubField(SubField::Polarity, static_cast<float>(value));
}
DspServiceError DspService::setSubMute(bool value) {
  if (!ready()) return DspServiceError::NotInitialized;
  if (global_.outputMode == OutputMode::Stereo20)
    return DspServiceError::NoActiveSubwoofer;
  DspGlobal next = global_;
  next.outputs[2].userMute = value;
  if (next.outputMode == OutputMode::Stereo22) next.outputs[3].userMute = value;
  return updateGlobal(next);
}
SubGroupValue DspService::subValue(SubField field) const {
  return readSubGroup(global_, field);
}
SubMuteValue DspService::subMute() const {
  if (global_.outputMode == OutputMode::Stereo20) return {};
  const bool first = global_.outputs[2].userMute;
  return {true, global_.outputMode == OutputMode::Stereo22 &&
                    first != global_.outputs[3].userMute, first};
}

OutputRole DspService::role(size_t index) const {
  return outputRole(global_.outputMode, outputEffectiveSubRouting(), index);
}
bool DspService::outputEffectiveMute(size_t index) const {
  return effectiveMute(global_, runtime_.masterMute, index);
}
SubRouting DspService::outputEffectiveSubRouting() const {
  return effectiveSubRouting(global_.outputMode, global_.subRouting);
}

DspServiceError DspService::prepareActivation(PresetId id,
                                               PresetActivation& out) const {
  if (!ready()) return DspServiceError::NotInitialized;
  if (!validPresetId(id)) return DspServiceError::InvalidPreset;
  const DspPreset selected = preset(id);
  if (!selected.name || !validTone(selected.toneSnapshot) ||
      !validWorking(selected.working)) return DspServiceError::InvalidPreset;
  out.id = id;
  out.toneToApply = selected.toneSnapshot;
  out.expectedRevision = runtime_.revision;
  return DspServiceError::Ok;
}
DspServiceError DspService::prepareRestore(PresetActivation& out) const {
  return prepareActivation(global_.activePresetId, out);
}
DspServiceError DspService::commitActivation(const PresetActivation& plan,
                                             const ToneSnapshot& appliedTone) {
  if (!ready()) return DspServiceError::NotInitialized;
  if (!validPresetId(plan.id)) return DspServiceError::InvalidPreset;
  const DspPreset selected = preset(plan.id);
  if (plan.expectedRevision != runtime_.revision || !selected.name ||
      !sameTone(plan.toneToApply, selected.toneSnapshot) ||
      !sameTone(appliedTone, selected.toneSnapshot))
    return DspServiceError::StaleActivation;
  global_.activePresetId = plan.id;
  working_ = selected.working;
  lastObservedTone_ = appliedTone;
  return applyChange();
}

DspServiceError DspService::saveUserPreset(PresetId id,
                                            const ToneSnapshot& currentTone,
                                            const char* name) {
  if (!ready()) return DspServiceError::NotInitialized;
  if (!validPresetId(id)) return DspServiceError::InvalidPreset;
  if (!presetWritable(id)) return DspServiceError::ReadOnlyPreset;
  if (!validTone(currentTone)) return DspServiceError::InvalidValue;
  if (name && !isValidDspPresetName(name))
    return DspServiceError::InvalidName;
  const size_t index = userIndex(id);
  if (name) std::memmove(userNames_[index], name, std::strlen(name) + 1);
  users_[index].toneSnapshot = currentTone;
  users_[index].working = working_;
  global_.activePresetId = id;
  lastObservedTone_ = currentTone;
  return applyChange();
}

DspServiceError DspService::renameUserPreset(PresetId id, const char* name) {
  if (!ready()) return DspServiceError::NotInitialized;
  if (!validPresetId(id)) return DspServiceError::InvalidPreset;
  if (!presetWritable(id)) return DspServiceError::ReadOnlyPreset;
  if (!isValidDspPresetName(name)) return DspServiceError::InvalidName;
  char* target = userNames_[userIndex(id)];
  if (std::strcmp(target, name) != 0)
    std::memmove(target, name, std::strlen(name) + 1);
  return DspServiceError::Ok;  // Name is metadata, not applied DSP state.
}

}  // namespace dsp
}  // namespace voxone
