#include "../src/core/dsp_service.h"

#include <cassert>
#include <cstring>
#include <limits>

using namespace voxone::dsp;

namespace {

bool ok(DspServiceError error) { return error == DspServiceError::Ok; }

struct CountingBackend : IDspBackend {
  bool failInit = false;
  bool failApply = false;
  unsigned applies = 0;
  bool init() override { return !failInit; }
  bool apply(const DspGlobal& global, const DspWorkingState& working,
             const DspRuntimeState&) override {
    ++applies;
    return !failApply && validGlobal(global) && validWorking(working);
  }
  BackendStatus status() const override { return BackendStatus::DemoNoHardware; }
  const char* lastError() const override { return "backend failed"; }
};

void testInitAndGlobal() {
  DemoBackend demo;
  DspService service;
  const ToneSnapshot flat{};
  assert(ok(service.init(demo, flat)));
  assert(service.global().activePresetId == PresetId::Flat);
  assert(!service.dirty(flat));
  assert(!service.runtime().masterMute);
  assert(service.runtime().revision == 1);
  assert(service.runtime().appliedRevision == 1);
  assert(service.runtime().backendStatus == BackendStatus::DemoNoHardware);
  assert(service.presetInfo(PresetId::Flat).initialized);
  assert(!service.presetInfo(PresetId::Flat).writable);
  assert(service.presetInfo(PresetId::User1).writable);
  assert(!service.presetInfo(static_cast<PresetId>(99)).initialized);

  const uint32_t before = service.runtime().revision;
  assert(ok(service.setMasterTrim(.5f)));
  assert(service.runtime().revision == before + 1);
  assert(service.runtime().appliedRevision == service.runtime().revision);
  assert(ok(service.setMasterTrim(.5f)));
  assert(ok(service.setMasterTrim(.50000001f)));
  assert(service.runtime().revision == before + 1);
  assert(service.setMasterTrim(.25f) == DspServiceError::InvalidValue);
  assert(service.setMasterTrim(std::numeric_limits<float>::quiet_NaN()) ==
         DspServiceError::InvalidValue);
  assert(service.runtime().revision == before + 1);
  assert(ok(service.setProcessingEnabled(false)));
  assert(ok(service.setCrossover(120)));
  assert(service.setCrossover(201) == DspServiceError::InvalidValue);
  assert(ok(service.setHpfMains(false)));
  assert(ok(service.setSubRouting(SubRouting::Stereo)));
  assert(ok(service.setOutputTrim(0, -2.5f)));
  assert(ok(service.setOutputDelay(0, 1.05f)));
  assert(ok(service.setOutputPolarity(0, 180)));
  assert(service.setOutputPolarity(0, 90) == DspServiceError::InvalidValue);
  assert(service.setOutputTrim(kOutputCount, 0) == DspServiceError::InvalidIndex);
  assert(ok(service.setOutputMute(3, true)));
  DspLoudness loudness{}; loudness.enabled = true; loudness.intensity = 60;
  assert(ok(service.setLoudness(loudness)));
  loudness.intensity = 101;
  assert(service.setLoudness(loudness) == DspServiceError::InvalidValue);
  DspLimiter limiter = service.global().limiter;
  limiter.thresholdDbfs = -6;
  assert(ok(service.setLimiter(limiter)));
  limiter.attackMs = .7f;
  assert(service.setLimiter(limiter) == DspServiceError::InvalidValue);
  assert(service.runtime().appliedRevision == service.runtime().revision);
}

void testRoutingAndSubGroup() {
  DemoBackend demo;
  DspService service;
  assert(ok(service.init(demo, {})));
  assert(service.role(2) == OutputRole::Off);
  assert(service.outputEffectiveMute(2));
  assert(service.outputEffectiveSubRouting() == SubRouting::Sum);
  assert(service.setSubTrim(-2) == DspServiceError::NoActiveSubwoofer);
  assert(service.setSubDelay(1) == DspServiceError::NoActiveSubwoofer);
  assert(service.setSubPolarity(180) == DspServiceError::NoActiveSubwoofer);
  assert(service.setSubMute(true) == DspServiceError::NoActiveSubwoofer);
  assert(!service.subValue(SubField::Trim).active);
  assert(!service.subMute().active);

  assert(ok(service.setSubRouting(SubRouting::Stereo)));
  assert(ok(service.setOutputMode(OutputMode::Stereo21)));
  assert(service.outputEffectiveSubRouting() == SubRouting::Sum);
  assert(service.role(2) == OutputRole::Sub && service.role(3) == OutputRole::Off);
  assert(ok(service.setSubTrim(-3)));
  assert(ok(service.setSubDelay(1.05f)));
  assert(ok(service.setSubPolarity(180)));
  assert(ok(service.setSubMute(true)));
  assert(service.global().outputs[2].trimDb == -3);
  assert(service.global().outputs[3].trimDb == 0);
  assert(service.global().outputs[2].delayMs == 1.05f);
  assert(service.global().outputs[3].delayMs == 0);
  assert(service.global().outputs[2].polarity == 180);
  assert(service.global().outputs[3].polarity == 0);
  assert(service.global().outputs[2].userMute);
  assert(!service.global().outputs[3].userMute);

  assert(ok(service.setOutputMode(OutputMode::Stereo22)));
  assert(service.role(2) == OutputRole::SubLeft &&
         service.role(3) == OutputRole::SubRight);
  assert(ok(service.setSubRouting(SubRouting::Sum)));
  assert(service.role(2) == OutputRole::SubMono &&
         service.role(3) == OutputRole::SubMono);
  assert(ok(service.setSubRouting(SubRouting::Stereo)));
  assert(service.global().outputs[2].userMute);
  assert(!service.global().outputs[3].userMute);
  assert(service.subValue(SubField::Trim).mixed);
  assert(service.subMute().mixed);
  assert(ok(service.setSubTrim(-2)));
  assert(ok(service.setSubDelay(2)));
  assert(ok(service.setSubPolarity(0)));
  assert(ok(service.setSubMute(false)));
  assert(!service.subValue(SubField::Trim).mixed);
  assert(service.global().outputs[2].trimDb == -2 &&
         service.global().outputs[3].trimDb == -2);
  assert(service.global().outputs[2].delayMs == 2 &&
         service.global().outputs[3].delayMs == 2);
  assert(service.global().outputs[2].polarity == 0 &&
         service.global().outputs[3].polarity == 0);
  assert(!service.subMute().mixed && !service.subMute().value);
  assert(ok(service.setOutputMute(3, true)));
  assert(service.outputEffectiveMute(3));
  assert(ok(service.setOutputMode(OutputMode::Stereo20)));
  assert(service.outputEffectiveMute(3));
  assert(ok(service.setOutputMode(OutputMode::Stereo22)));
  assert(service.global().outputs[3].userMute && service.outputEffectiveMute(3));
  assert(ok(service.setMasterMute(true)));
  assert(service.outputEffectiveMute(0) && service.outputEffectiveMute(2));
  assert(ok(service.setMasterMute(false)));
  assert(!service.outputEffectiveMute(0));
  assert(service.outputEffectiveMute(3));
}

void testWorkingToneAndPresets() {
  DemoBackend demo;
  DspService service;
  ToneSnapshot tone{};
  assert(ok(service.init(demo, tone)));
  PeqBand band = service.working().peq[0];
  band.gainDb = 1;
  assert(ok(service.setPeqBand(0, band)) && service.dirty(tone));
  assert(service.setPeqBand(kPeqBands, band) == DspServiceError::InvalidIndex);
  band.q = .25f;
  const uint32_t beforeInvalid = service.runtime().revision;
  assert(service.setPeqBand(0, band) == DspServiceError::InvalidValue);
  assert(service.runtime().revision == beforeInvalid);
  Dynamics dynamics = service.working().dynamics;
  dynamics.enabled = true;
  assert(ok(service.setDynamics(dynamics)) && service.dirty(tone));

  assert(service.saveUserPreset(PresetId::Rock, tone) ==
         DspServiceError::ReadOnlyPreset);
  const uint32_t beforeInvalidSave = service.runtime().revision;
  assert(service.saveUserPreset(PresetId::User1, tone, "") ==
         DspServiceError::InvalidName);
  assert(service.runtime().revision == beforeInvalidSave);
  assert(service.global().activePresetId == PresetId::Flat);
  assert(ok(service.saveUserPreset(PresetId::User1, tone)));
  assert(service.global().activePresetId == PresetId::User1);
  assert(!service.dirty(tone));
  assert(service.preset(PresetId::User1).working.dynamics.enabled);
  assert(service.preset(PresetId::User1).working.peq[0].gainDb == 1);
  assert(service.preset(PresetId::User1).toneSnapshot.bass == 0);

  tone.bass = 2;
  assert(ok(service.notifySharedToneChanged(tone)));
  assert(service.dirty(tone));
  const uint32_t afterTone = service.runtime().revision;
  assert(ok(service.notifySharedToneChanged(tone)));
  assert(service.runtime().revision == afterTone);
  assert(service.global().activePresetId == PresetId::User1);
  assert(service.preset(PresetId::User1).toneSnapshot.bass == 0);

  PresetActivation plan{};
  assert(ok(service.prepareRestore(plan)));
  assert(plan.id == PresetId::User1 && plan.toneToApply.bass == 0);
  assert(service.commitActivation(plan, tone) == DspServiceError::StaleActivation);
  assert(service.runtime().revision == afterTone);
  tone = plan.toneToApply;
  assert(ok(service.commitActivation(plan, tone)));
  assert(!service.dirty(tone));
  assert(service.working().peq[0].gainDb == 1);
  assert(service.working().dynamics.enabled);
  assert(service.runtime().revision == afterTone + 1);

  assert(ok(service.prepareActivation(PresetId::Rock, plan)));
  assert(plan.toneToApply.bass == 5 && plan.toneToApply.middle == -2);
  tone = plan.toneToApply;
  assert(ok(service.commitActivation(plan, tone)));
  assert(service.global().activePresetId == PresetId::Rock);
  assert(!service.dirty(tone));
  assert(!service.working().dynamics.enabled);
  assert(service.working().peq[0].gainDb == 0);
  assert(service.prepareActivation(static_cast<PresetId>(99), plan) ==
         DspServiceError::InvalidPreset);
  assert(ok(service.prepareActivation(PresetId::User1, plan)));
  assert(ok(service.setMasterTrim(-1)));
  assert(service.commitActivation(plan, plan.toneToApply) ==
         DspServiceError::StaleActivation);
  assert(ok(service.prepareActivation(PresetId::User1, plan)));
  tone = plan.toneToApply;
  assert(ok(service.commitActivation(plan, tone)));
  assert(!service.dirty(tone));

  assert(service.renameUserPreset(PresetId::Flat, "New") ==
         DspServiceError::ReadOnlyPreset);
  assert(service.renameUserPreset(PresetId::User1, "") ==
         DspServiceError::InvalidName);
  assert(service.renameUserPreset(PresetId::User1, "1234567890123456789012345") ==
         DspServiceError::InvalidName);
  const char invalidUtf8[] = {'B', static_cast<char>(0xc0), static_cast<char>(0xaf), 0};
  assert(service.renameUserPreset(PresetId::User1, invalidUtf8) ==
         DspServiceError::InvalidName);
  assert(ok(service.renameUserPreset(PresetId::User1, "Mój preset")));
  assert(std::strcmp(service.presetInfo(PresetId::User1).name, "Mój preset") == 0);
  assert(!service.dirty(tone));
  assert(ok(service.saveUserPreset(PresetId::User2, tone, "Spokojnie")));
  assert(service.global().activePresetId == PresetId::User2);
  assert(!service.dirty(tone));
  assert(service.preset(PresetId::User2).writable);
  assert(service.preset(PresetId::User2).working.peq[0].gainDb == 1);
  assert(service.preset(PresetId::User2).toneSnapshot.bass == 0);
  assert(!service.runtime().masterMute);
}

void testFailuresAndRuntimeIsolation() {
  CountingBackend backend;
  DspService service;
  assert(service.setMasterTrim(1) == DspServiceError::NotInitialized);
  assert(ok(service.init(backend, {})));
  assert(backend.applies == 1);
  assert(ok(service.setMasterTrim(0)));
  assert(backend.applies == 1);
  backend.failApply = true;
  const uint32_t oldApplied = service.runtime().appliedRevision;
  assert(service.setMasterTrim(1) == DspServiceError::BackendError);
  assert(service.global().masterTrimDb == 1);
  assert(service.runtime().revision == oldApplied + 1);
  assert(service.runtime().appliedRevision == oldApplied);
  assert(service.runtime().backendStatus == BackendStatus::Error);
  assert(std::strcmp(service.runtime().lastError, "backend failed") == 0);
  assert(backend.applies == 2);
  backend.failApply = false;
  assert(ok(service.retryApply()));
  assert(service.runtime().appliedRevision == service.runtime().revision);
  assert(service.runtime().backendStatus == BackendStatus::DemoNoHardware);
  assert(service.runtime().lastError == nullptr);
  assert(backend.applies == 3);

  assert(ok(service.setMasterMute(true)));
  assert(service.runtime().masterMute);
  assert(service.global().masterTrimDb == 1);
  assert(ok(service.saveUserPreset(PresetId::User1, {})));
  assert(service.runtime().masterMute);
  assert(!service.dirty({}));
  PresetActivation plan{};
  assert(ok(service.prepareActivation(PresetId::Flat, plan)));
  assert(ok(service.commitActivation(plan, plan.toneToApply)));
  assert(service.runtime().masterMute);
  assert(ok(service.prepareActivation(PresetId::User1, plan)));
  assert(ok(service.commitActivation(plan, plan.toneToApply)));
  assert(service.runtime().masterMute);
  assert(service.global().masterTrimDb == 1);  // User preset never owns Global.
  assert(ok(service.init(backend, {})));
  assert(!service.runtime().masterMute);
  assert(service.global().activePresetId == PresetId::Flat);
  assert(!service.dirty({}));

  ToneSnapshot audioTone{2, 0, 0};
  uint32_t revision = service.runtime().revision;
  unsigned applies = backend.applies;
  assert(ok(service.notifySharedToneChanged(audioTone)));
  assert(service.runtime().revision == revision + 1);
  assert(backend.applies == applies + 1);

  assert(ok(service.prepareActivation(PresetId::Rock, plan)));
  revision = service.runtime().revision;
  applies = backend.applies;
  assert(ok(service.commitActivation(plan, plan.toneToApply)));
  assert(service.runtime().revision == revision + 1);
  assert(backend.applies == applies + 1);
  // Config's deferred Tone notification arrives after the preset commit.
  assert(ok(service.notifySharedToneChanged(plan.toneToApply)));
  assert(service.runtime().revision == revision + 1);
  assert(backend.applies == applies + 1);

  assert(ok(service.prepareRestore(plan)));
  revision = service.runtime().revision;
  applies = backend.applies;
  assert(ok(service.commitActivation(plan, plan.toneToApply)));
  assert(service.runtime().revision == revision + 1);
  assert(backend.applies == applies + 1);
  assert(ok(service.notifySharedToneChanged(plan.toneToApply)));
  assert(service.runtime().revision == revision + 1);
  assert(backend.applies == applies + 1);
}

void testSaveSnapshots() {
  DemoBackend demo;
  DspService service;
  assert(ok(service.init(demo, {})));
  assert(isValidDspPresetName("A"));
  assert(isValidDspPresetName("123456789012345678901234"));
  assert(!isValidDspPresetName(""));
  assert(!isValidDspPresetName("1234567890123456789012345"));
  const char invalidUtf8[] = {static_cast<char>(0xc0), static_cast<char>(0xaf), 0};
  assert(!isValidDspPresetName(invalidUtf8));

  const PresetId slots[] = {PresetId::User1, PresetId::User2};
  for (size_t i = 0; i < 2; ++i) {
    ToneSnapshot currentTone(static_cast<int8_t>(i + 1),
                             static_cast<int8_t>(-static_cast<int>(i + 1)), 3);
    PeqBand band = service.working().peq[i];
    band.gainDb = static_cast<float>(i + 1);
    assert(ok(service.setPeqBand(i, band)));
    Dynamics dynamics = service.working().dynamics;
    dynamics.enabled = true;
    dynamics.makeupGainDb = static_cast<float>(i + 1);
    assert(ok(service.setDynamics(dynamics)));
    assert(ok(service.notifySharedToneChanged(currentTone)));
    assert(service.saveUserPreset(PresetId::Flat, currentTone) ==
           DspServiceError::ReadOnlyPreset);
    assert(ok(service.saveUserPreset(slots[i], currentTone)));
    assert(service.global().activePresetId == slots[i]);
    assert(!service.dirty(currentTone));
    const DspPreset saved = service.preset(slots[i]);
    assert(saved.toneSnapshot.bass == currentTone.bass);
    assert(saved.toneSnapshot.middle == currentTone.middle);
    assert(saved.toneSnapshot.treble == currentTone.treble);
    assert(saved.working.peq[i].gainDb == band.gainDb);
    assert(saved.working.dynamics.enabled == dynamics.enabled);
    assert(saved.working.dynamics.makeupGainDb == dynamics.makeupGainDb);
  }
}

}  // namespace

int main() {
  testInitAndGlobal();
  testRoutingAndSubGroup();
  testWorkingToneAndPresets();
  testFailuresAndRuntimeIsolation();
  testSaveSnapshots();
}
