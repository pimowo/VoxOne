#include "../src/core/dsp_model.h"

#include <cassert>
#include <cmath>
#include <limits>

using namespace voxone::dsp;

int main() {
  const DspCapabilities capabilities = demoCapabilities();
  assert(capabilities.demoMode && !capabilities.hasDspHardware);
  assert(capabilities.peqBands == 5 && capabilities.outputCount == 4);
  assert(capabilities.supportedOutputModes == 0x07);
  assert(capabilities.maxDelayMs == 5 && capabilities.sampleRateHz == 48000);

  DspGlobal global = defaultGlobal();
  assert(validGlobal(global));
  assert(outputRole(OutputMode::Stereo20, SubRouting::Sum, 0) == OutputRole::Left);
  assert(outputRole(OutputMode::Stereo20, SubRouting::Sum, 1) == OutputRole::Right);
  assert(outputRole(OutputMode::Stereo20, SubRouting::Sum, 2) == OutputRole::Off);
  assert(outputRole(OutputMode::Stereo20, SubRouting::Sum, 3) == OutputRole::Off);
  assert(outputRole(OutputMode::Stereo21, SubRouting::Stereo, 2) == OutputRole::Sub);
  assert(outputRole(OutputMode::Stereo21, SubRouting::Stereo, 3) == OutputRole::Off);
  assert(effectiveSubRouting(OutputMode::Stereo21, SubRouting::Stereo) == SubRouting::Sum);
  assert(outputRole(OutputMode::Stereo22, SubRouting::Sum, 2) == OutputRole::SubMono);
  assert(outputRole(OutputMode::Stereo22, SubRouting::Sum, 3) == OutputRole::SubMono);
  assert(outputRole(OutputMode::Stereo22, SubRouting::Stereo, 2) == OutputRole::SubLeft);
  assert(outputRole(OutputMode::Stereo22, SubRouting::Stereo, 3) == OutputRole::SubRight);
  assert(outputRole(OutputMode::Stereo22, SubRouting::Stereo, 4) == OutputRole::Off);

  global.outputMode = OutputMode::Stereo22;
  global.outputs[3].userMute = true;
  assert(!effectiveMute(global, false, 2));
  assert(effectiveMute(global, false, 3));
  assert(effectiveMute(global, true, 2));
  global.outputMode = OutputMode::Stereo21;
  assert(effectiveMute(global, false, 3));
  global.outputMode = OutputMode::Stereo22;
  assert(global.outputs[3].userMute && effectiveMute(global, false, 3));
  global.outputs[3].userMute = false;

  assert(writeSubGroup(global, SubField::Trim, -2.5f));
  SubGroupValue sub = readSubGroup(global, SubField::Trim);
  assert(sub.active && !sub.mixed && sub.value == -2.5f);
  global.outputs[3].trimDb = -1;
  sub = readSubGroup(global, SubField::Trim);
  assert(sub.active && sub.mixed);
  assert(writeSubGroup(global, SubField::Delay, 1.05f));
  assert(global.outputs[2].delayMs == global.outputs[3].delayMs);
  assert(writeSubGroup(global, SubField::Polarity, 180));
  assert(global.outputs[2].polarity == 180 && global.outputs[3].polarity == 180);
  global.outputMode = OutputMode::Stereo21;
  assert(writeSubGroup(global, SubField::Trim, -3));
  assert(global.outputs[2].trimDb == -3 && global.outputs[3].trimDb == -1);
  global.outputMode = OutputMode::Stereo20;
  assert(!readSubGroup(global, SubField::Trim).active);
  assert(!writeSubGroup(global, SubField::Trim, 0));

  DspWorkingState working = defaultWorking();
  assert(validWorking(working));
  working.peq[0].frequencyHz = 19;
  assert(!validWorking(working));
  working.peq[0].frequencyHz = 20;
  working.peq[0].gainDb = .25f;
  assert(!validPeqBand(working.peq[0]));
  working.peq[0].gainDb = std::numeric_limits<float>::quiet_NaN();
  assert(!validPeqBand(working.peq[0]));
  working.peq[0].gainDb = 0;
  working.peq[0].q = std::numeric_limits<float>::infinity();
  assert(!validPeqBand(working.peq[0]));
  working.peq[0].q = 1;
  assert(validWorking(working));

  global = defaultGlobal();
  global.outputs[0].delayMs = 5.05f;
  assert(!validGlobal(global));
  global.outputs[0].delayMs = .03f;
  assert(!validGlobal(global));
  global.outputs[0].delayMs = 5;
  assert(validGlobal(global));
  global.limiter.thresholdDbfs = -24.5f;
  assert(!validGlobal(global));
  global.limiter.thresholdDbfs = -3;
  global.limiter.attackMs = .7f;
  assert(!validGlobal(global));
  global.limiter.attackMs = 5;
  global.limiter.releaseMs = 25;
  assert(!validGlobal(global));
  global.limiter.releaseMs = 200;
  assert(validGlobal(global));
  global.masterTrimDb = std::numeric_limits<float>::infinity();
  assert(!validGlobal(global));
  global.masterTrimDb = 0;
  global.outputMode = static_cast<OutputMode>(99);
  assert(!validGlobal(global));

  Dynamics dynamics{};
  assert(validDynamics(dynamics));
  dynamics.ratio = 8.1f;
  assert(!validDynamics(dynamics));
  dynamics.ratio = 2;
  dynamics.thresholdDbfs = -40.5f;
  assert(!validDynamics(dynamics));
  dynamics.thresholdDbfs = -18;
  dynamics.makeupGainDb = std::numeric_limits<float>::quiet_NaN();
  assert(!validDynamics(dynamics));

  for (int i = 0; i < 6; ++i) {
    const DspPreset preset = factoryPreset(static_cast<PresetId>(i));
    assert(preset.name && !preset.writable && validTone(preset.toneSnapshot));
    assert(validWorking(preset.working));
  }
  assert(!presetWritable(PresetId::Flat));
  assert(presetWritable(PresetId::User1) && presetWritable(PresetId::User2));

  DemoBackend backend;
  DspModel model(backend);
  assert(model.init());
  assert(model.runtime().backendStatus == BackendStatus::DemoNoHardware);
  assert(model.runtime().revision == model.runtime().appliedRevision);
  ToneSnapshot tone{};
  assert(!model.isDirty(tone));
  assert(model.activatePreset(PresetId::Rock, tone));
  assert(tone.bass == 5 && tone.middle == -2 && tone.treble == 4);
  assert(!model.isDirty(tone));
  assert(model.runtime().revision == model.runtime().appliedRevision);
  ToneSnapshot changedTone = tone;
  changedTone.bass = 4;
  assert(model.isDirty(changedTone));
  working = model.working();
  working.peq[1].gainDb = 1;
  assert(model.setWorking(working) && model.isDirty(tone));
  assert(model.activatePreset(PresetId::Rock, tone) && !model.isDirty(tone));
  working = model.working();
  working.dynamics.enabled = !working.dynamics.enabled;
  assert(model.setWorking(working) && model.isDirty(tone));
  assert(!model.saveUserPreset(PresetId::Rock, tone));
  assert(model.saveUserPreset(PresetId::User1, tone));
  assert(!model.isDirty(tone));
  assert(model.preset(PresetId::User1).writable);
  assert(!model.setOutput(kOutputCount, model.global().outputs[0]));
  assert(!model.setPeqBand(kPeqBands, model.working().peq[0]));
  working.peq[0].frequencyHz = 20001;
  assert(!model.setWorking(working));

  global = model.global();
  global.activePresetId = PresetId::Bass;
  assert(!model.setGlobal(global));
  global = model.global();
  global.processingEnabled = false;
  assert(model.setGlobal(global));
  const DspBypassPolicy bypass = bypassFor(model.global());
  assert(bypass.tone && bypass.peq && bypass.loudness && bypass.dynamics);
  assert(!bypass.crossover && !bypass.routing && !bypass.delay && !bypass.trim &&
         !bypass.limiter && !bypass.mute);
  assert(model.setMasterMute(true));
  assert(model.runtime().masterMute);
  assert(model.runtime().revision == model.runtime().appliedRevision);
}
