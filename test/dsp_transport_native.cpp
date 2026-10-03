#include "../src/core/dsp_transport_protocol.h"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace voxone::dsp;

namespace {

DspParseError parse(const char* text, DspCommand& out) {
  return parseDspWsFrame(text, std::strlen(text), out);
}

void checkSet(DspService& service, DspParameter parameter, uint8_t index,
              float value, DspServiceError expected) {
  DspCommand command{};
  command.operation = DspOperation::Set;
  command.parameter = parameter;
  command.index = index;
  command.value = value;
  assert(applyDspSet(service, command) == expected);
}

}  // namespace

int main() {
  DspCommand command{};
  assert(parse("bass=2", command) == DspParseError::NotDsp);
  assert(parse("dsp.set=42,8,2,1.05", command) == DspParseError::Ok);
  assert(command.requestId == 42 && command.operation == DspOperation::Set);
  assert(command.parameter == DspParameter::OutputDelay &&
         command.index == 2 && command.value == 1.05f);
  assert(parse("dsp.set=4294967295,1,0,-2.5", command) == DspParseError::Ok);
  assert(command.requestId == UINT32_MAX);
  assert(parse("dsp.set=4294967296,1,0,1", command) == DspParseError::Malformed);
  assert(parse("dsp.set=12,0,0,1", command) == DspParseError::InvalidParameter);
  assert(parse("dsp.set=12,32,0,1", command) == DspParseError::InvalidParameter);
  assert(parse("dsp.set=12,1,256,1", command) == DspParseError::Malformed);
  assert(parse("dsp.set=12,1,0,nan", command) == DspParseError::Malformed);
  assert(parse("dsp.set=12,1,0,1,extra", command) == DspParseError::Malformed);
  assert(parse("dsp.set=12,1,0", command) == DspParseError::Malformed);
  assert(parse("dsp.unknown=12", command) == DspParseError::Malformed);
  char tooLarge[kDspWsFrameMaxBytes + 2];
  std::memset(tooLarge, 'A', sizeof(tooLarge));
  std::memcpy(tooLarge, "dsp.", 4);
  assert(parseDspWsFrame(tooLarge, sizeof(tooLarge), command) ==
         DspParseError::TooLarge);
  const char embeddedNul[] = {'d','s','p','.','r','e','s','t','o','r','e','=','1',0,'x'};
  assert(parseDspWsFrame(embeddedNul, sizeof(embeddedNul), command) ==
         DspParseError::Malformed);

  assert(parse("dsp.activate=7,2", command) == DspParseError::Ok);
  assert(command.operation == DspOperation::Activate && command.presetId == PresetId::Rock);
  assert(parse("dsp.restore=8", command) == DspParseError::Ok);
  assert(command.operation == DspOperation::Restore && command.requestId == 8);
  assert(parse("dsp.save=9,6", command) == DspParseError::Ok);
  assert(command.operation == DspOperation::Save && command.presetId == PresetId::User1);
  assert(command.name[0] == '\0');
  assert(parse("dsp.save=10,7,Mój preset", command) == DspParseError::Ok);
  assert(command.operation == DspOperation::Save && command.presetId == PresetId::User2);
  assert(std::strcmp(command.name, "Mój preset") == 0);
  assert(parse("dsp.rename=11,6,A,B", command) == DspParseError::Ok);
  assert(std::strcmp(command.name, "A,B") == 0);
  assert(parse("dsp.rename=11,6", command) == DspParseError::Malformed);
  assert(parse("dsp.rename=11,6,1234567890123456789012345", command) ==
         DspParseError::InvalidName);
  const char badName[] = {'d','s','p','.','r','e','n','a','m','e','=','1',',','6',',',
                          static_cast<char>(0xc0),static_cast<char>(0xaf),0};
  assert(parse(badName, command) == DspParseError::InvalidName);

  DemoBackend backend;
  DspService service;
  assert(service.init(backend, {}) == DspServiceError::Ok);
  checkSet(service, DspParameter::MasterTrim, 0, -2.5f, DspServiceError::Ok);
  assert(service.global().masterTrimDb == -2.5f);
  checkSet(service, DspParameter::MasterTrim, 1, 0, DspServiceError::InvalidIndex);
  checkSet(service, DspParameter::MasterTrim, 0, .25f, DspServiceError::InvalidValue);
  checkSet(service, DspParameter::OutputDelay, 4, 1, DspServiceError::InvalidIndex);
  checkSet(service, DspParameter::OutputDelay, 2, 1.05f, DspServiceError::Ok);
  assert(service.global().outputs[2].delayMs == 1.05f);
  checkSet(service, DspParameter::OutputMode, 0, 1, DspServiceError::Ok);
  checkSet(service, DspParameter::OutputMode, 0, 1.5f, DspServiceError::InvalidValue);
  checkSet(service, DspParameter::PeqFrequency, 5, 1000, DspServiceError::InvalidIndex);
  checkSet(service, DspParameter::PeqGain, 0, 1.5f, DspServiceError::Ok);
  assert(service.working().peq[0].gainDb == 1.5f);
  checkSet(service, DspParameter::DynamicsRatio, 0, 3.5f, DspServiceError::Ok);
  assert(service.working().dynamics.ratio == 3.5f);
  checkSet(service, DspParameter::SubTrim, 0, -3, DspServiceError::Ok);
  assert(service.global().outputs[2].trimDb == -3);
  checkSet(service, DspParameter::MasterMute, 0, 1, DspServiceError::Ok);
  assert(service.runtime().masterMute);
  checkSet(service, DspParameter::MasterMute, 0, 2, DspServiceError::InvalidValue);

  struct Case { DspParameter parameter; uint8_t index; float value; };
  const Case allParameters[] = {
      {DspParameter::MasterTrim, 0, -1},
      {DspParameter::ProcessingEnabled, 0, 0},
      {DspParameter::OutputMode, 0, 2},
      {DspParameter::Crossover, 0, 120},
      {DspParameter::HpfMains, 0, 0},
      {DspParameter::SubRouting, 0, 1},
      {DspParameter::OutputTrim, 2, -1},
      {DspParameter::OutputDelay, 2, 1},
      {DspParameter::OutputPolarity, 2, 180},
      {DspParameter::OutputUserMute, 2, 1},
      {DspParameter::LoudnessEnabled, 0, 1},
      {DspParameter::LoudnessIntensity, 0, 50},
      {DspParameter::LimiterEnabled, 0, 0},
      {DspParameter::LimiterThreshold, 0, -6},
      {DspParameter::LimiterAttack, 0, 6},
      {DspParameter::LimiterRelease, 0, 210},
      {DspParameter::MasterMute, 0, 0},
      {DspParameter::PeqEnabled, 0, 0},
      {DspParameter::PeqFrequency, 0, 90},
      {DspParameter::PeqGain, 0, 1},
      {DspParameter::PeqQ, 0, 1.2f},
      {DspParameter::DynamicsEnabled, 0, 1},
      {DspParameter::DynamicsThreshold, 0, -20},
      {DspParameter::DynamicsRatio, 0, 3},
      {DspParameter::DynamicsAttack, 0, 20},
      {DspParameter::DynamicsRelease, 0, 300},
      {DspParameter::DynamicsMakeup, 0, 1},
      {DspParameter::SubTrim, 0, -2},
      {DspParameter::SubDelay, 0, 1},
      {DspParameter::SubPolarity, 0, 180},
      {DspParameter::SubMute, 0, 1},
  };
  for (const Case& entry : allParameters)
    checkSet(service, entry.parameter, entry.index, entry.value, DspServiceError::Ok);
  std::printf("dsp_command_bytes=%zu\n", sizeof(DspCommand));
}
