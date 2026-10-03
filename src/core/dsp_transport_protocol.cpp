#include "dsp_transport_protocol.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace voxone {
namespace dsp {
namespace {

bool unsignedValue(const char* text, uint32_t& out) {
  if (!text || !*text) return false;
  for (const char* p = text; *p; ++p)
    if (*p < '0' || *p > '9') return false;
  errno = 0;
  char* end = nullptr;
  const unsigned long value = std::strtoul(text, &end, 10);
  if (errno == ERANGE || *end ||
      value > std::numeric_limits<uint32_t>::max()) return false;
  out = static_cast<uint32_t>(value);
  return true;
}

bool floatValue(const char* text, float& out) {
  if (!text || !*text) return false;
  errno = 0;
  char* end = nullptr;
  const float value = std::strtof(text, &end);
  if (errno == ERANGE || *end || !std::isfinite(value)) return false;
  out = value;
  return true;
}

char* nextField(char*& cursor) {
  if (!cursor) return nullptr;
  char* current = cursor;
  char* comma = std::strchr(cursor, ',');
  if (comma) { *comma = '\0'; cursor = comma + 1; }
  else cursor = nullptr;
  return current;
}

bool booleanValue(float value) { return value == 0 || value == 1; }
bool whole(float value, float low, float high) {
  return std::isfinite(value) && value >= low && value <= high &&
         std::floor(value) == value;
}

}  // namespace

DspParseError parseDspWsFrame(const char* frame, size_t length, DspCommand& out) {
  out = {};
  if (!frame || length < 4 || std::memcmp(frame, "dsp.", 4) != 0)
    return DspParseError::NotDsp;
  if (length > kDspWsFrameMaxBytes) return DspParseError::TooLarge;
  if (std::memchr(frame, '\0', length)) return DspParseError::Malformed;
  char copy[kDspWsFrameMaxBytes + 1];
  std::memcpy(copy, frame, length);
  copy[length] = '\0';
  char* equals = std::strchr(copy, '=');
  if (!equals) return DspParseError::Malformed;
  *equals++ = '\0';
  char* cursor = equals;
  uint32_t value = 0;
  if (!unsignedValue(nextField(cursor), out.requestId))
    return DspParseError::Malformed;
  if (std::strcmp(copy, "dsp.set") == 0) {
    if (!unsignedValue(nextField(cursor), value) ||
        value < static_cast<uint8_t>(DspParameter::MasterTrim) ||
        value > static_cast<uint8_t>(DspParameter::SubMute))
      return DspParseError::InvalidParameter;
    out.operation = DspOperation::Set;
    out.parameter = static_cast<DspParameter>(value);
    if (!unsignedValue(nextField(cursor), value) || value > UINT8_MAX)
      return DspParseError::Malformed;
    out.index = static_cast<uint8_t>(value);
    if (!floatValue(nextField(cursor), out.value) || cursor)
      return DspParseError::Malformed;
    return DspParseError::Ok;
  }
  if (std::strcmp(copy, "dsp.restore") == 0) {
    if (cursor) return DspParseError::Malformed;
    out.operation = DspOperation::Restore;
    return DspParseError::Ok;
  }
  if (std::strcmp(copy, "dsp.activate") == 0 ||
      std::strcmp(copy, "dsp.save") == 0 ||
      std::strcmp(copy, "dsp.rename") == 0) {
    if (!unsignedValue(nextField(cursor), value) || value > UINT8_MAX)
      return DspParseError::Malformed;
    out.presetId = static_cast<PresetId>(value);
    if (std::strcmp(copy, "dsp.activate") == 0) {
      if (cursor) return DspParseError::Malformed;
      out.operation = DspOperation::Activate;
      return DspParseError::Ok;
    }
    out.operation = std::strcmp(copy, "dsp.save") == 0
                        ? DspOperation::Save : DspOperation::Rename;
    if (cursor) {
      if (!isValidDspPresetName(cursor)) return DspParseError::InvalidName;
      std::memcpy(out.name, cursor, std::strlen(cursor) + 1);
    } else if (out.operation == DspOperation::Rename) {
      return DspParseError::Malformed;
    }
    return DspParseError::Ok;
  }
  return DspParseError::Malformed;
}

DspServiceError applyDspSet(DspService& service, const DspCommand& command) {
  if (command.operation != DspOperation::Set)
    return DspServiceError::InvalidValue;
  const float value = command.value;
  if (!std::isfinite(value)) return DspServiceError::InvalidValue;
  const size_t index = command.index;
  switch (command.parameter) {
    case DspParameter::OutputTrim:
      return service.setOutputTrim(index, value);
    case DspParameter::OutputDelay:
      return service.setOutputDelay(index, value);
    case DspParameter::OutputPolarity:
      return whole(value, 0, 180)
          ? service.setOutputPolarity(index, static_cast<uint16_t>(value))
          : DspServiceError::InvalidValue;
    case DspParameter::OutputUserMute:
      return booleanValue(value) ? service.setOutputMute(index, value != 0)
                                 : DspServiceError::InvalidValue;
    case DspParameter::PeqEnabled:
    case DspParameter::PeqFrequency:
    case DspParameter::PeqGain:
    case DspParameter::PeqQ: {
      if (index >= kPeqBands) return DspServiceError::InvalidIndex;
      PeqBand band = service.working().peq[index];
      if (command.parameter == DspParameter::PeqEnabled) {
        if (!booleanValue(value)) return DspServiceError::InvalidValue;
        band.enabled = value != 0;
      } else if (command.parameter == DspParameter::PeqFrequency) {
        if (!whole(value, 0, UINT16_MAX)) return DspServiceError::InvalidValue;
        band.frequencyHz = static_cast<uint16_t>(value);
      } else if (command.parameter == DspParameter::PeqGain) band.gainDb = value;
      else band.q = value;
      return service.setPeqBand(index, band);
    }
    default: break;
  }
  if (index != 0) return DspServiceError::InvalidIndex;
  switch (command.parameter) {
    case DspParameter::MasterTrim: return service.setMasterTrim(value);
    case DspParameter::ProcessingEnabled:
      return booleanValue(value) ? service.setProcessingEnabled(value != 0)
                                 : DspServiceError::InvalidValue;
    case DspParameter::OutputMode:
      return whole(value, 0, 2)
          ? service.setOutputMode(static_cast<OutputMode>(static_cast<uint8_t>(value)))
          : DspServiceError::InvalidValue;
    case DspParameter::Crossover:
      return whole(value, 0, UINT16_MAX)
          ? service.setCrossover(static_cast<uint16_t>(value))
          : DspServiceError::InvalidValue;
    case DspParameter::HpfMains:
      return booleanValue(value) ? service.setHpfMains(value != 0)
                                 : DspServiceError::InvalidValue;
    case DspParameter::SubRouting:
      return whole(value, 0, 1)
          ? service.setSubRouting(static_cast<SubRouting>(static_cast<uint8_t>(value)))
          : DspServiceError::InvalidValue;
    case DspParameter::LoudnessEnabled:
    case DspParameter::LoudnessIntensity: {
      DspLoudness loudness = service.global().loudness;
      if (command.parameter == DspParameter::LoudnessEnabled) {
        if (!booleanValue(value)) return DspServiceError::InvalidValue;
        loudness.enabled = value != 0;
      } else {
        if (!whole(value, 0, UINT8_MAX)) return DspServiceError::InvalidValue;
        loudness.intensity = static_cast<uint8_t>(value);
      }
      return service.setLoudness(loudness);
    }
    case DspParameter::LimiterEnabled:
    case DspParameter::LimiterThreshold:
    case DspParameter::LimiterAttack:
    case DspParameter::LimiterRelease: {
      DspLimiter limiter = service.global().limiter;
      switch (command.parameter) {
        case DspParameter::LimiterEnabled:
          if (!booleanValue(value)) return DspServiceError::InvalidValue;
          limiter.enabled = value != 0; break;
        case DspParameter::LimiterThreshold: limiter.thresholdDbfs = value; break;
        case DspParameter::LimiterAttack: limiter.attackMs = value; break;
        default: limiter.releaseMs = value; break;
      }
      return service.setLimiter(limiter);
    }
    case DspParameter::MasterMute:
      return booleanValue(value) ? service.setMasterMute(value != 0)
                                 : DspServiceError::InvalidValue;
    case DspParameter::DynamicsEnabled:
    case DspParameter::DynamicsThreshold:
    case DspParameter::DynamicsRatio:
    case DspParameter::DynamicsAttack:
    case DspParameter::DynamicsRelease:
    case DspParameter::DynamicsMakeup: {
      Dynamics dynamics = service.working().dynamics;
      switch (command.parameter) {
        case DspParameter::DynamicsEnabled:
          if (!booleanValue(value)) return DspServiceError::InvalidValue;
          dynamics.enabled = value != 0; break;
        case DspParameter::DynamicsThreshold: dynamics.thresholdDbfs = value; break;
        case DspParameter::DynamicsRatio: dynamics.ratio = value; break;
        case DspParameter::DynamicsAttack: dynamics.attackMs = value; break;
        case DspParameter::DynamicsRelease: dynamics.releaseMs = value; break;
        default: dynamics.makeupGainDb = value; break;
      }
      return service.setDynamics(dynamics);
    }
    case DspParameter::SubTrim: return service.setSubTrim(value);
    case DspParameter::SubDelay: return service.setSubDelay(value);
    case DspParameter::SubPolarity:
      return whole(value, 0, 180)
          ? service.setSubPolarity(static_cast<uint16_t>(value))
          : DspServiceError::InvalidValue;
    case DspParameter::SubMute:
      return booleanValue(value) ? service.setSubMute(value != 0)
                                 : DspServiceError::InvalidValue;
    default: return DspServiceError::InvalidValue;
  }
}

}  // namespace dsp
}  // namespace voxone
