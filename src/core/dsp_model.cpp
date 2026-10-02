#include "dsp_model.h"

#include <cmath>

namespace voxone {
namespace dsp {
namespace {

bool stepped(float value, float low, float high, float step) {
  if (!std::isfinite(value) || value < low || value > high) return false;
  const float steps = (value - low) / step;
  return std::fabs(steps - std::round(steps)) < 0.001f;
}

bool sameTone(const ToneSnapshot& a, const ToneSnapshot& b) {
  return a.bass == b.bass && a.middle == b.middle && a.treble == b.treble;
}

bool sameWorking(const DspWorkingState& a, const DspWorkingState& b) {
  for (size_t i = 0; i < kPeqBands; ++i) {
    if (a.peq[i].enabled != b.peq[i].enabled ||
        a.peq[i].frequencyHz != b.peq[i].frequencyHz ||
        a.peq[i].gainDb != b.peq[i].gainDb || a.peq[i].q != b.peq[i].q) return false;
  }
  const Dynamics& x = a.dynamics;
  const Dynamics& y = b.dynamics;
  return x.enabled == y.enabled && x.thresholdDbfs == y.thresholdDbfs &&
         x.ratio == y.ratio && x.attackMs == y.attackMs &&
         x.releaseMs == y.releaseMs && x.makeupGainDb == y.makeupGainDb;
}

bool sameGlobal(const DspGlobal& a, const DspGlobal& b) {
  if (a.masterTrimDb != b.masterTrimDb || a.processingEnabled != b.processingEnabled ||
      a.outputMode != b.outputMode || a.crossoverHz != b.crossoverHz ||
      a.hpfMains != b.hpfMains || a.subRouting != b.subRouting ||
      a.loudness.enabled != b.loudness.enabled || a.loudness.intensity != b.loudness.intensity ||
      a.limiter.enabled != b.limiter.enabled ||
      a.limiter.thresholdDbfs != b.limiter.thresholdDbfs ||
      a.limiter.attackMs != b.limiter.attackMs ||
      a.limiter.releaseMs != b.limiter.releaseMs ||
      a.activePresetId != b.activePresetId) return false;
  for (size_t i = 0; i < kOutputCount; ++i) {
    if (a.outputs[i].trimDb != b.outputs[i].trimDb ||
        a.outputs[i].delayMs != b.outputs[i].delayMs ||
        a.outputs[i].polarity != b.outputs[i].polarity ||
        a.outputs[i].userMute != b.outputs[i].userMute) return false;
  }
  return true;
}

float subValue(const DspOutput& output, SubField field) {
  switch (field) {
    case SubField::Trim: return output.trimDb;
    case SubField::Delay: return output.delayMs;
    case SubField::Polarity: return static_cast<float>(output.polarity);
  }
  return 0;
}

bool validSubValue(SubField field, float value) {
  switch (field) {
    case SubField::Trim: return stepped(value, -24, 6, .5f);
    case SubField::Delay: return stepped(value, 0, 5, .05f);
    case SubField::Polarity: return value == 0 || value == 180;
  }
  return false;
}

void setSubValue(DspOutput& output, SubField field, float value) {
  switch (field) {
    case SubField::Trim: output.trimDb = value; break;
    case SubField::Delay: output.delayMs = value; break;
    case SubField::Polarity: output.polarity = static_cast<uint16_t>(value); break;
  }
}

}  // namespace

DspCapabilities demoCapabilities() { return {}; }

DspWorkingState defaultWorking() {
  DspWorkingState working{};
  const uint16_t frequencies[kPeqBands] = {80, 250, 1000, 4000, 10000};
  for (size_t i = 0; i < kPeqBands; ++i) working.peq[i].frequencyHz = frequencies[i];
  return working;
}

DspGlobal defaultGlobal() { return {}; }

DspBypassPolicy bypassFor(const DspGlobal& global) {
  const bool bypass = !global.processingEnabled;
  return {bypass, bypass, bypass, bypass, false, false, false, false, false, false};
}

bool validPresetId(PresetId id) { return static_cast<uint8_t>(id) < kPresetCount; }
bool presetWritable(PresetId id) {
  return id == PresetId::User1 || id == PresetId::User2;
}

bool validTone(const ToneSnapshot& tone) {
  return tone.bass >= -6 && tone.bass <= 6 && tone.middle >= -6 && tone.middle <= 6 &&
         tone.treble >= -6 && tone.treble <= 6;
}

bool validPeqBand(const PeqBand& band) {
  return band.frequencyHz >= 20 && band.frequencyHz <= 20000 &&
         stepped(band.gainDb, -12, 12, .5f) && stepped(band.q, .3f, 10, .1f);
}

bool validDynamics(const Dynamics& dynamics) {
  return stepped(dynamics.thresholdDbfs, -40, 0, .5f) &&
         stepped(dynamics.ratio, 1, 8, .1f) &&
         stepped(dynamics.attackMs, 1, 100, 1) &&
         stepped(dynamics.releaseMs, 20, 1000, 10) &&
         stepped(dynamics.makeupGainDb, 0, 12, .5f);
}

bool validWorking(const DspWorkingState& working) {
  if (!validDynamics(working.dynamics)) return false;
  for (const PeqBand& band : working.peq) if (!validPeqBand(band)) return false;
  return true;
}

bool validGlobal(const DspGlobal& global) {
  if (!stepped(global.masterTrimDb, -24, 6, .5f) ||
      static_cast<uint8_t>(global.outputMode) > static_cast<uint8_t>(OutputMode::Stereo22) ||
      global.crossoverHz < 40 || global.crossoverHz > 200 ||
      static_cast<uint8_t>(global.subRouting) > static_cast<uint8_t>(SubRouting::Stereo) ||
      global.loudness.intensity > 100 || !validPresetId(global.activePresetId) ||
      !stepped(global.limiter.thresholdDbfs, -24, 0, .5f) ||
      !stepped(global.limiter.attackMs, .5f, 20, .5f) ||
      !stepped(global.limiter.releaseMs, 20, 1000, 10)) return false;
  for (const DspOutput& output : global.outputs) {
    if (!stepped(output.trimDb, -24, 6, .5f) ||
        !stepped(output.delayMs, 0, 5, .05f) ||
        (output.polarity != 0 && output.polarity != 180)) return false;
  }
  return true;
}

DspPreset factoryPreset(PresetId id) {
  DspPreset preset{};
  preset.id = id;
  preset.working = defaultWorking();
  switch (id) {
    case PresetId::Flat: preset.name = "Flat"; break;
    case PresetId::Bass: preset.name = "Bass"; preset.toneSnapshot = {6, -2, 0}; break;
    case PresetId::Rock: preset.name = "Rock"; preset.toneSnapshot = {5, -2, 4}; break;
    case PresetId::Pop: preset.name = "Pop"; preset.toneSnapshot = {3, 2, 3}; break;
    case PresetId::Mowa:
      preset.name = "Mowa"; preset.toneSnapshot = {-4, 4, 2};
      preset.working.dynamics.enabled = true; break;
    case PresetId::Noc:
      preset.name = "Noc"; preset.toneSnapshot = {-2, 1, -1};
      preset.working.dynamics.enabled = true; break;
    default: preset.name = nullptr; break;
  }
  return preset;
}

SubRouting effectiveSubRouting(OutputMode mode, SubRouting preferred) {
  return mode == OutputMode::Stereo21 ? SubRouting::Sum : preferred;
}

OutputRole outputRole(OutputMode mode, SubRouting routing, size_t index) {
  if (index >= kOutputCount) return OutputRole::Off;
  if (index == 0) return OutputRole::Left;
  if (index == 1) return OutputRole::Right;
  switch (mode) {
    case OutputMode::Stereo20: return OutputRole::Off;
    case OutputMode::Stereo21: return index == 2 ? OutputRole::Sub : OutputRole::Off;
    case OutputMode::Stereo22:
      if (routing == SubRouting::Sum) return OutputRole::SubMono;
      if (routing == SubRouting::Stereo)
        return index == 2 ? OutputRole::SubLeft : OutputRole::SubRight;
      return OutputRole::Off;
  }
  return OutputRole::Off;
}

bool effectiveMute(const DspGlobal& global, bool masterMute, size_t index) {
  return index >= kOutputCount || masterMute || global.outputs[index].userMute ||
         outputRole(global.outputMode, effectiveSubRouting(global.outputMode, global.subRouting),
                    index) == OutputRole::Off;
}

SubGroupValue readSubGroup(const DspGlobal& global, SubField field) {
  if (global.outputMode == OutputMode::Stereo20 ||
      static_cast<uint8_t>(global.outputMode) > static_cast<uint8_t>(OutputMode::Stereo22) ||
      static_cast<uint8_t>(field) > static_cast<uint8_t>(SubField::Polarity)) return {};
  const float first = subValue(global.outputs[2], field);
  return {true, global.outputMode == OutputMode::Stereo22 &&
                    first != subValue(global.outputs[3], field), first};
}

bool writeSubGroup(DspGlobal& global, SubField field, float value) {
  if (global.outputMode == OutputMode::Stereo20 ||
      static_cast<uint8_t>(global.outputMode) > static_cast<uint8_t>(OutputMode::Stereo22) ||
      !validSubValue(field, value)) return false;
  setSubValue(global.outputs[2], field, value);
  if (global.outputMode == OutputMode::Stereo22) setSubValue(global.outputs[3], field, value);
  return true;
}

bool DemoBackend::init() {
  status_ = BackendStatus::DemoNoHardware;
  return true;
}

bool DemoBackend::apply(const DspGlobal& global, const DspWorkingState& working,
                        const DspRuntimeState&) {
  return status_ == BackendStatus::DemoNoHardware && validGlobal(global) &&
         validWorking(working);
}

DspModel::DspModel(IDspBackend& backend) : backend_(backend), working_(defaultWorking()) {
  userPresets_[0] = factoryPreset(PresetId::Flat);
  userPresets_[0].id = PresetId::User1;
  userPresets_[0].name = "User 1";
  userPresets_[0].writable = true;
  userPresets_[1] = userPresets_[0];
  userPresets_[1].id = PresetId::User2;
  userPresets_[1].name = "User 2";
}

bool DspModel::init() {
  if (!backend_.init()) {
    runtime_.backendStatus = BackendStatus::Error;
    runtime_.lastError = backend_.lastError();
    return false;
  }
  runtime_.backendStatus = backend_.status();
  if (!backend_.apply(global_, working_, runtime_)) {
    runtime_.backendStatus = BackendStatus::Error;
    runtime_.lastError = backend_.lastError();
    return false;
  }
  runtime_.appliedRevision = runtime_.revision;
  return true;
}

DspPreset DspModel::preset(PresetId id) const {
  if (id == PresetId::User1) return userPresets_[0];
  if (id == PresetId::User2) return userPresets_[1];
  return factoryPreset(id);
}

bool DspModel::isDirty(const ToneSnapshot& currentTone) const {
  const DspPreset active = preset(global_.activePresetId);
  return !sameTone(currentTone, active.toneSnapshot) ||
         !sameWorking(working_, active.working);
}

void DspModel::changed() {
  ++runtime_.revision;
  if (backend_.apply(global_, working_, runtime_)) {
    runtime_.appliedRevision = runtime_.revision;
    runtime_.backendStatus = backend_.status();
    runtime_.lastError = nullptr;
  } else {
    runtime_.backendStatus = BackendStatus::Error;
    runtime_.lastError = backend_.lastError();
  }
}

bool DspModel::setGlobal(const DspGlobal& candidate) {
  // Preset selection must go through activatePreset(), which also replaces working state.
  if (!validGlobal(candidate) || candidate.activePresetId != global_.activePresetId) return false;
  if (!sameGlobal(global_, candidate)) { global_ = candidate; changed(); }
  return true;
}

bool DspModel::setWorking(const DspWorkingState& candidate) {
  if (!validWorking(candidate)) return false;
  if (!sameWorking(working_, candidate)) { working_ = candidate; changed(); }
  return true;
}

bool DspModel::setOutput(size_t index, const DspOutput& output) {
  if (index >= kOutputCount) return false;
  DspGlobal candidate = global_;
  candidate.outputs[index] = output;
  return setGlobal(candidate);
}

bool DspModel::setPeqBand(size_t index, const PeqBand& band) {
  if (index >= kPeqBands) return false;
  DspWorkingState candidate = working_;
  candidate.peq[index] = band;
  return setWorking(candidate);
}

bool DspModel::setMasterMute(bool mute) {
  if (runtime_.masterMute != mute) { runtime_.masterMute = mute; changed(); }
  return true;
}

bool DspModel::activatePreset(PresetId id, ToneSnapshot& requestedTone) {
  if (!validPresetId(id)) return false;
  const DspPreset selected = preset(id);
  if (!selected.name || !validTone(selected.toneSnapshot) ||
      !validWorking(selected.working)) return false;
  requestedTone = selected.toneSnapshot;
  global_.activePresetId = id;
  working_ = selected.working;
  changed();
  return true;
}

bool DspModel::saveUserPreset(PresetId id, const ToneSnapshot& currentTone) {
  if (!presetWritable(id) || !validTone(currentTone) || !validWorking(working_)) return false;
  DspPreset& target = userPresets_[id == PresetId::User1 ? 0 : 1];
  target.toneSnapshot = currentTone;
  target.working = working_;
  global_.activePresetId = id;
  changed();
  return true;
}

}  // namespace dsp
}  // namespace voxone
