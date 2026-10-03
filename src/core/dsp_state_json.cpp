#include "dsp_state_json.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace voxone {
namespace dsp {
namespace {

class JsonWriter {
 public:
  JsonWriter(char* buffer, size_t capacity) : buffer_(buffer), capacity_(capacity) {
    if (!buffer_ || !capacity_) failed_ = true;
    else buffer_[0] = '\0';
  }
  void literal(const char* value) {
    if (failed_) return;
    const size_t length = std::strlen(value);
    if (length >= capacity_ - used_) { failed_ = true; return; }
    std::memcpy(buffer_ + used_, value, length + 1);
    used_ += length;
  }
  void format(const char* pattern, ...) {
    if (failed_) return;
    va_list args;
    va_start(args, pattern);
    const int written = std::vsnprintf(buffer_ + used_, capacity_ - used_, pattern, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= capacity_ - used_) {
      failed_ = true;
      return;
    }
    used_ += static_cast<size_t>(written);
  }
  void quoted(const char* value) {
    if (!value) { literal("null"); return; }
    literal("\"");
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value);
         *p && !failed_; ++p) {
      if (*p == '"' || *p == '\\') format("\\%c", *p);
      else if (*p < 0x20 || *p == 0x7f) format("\\u%04x", *p);
      else format("%c", *p);
    }
    literal("\"");
  }
  size_t length() const { return failed_ ? 0 : used_; }

 private:
  char* buffer_;
  size_t capacity_;
  size_t used_ = 0;
  bool failed_ = false;
};

const char* boolean(bool value) { return value ? "true" : "false"; }

void subField(JsonWriter& out, const char* key, const SubGroupValue& value,
              bool comma) {
  out.format("%s\"%s\":{\"active\":%s,\"mixed\":%s,\"value\":%.3f}",
             comma ? "," : "", key, boolean(value.active), boolean(value.mixed),
             static_cast<double>(value.value));
}

}  // namespace

size_t formatDspStateJson(char* buffer, size_t capacity,
                          const DspService& service,
                          const DspSharedAudioView& shared) {
  JsonWriter out(buffer, capacity);
  const DspCapabilities caps = demoCapabilities();
  const DspGlobal& global = service.global();
  const DspWorkingState& working = service.working();
  const DspRuntimeState& runtime = service.runtime();
  out.format("{\"capabilities\":{\"demoMode\":%s,\"hasDspHardware\":%s,"
             "\"peqBands\":%u,\"outputCount\":%u,\"supportedOutputModes\":%u,"
             "\"maxDelayMs\":%.3f,\"sampleRateHz\":%lu},",
             boolean(caps.demoMode), boolean(caps.hasDspHardware),
             static_cast<unsigned>(caps.peqBands), static_cast<unsigned>(caps.outputCount),
             static_cast<unsigned>(caps.supportedOutputModes),
             static_cast<double>(caps.maxDelayMs),
             static_cast<unsigned long>(caps.sampleRateHz));
  out.format("\"sharedAudio\":{\"volume\":%u,\"maximumVolume\":%u,"
             "\"startupMode\":%u,\"startupFixedVolume\":%u,"
             "\"bass\":%d,\"middle\":%d,\"treble\":%d,\"balance\":%d},",
             static_cast<unsigned>(shared.volume),
             static_cast<unsigned>(shared.maximumVolume),
             static_cast<unsigned>(shared.startupMode),
             static_cast<unsigned>(shared.startupFixedVolume),
             static_cast<int>(shared.tone.bass), static_cast<int>(shared.tone.middle),
             static_cast<int>(shared.tone.treble), static_cast<int>(shared.balance));
  out.format("\"global\":{\"masterTrimDb\":%.3f,\"processingEnabled\":%s,"
             "\"outputMode\":%u,\"crossoverHz\":%u,\"hpfMains\":%s,"
             "\"subRouting\":%u,\"activePresetId\":%u,\"outputs\":[",
             static_cast<double>(global.masterTrimDb), boolean(global.processingEnabled),
             static_cast<unsigned>(global.outputMode),
             static_cast<unsigned>(global.crossoverHz), boolean(global.hpfMains),
             static_cast<unsigned>(global.subRouting),
             static_cast<unsigned>(global.activePresetId));
  for (size_t i = 0; i < kOutputCount; ++i) {
    const DspOutput& output = global.outputs[i];
    out.format("%s{\"trimDb\":%.3f,\"delayMs\":%.3f,\"polarity\":%u,"
               "\"userMute\":%s}", i ? "," : "",
               static_cast<double>(output.trimDb),
               static_cast<double>(output.delayMs),
               static_cast<unsigned>(output.polarity), boolean(output.userMute));
  }
  out.format("],\"loudness\":{\"enabled\":%s,\"intensity\":%u},"
             "\"limiter\":{\"enabled\":%s,\"thresholdDbfs\":%.3f,"
             "\"attackMs\":%.3f,\"releaseMs\":%.3f}},",
             boolean(global.loudness.enabled),
             static_cast<unsigned>(global.loudness.intensity),
             boolean(global.limiter.enabled),
             static_cast<double>(global.limiter.thresholdDbfs),
             static_cast<double>(global.limiter.attackMs),
             static_cast<double>(global.limiter.releaseMs));
  out.literal("\"working\":{\"peq\":[");
  for (size_t i = 0; i < kPeqBands; ++i) {
    const PeqBand& band = working.peq[i];
    out.format("%s{\"enabled\":%s,\"frequencyHz\":%u,\"gainDb\":%.3f,"
               "\"q\":%.3f}", i ? "," : "", boolean(band.enabled),
               static_cast<unsigned>(band.frequencyHz),
               static_cast<double>(band.gainDb), static_cast<double>(band.q));
  }
  const Dynamics& dynamics = working.dynamics;
  out.format("],\"dynamics\":{\"enabled\":%s,\"thresholdDbfs\":%.3f,"
             "\"ratio\":%.3f,\"attackMs\":%.3f,\"releaseMs\":%.3f,"
             "\"makeupGainDb\":%.3f}},\"presets\":[",
             boolean(dynamics.enabled), static_cast<double>(dynamics.thresholdDbfs),
             static_cast<double>(dynamics.ratio),
             static_cast<double>(dynamics.attackMs),
             static_cast<double>(dynamics.releaseMs),
             static_cast<double>(dynamics.makeupGainDb));
  for (size_t i = 0; i < kPresetCount; ++i) {
    const PresetInfo info = service.presetInfo(static_cast<PresetId>(i));
    out.format("%s{\"id\":%u,\"name\":", i ? "," : "",
               static_cast<unsigned>(info.id));
    out.quoted(info.name);
    out.format(",\"writable\":%s,\"initialized\":%s}",
               boolean(info.writable), boolean(info.initialized));
  }
  out.format("],\"runtime\":{\"masterMute\":%s,\"backendStatus\":%u,"
             "\"lastError\":", boolean(runtime.masterMute),
             static_cast<unsigned>(runtime.backendStatus));
  out.quoted(runtime.lastError);
  out.format(",\"revision\":%lu,\"appliedRevision\":%lu,\"dirty\":%s},",
             static_cast<unsigned long>(runtime.revision),
             static_cast<unsigned long>(runtime.appliedRevision),
             boolean(service.dirty(shared.tone)));
  out.format("\"computed\":{\"effectiveSubRouting\":%u,\"outputs\":[",
             static_cast<unsigned>(service.outputEffectiveSubRouting()));
  for (size_t i = 0; i < kOutputCount; ++i)
    out.format("%s{\"role\":%u,\"effectiveMute\":%s}", i ? "," : "",
               static_cast<unsigned>(service.role(i)),
               boolean(service.outputEffectiveMute(i)));
  out.literal("],\"sub\":{");
  subField(out, "trim", service.subValue(SubField::Trim), false);
  subField(out, "delay", service.subValue(SubField::Delay), true);
  subField(out, "polarity", service.subValue(SubField::Polarity), true);
  const SubMuteValue mute = service.subMute();
  out.format(",\"mute\":{\"active\":%s,\"mixed\":%s,\"value\":%s}}}}",
             boolean(mute.active), boolean(mute.mixed), boolean(mute.value));
  return out.length();
}

}  // namespace dsp
}  // namespace voxone
