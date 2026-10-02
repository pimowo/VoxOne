#ifndef VOXONE_DSP_MODEL_H
#define VOXONE_DSP_MODEL_H

#include <cstddef>
#include <cstdint>

namespace voxone {
namespace dsp {

constexpr size_t kPeqBands = 5;
constexpr size_t kOutputCount = 4;
constexpr size_t kPresetCount = 8;

enum class OutputMode : uint8_t { Stereo20, Stereo21, Stereo22 };
enum class SubRouting : uint8_t { Sum, Stereo };
enum class OutputRole : uint8_t { Left, Right, Sub, SubMono, SubLeft, SubRight, Off };
enum class PresetId : uint8_t { Flat, Bass, Rock, Pop, Mowa, Noc, User1, User2 };
enum class BackendStatus : uint8_t { Uninitialized, DemoNoHardware, Error };
enum class SubField : uint8_t { Trim, Delay, Polarity };

struct ToneSnapshot {
  int8_t bass = 0;
  int8_t middle = 0;
  int8_t treble = 0;
  ToneSnapshot() = default;
  ToneSnapshot(int8_t bassValue, int8_t middleValue, int8_t trebleValue)
      : bass(bassValue), middle(middleValue), treble(trebleValue) {}
};

struct PeqBand {
  bool enabled = true;
  uint16_t frequencyHz = 1000;
  float gainDb = 0;
  float q = 1;
};

struct Dynamics {
  bool enabled = false;
  float thresholdDbfs = -18;
  float ratio = 2;
  float attackMs = 10;
  float releaseMs = 200;
  float makeupGainDb = 0;
};

struct DspWorkingState {
  PeqBand peq[kPeqBands]{};
  Dynamics dynamics{};
};

struct DspOutput {
  float trimDb = 0;
  float delayMs = 0;
  uint16_t polarity = 0;
  bool userMute = false;
};

struct DspLoudness {
  bool enabled = false;
  uint8_t intensity = 0;
};

struct DspLimiter {
  bool enabled = true;
  float thresholdDbfs = -3;
  float attackMs = 5;
  float releaseMs = 200;
};

struct DspGlobal {
  float masterTrimDb = 0;
  bool processingEnabled = true;
  OutputMode outputMode = OutputMode::Stereo20;
  uint16_t crossoverHz = 80;
  bool hpfMains = true;
  SubRouting subRouting = SubRouting::Sum;
  DspOutput outputs[kOutputCount]{};
  DspLoudness loudness{};
  DspLimiter limiter{};
  PresetId activePresetId = PresetId::Flat;
};

struct DspPreset {
  PresetId id = PresetId::Flat;
  const char* name = "Flat";
  bool writable = false;
  ToneSnapshot toneSnapshot{};
  DspWorkingState working{};
};

struct DspRuntimeState {
  bool masterMute = false;
  BackendStatus backendStatus = BackendStatus::Uninitialized;
  const char* lastError = nullptr;
  uint32_t revision = 0;
  uint32_t appliedRevision = 0;
};

// The bit positions correspond to OutputMode values.
struct DspCapabilities {
  bool demoMode = true;
  bool hasDspHardware = false;
  uint8_t peqBands = kPeqBands;
  uint8_t outputCount = kOutputCount;
  uint8_t supportedOutputModes = 0x07;
  float maxDelayMs = 5;
  uint32_t sampleRateHz = 48000;
};

// True means processingEnabled requests bypass of that stage, independent of its own enable flag.
struct DspBypassPolicy {
  bool tone;
  bool peq;
  bool loudness;
  bool dynamics;
  bool crossover;
  bool routing;
  bool delay;
  bool trim;
  bool limiter;
  bool mute;
};

DspCapabilities demoCapabilities();
DspGlobal defaultGlobal();
DspWorkingState defaultWorking();
DspBypassPolicy bypassFor(const DspGlobal& global);
bool validGlobal(const DspGlobal& global);
bool validWorking(const DspWorkingState& working);
bool validPeqBand(const PeqBand& band);
bool validDynamics(const Dynamics& dynamics);
bool validTone(const ToneSnapshot& tone);
bool validPresetId(PresetId id);
bool presetWritable(PresetId id);
DspPreset factoryPreset(PresetId id);
OutputRole outputRole(OutputMode mode, SubRouting routing, size_t index);
SubRouting effectiveSubRouting(OutputMode mode, SubRouting preferred);
bool effectiveMute(const DspGlobal& global, bool masterMute, size_t index);

struct SubGroupValue {
  bool active = false;
  bool mixed = false;
  float value = 0;
  SubGroupValue() = default;
  SubGroupValue(bool isActive, bool isMixed, float current)
      : active(isActive), mixed(isMixed), value(current) {}
};

SubGroupValue readSubGroup(const DspGlobal& global, SubField field);
bool writeSubGroup(DspGlobal& global, SubField field, float value);

class IDspBackend {
 public:
  virtual ~IDspBackend() = default;
  virtual bool init() = 0;
  virtual bool apply(const DspGlobal& global, const DspWorkingState& working,
                     const DspRuntimeState& runtime) = 0;
  virtual BackendStatus status() const = 0;
  virtual const char* lastError() const = 0;
};

class DemoBackend final : public IDspBackend {
 public:
  bool init() override;
  bool apply(const DspGlobal& global, const DspWorkingState& working,
             const DspRuntimeState& runtime) override;
  BackendStatus status() const override { return status_; }
  const char* lastError() const override { return nullptr; }

 private:
  BackendStatus status_ = BackendStatus::Uninitialized;
};

// Pure RAM model. Current Tone is supplied by the caller; Config remains its owner.
class DspModel {
 public:
  explicit DspModel(IDspBackend& backend);
  bool init();
  const DspGlobal& global() const { return global_; }
  const DspWorkingState& working() const { return working_; }
  const DspRuntimeState& runtime() const { return runtime_; }
  DspPreset preset(PresetId id) const;
  bool isDirty(const ToneSnapshot& currentTone) const;
  bool setGlobal(const DspGlobal& candidate);
  bool setWorking(const DspWorkingState& candidate);
  bool setOutput(size_t index, const DspOutput& output);
  bool setPeqBand(size_t index, const PeqBand& band);
  bool setMasterMute(bool mute);
  bool activatePreset(PresetId id, ToneSnapshot& requestedTone);
  bool saveUserPreset(PresetId id, const ToneSnapshot& currentTone);

 private:
  void changed();
  IDspBackend& backend_;
  DspGlobal global_{};
  DspWorkingState working_{};
  DspPreset userPresets_[2]{};
  DspRuntimeState runtime_{};
};

}  // namespace dsp
}  // namespace voxone

#endif
