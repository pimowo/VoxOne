#include "dsp_storage_format.h"

#include <cmath>
#include <cstring>

namespace voxone {
namespace dsp {
namespace storage_format {
namespace {

constexpr uint8_t kMagic[4] = {'V', 'D', 'S', 'P'};
constexpr uint8_t kLiveType = 1;
constexpr uint8_t kUserType = 2;
constexpr uint8_t kPendingType = 3;

class Writer {
 public:
  Writer(uint8_t* buffer, size_t capacity) : buffer_(buffer), capacity_(capacity) {}
  bool u8(uint8_t value) {
    if (position_ >= capacity_) return false;
    buffer_[position_++] = value;
    return true;
  }
  bool i8(int8_t value) { return u8(static_cast<uint8_t>(value)); }
  bool boolean(bool value) { return u8(value ? 1 : 0); }
  bool u16(uint16_t value) {
    return u8(static_cast<uint8_t>(value)) && u8(static_cast<uint8_t>(value >> 8));
  }
  bool u32(uint32_t value) {
    return u16(static_cast<uint16_t>(value)) && u16(static_cast<uint16_t>(value >> 16));
  }
  bool bytes(const uint8_t* source, size_t count) {
    if (count > capacity_ - position_) return false;
    std::memcpy(buffer_ + position_, source, count);
    position_ += count;
    return true;
  }
  size_t position() const { return position_; }

 private:
  uint8_t* buffer_;
  size_t capacity_;
  size_t position_ = 0;
};

class Reader {
 public:
  Reader(const uint8_t* buffer, size_t length) : buffer_(buffer), length_(length) {}
  bool u8(uint8_t& value) {
    if (position_ >= length_) return false;
    value = buffer_[position_++];
    return true;
  }
  bool i8(int8_t& value) {
    uint8_t encoded = 0;
    if (!u8(encoded)) return false;
    value = static_cast<int8_t>(encoded < 128 ? encoded :
                                static_cast<int16_t>(encoded) - 256);
    return true;
  }
  bool boolean(bool& value) {
    uint8_t encoded = 0;
    if (!u8(encoded) || encoded > 1) return false;
    value = encoded != 0;
    return true;
  }
  bool u16(uint16_t& value) {
    uint8_t low = 0, high = 0;
    if (!u8(low) || !u8(high)) return false;
    value = static_cast<uint16_t>(low | static_cast<uint16_t>(high) << 8);
    return true;
  }
  const uint8_t* take(size_t count) {
    if (count > length_ - position_) return nullptr;
    const uint8_t* result = buffer_ + position_;
    position_ += count;
    return result;
  }
  bool finished() const { return position_ == length_; }

 private:
  const uint8_t* buffer_;
  size_t length_;
  size_t position_ = 0;
};

uint32_t crc32(const uint8_t* bytes, size_t length) {
  uint32_t crc = 0xffffffffUL;
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320UL : 0UL);
  }
  return crc ^ 0xffffffffUL;
}

uint32_t littleU32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         static_cast<uint32_t>(bytes[1]) << 8 |
         static_cast<uint32_t>(bytes[2]) << 16 |
         static_cast<uint32_t>(bytes[3]) << 24;
}

bool header(Writer& writer, uint8_t type, size_t payloadLength) {
  return writer.bytes(kMagic, sizeof(kMagic)) && writer.u8(type) &&
         writer.u8(kSchemaVersion) && writer.u16(static_cast<uint16_t>(payloadLength));
}

bool finishRecord(Writer& writer, uint8_t* buffer, size_t expected, size_t& written) {
  if (writer.position() != expected - kCrcBytes ||
      !writer.u32(crc32(buffer, writer.position())) || writer.position() != expected) return false;
  written = expected;
  return true;
}

bool checkHeader(const uint8_t* buffer, size_t length, uint8_t type,
                 size_t minPayload, size_t maxPayload, size_t& payloadLength) {
  if (!buffer || length < kHeaderBytes + kCrcBytes ||
      length > kHeaderBytes + maxPayload + kCrcBytes ||
      std::memcmp(buffer, kMagic, sizeof(kMagic)) != 0 ||
      buffer[4] != type || buffer[5] != kSchemaVersion) return false;
  payloadLength = static_cast<size_t>(buffer[6]) |
                  static_cast<size_t>(buffer[7]) << 8;
  if (payloadLength < minPayload || payloadLength > maxPayload ||
      length != kHeaderBytes + payloadLength + kCrcBytes) return false;
  return littleU32(buffer + length - kCrcBytes) == crc32(buffer, length - kCrcBytes);
}

int8_t scaled(float value, float factor) {
  return static_cast<int8_t>(std::lround(value * factor));
}

uint8_t unsignedScaled(float value, float factor) {
  return static_cast<uint8_t>(std::lround(value * factor));
}

bool writeGlobal(Writer& writer, const DspGlobal& global) {
  if (!writer.i8(scaled(global.masterTrimDb, 2)) ||
      !writer.boolean(global.processingEnabled) ||
      !writer.u8(static_cast<uint8_t>(global.outputMode)) ||
      !writer.u8(static_cast<uint8_t>(global.crossoverHz)) ||
      !writer.boolean(global.hpfMains) ||
      !writer.u8(static_cast<uint8_t>(global.subRouting)) ||
      !writer.u8(static_cast<uint8_t>(kOutputCount))) return false;
  for (const DspOutput& output : global.outputs) {
    if (!writer.i8(scaled(output.trimDb, 2)) ||
        !writer.u8(unsignedScaled(output.delayMs, 20)) ||
        !writer.u8(output.polarity == 180 ? 1 : 0) ||
        !writer.boolean(output.userMute)) return false;
  }
  return writer.boolean(global.loudness.enabled) && writer.u8(global.loudness.intensity) &&
         writer.boolean(global.limiter.enabled) &&
         writer.i8(scaled(global.limiter.thresholdDbfs, 2)) &&
         writer.u8(unsignedScaled(global.limiter.attackMs, 2)) &&
         writer.u8(unsignedScaled(global.limiter.releaseMs, .1f)) &&
         writer.u8(static_cast<uint8_t>(global.activePresetId));
}

bool readGlobal(Reader& reader, DspGlobal& global) {
  int8_t signedValue = 0;
  uint8_t value = 0;
  if (!reader.i8(signedValue)) return false;
  global.masterTrimDb = signedValue / 2.0f;
  if (!reader.boolean(global.processingEnabled) || !reader.u8(value)) return false;
  global.outputMode = static_cast<OutputMode>(value);
  if (!reader.u8(value)) return false;
  global.crossoverHz = value;
  if (!reader.boolean(global.hpfMains) || !reader.u8(value)) return false;
  global.subRouting = static_cast<SubRouting>(value);
  if (!reader.u8(value) || value != kOutputCount) return false;
  for (DspOutput& output : global.outputs) {
    if (!reader.i8(signedValue)) return false;
    output.trimDb = signedValue / 2.0f;
    if (!reader.u8(value)) return false;
    output.delayMs = value / 20.0f;
    if (!reader.u8(value) || value > 1) return false;
    output.polarity = value ? 180 : 0;
    if (!reader.boolean(output.userMute)) return false;
  }
  if (!reader.boolean(global.loudness.enabled) ||
      !reader.u8(global.loudness.intensity) ||
      !reader.boolean(global.limiter.enabled) ||
      !reader.i8(signedValue)) return false;
  global.limiter.thresholdDbfs = signedValue / 2.0f;
  if (!reader.u8(value)) return false;
  global.limiter.attackMs = value / 2.0f;
  if (!reader.u8(value)) return false;
  global.limiter.releaseMs = value * 10.0f;
  if (!reader.u8(value)) return false;
  global.activePresetId = static_cast<PresetId>(value);
  return validGlobal(global);
}

bool writeWorking(Writer& writer, const DspWorkingState& working) {
  if (!writer.u8(static_cast<uint8_t>(kPeqBands))) return false;
  for (const PeqBand& band : working.peq) {
    if (!writer.boolean(band.enabled) || !writer.u16(band.frequencyHz) ||
        !writer.i8(scaled(band.gainDb, 2)) ||
        !writer.u8(unsignedScaled(band.q, 10))) return false;
  }
  const Dynamics& dynamics = working.dynamics;
  return writer.boolean(dynamics.enabled) &&
         writer.i8(scaled(dynamics.thresholdDbfs, 2)) &&
         writer.u8(unsignedScaled(dynamics.ratio, 10)) &&
         writer.u8(unsignedScaled(dynamics.attackMs, 1)) &&
         writer.u8(unsignedScaled(dynamics.releaseMs, .1f)) &&
         writer.i8(scaled(dynamics.makeupGainDb, 2));
}

bool readWorking(Reader& reader, DspWorkingState& working) {
  uint8_t value = 0;
  int8_t signedValue = 0;
  if (!reader.u8(value) || value != kPeqBands) return false;
  for (PeqBand& band : working.peq) {
    if (!reader.boolean(band.enabled) || !reader.u16(band.frequencyHz) ||
        !reader.i8(signedValue)) return false;
    band.gainDb = signedValue / 2.0f;
    if (!reader.u8(value)) return false;
    band.q = value / 10.0f;
  }
  Dynamics& dynamics = working.dynamics;
  if (!reader.boolean(dynamics.enabled) || !reader.i8(signedValue)) return false;
  dynamics.thresholdDbfs = signedValue / 2.0f;
  if (!reader.u8(value)) return false;
  dynamics.ratio = value / 10.0f;
  if (!reader.u8(value)) return false;
  dynamics.attackMs = value;
  if (!reader.u8(value)) return false;
  dynamics.releaseMs = value * 10.0f;
  if (!reader.i8(signedValue)) return false;
  dynamics.makeupGainDb = signedValue / 2.0f;
  return validWorking(working);
}

template <typename Byte>
bool validUtf8Name(const Byte* bytes, size_t length) {
  if (!bytes || length == 0 || length > kMaxPresetNameBytes) return false;
  for (size_t i = 0; i < length;) {
    const uint8_t lead = static_cast<uint8_t>(bytes[i++]);
    if (lead < 0x80) {
      if (lead < 0x20 || lead == 0x7f) return false;
      continue;
    }
    unsigned continuation = 0;
    uint32_t codePoint = 0;
    uint32_t minimum = 0;
    if (lead >= 0xc2 && lead <= 0xdf) {
      continuation = 1; codePoint = lead & 0x1f; minimum = 0x80;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      continuation = 2; codePoint = lead & 0x0f; minimum = 0x800;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      continuation = 3; codePoint = lead & 0x07; minimum = 0x10000;
    } else return false;
    if (continuation > length - i) return false;
    while (continuation--) {
      const uint8_t part = static_cast<uint8_t>(bytes[i++]);
      if ((part & 0xc0) != 0x80) return false;
      codePoint = (codePoint << 6) | (part & 0x3f);
    }
    if (codePoint < minimum || codePoint > 0x10ffff ||
        (codePoint >= 0xd800 && codePoint <= 0xdfff)) return false;
  }
  return true;
}

bool presetName(const char* name, size_t& length) {
  if (!name) return false;
  length = 0;
  while (length <= kMaxPresetNameBytes && name[length]) ++length;
  return validUtf8Name(name, length);
}

bool writeTone(Writer& writer, const ToneSnapshot& tone) {
  return writer.i8(tone.bass) && writer.i8(tone.middle) && writer.i8(tone.treble);
}

bool readTone(Reader& reader, ToneSnapshot& tone) {
  return reader.i8(tone.bass) && reader.i8(tone.middle) &&
         reader.i8(tone.treble) && validTone(tone);
}

}  // namespace

Live defaultLive() {
  Live result{};
  result.global = defaultGlobal();
  result.working = defaultWorking();
  return result;
}

bool defaultUserPreset(PresetId id, DspPreset& out) {
  if (!presetWritable(id)) return false;
  DspPreset result = factoryPreset(PresetId::Flat);
  result.id = id;
  result.name = id == PresetId::User1 ? "User 1" : "User 2";
  result.writable = true;
  out = result;
  return true;
}

bool encodeLive(const Live& value, uint8_t* buffer, size_t capacity, size_t& written) {
  written = 0;
  if (!buffer || capacity < kLiveRecordBytes ||
      !validGlobal(value.global) || !validWorking(value.working)) return false;
  Writer writer(buffer, capacity);
  return header(writer, kLiveType, kLivePayloadBytes) &&
         writeGlobal(writer, value.global) && writeWorking(writer, value.working) &&
         finishRecord(writer, buffer, kLiveRecordBytes, written);
}

bool decodeLive(const uint8_t* buffer, size_t length, Live& out) {
  size_t payloadLength = 0;
  if (!checkHeader(buffer, length, kLiveType, kLivePayloadBytes,
                   kLivePayloadBytes, payloadLength)) return false;
  Reader reader(buffer + kHeaderBytes, payloadLength);
  Live decoded = defaultLive();
  if (!readGlobal(reader, decoded.global) ||
      !readWorking(reader, decoded.working) || !reader.finished()) return false;
  out = decoded;
  return true;
}

bool encodeUserPreset(const DspPreset& value, uint8_t* buffer, size_t capacity,
                      size_t& written) {
  written = 0;
  size_t nameLength = 0;
  if (!buffer || !presetWritable(value.id) || !value.writable ||
      !presetName(value.name, nameLength) || !validTone(value.toneSnapshot) ||
      !validWorking(value.working)) return false;
  const size_t payloadLength = kUserFixedPayloadBytes + nameLength;
  const size_t totalLength = kHeaderBytes + payloadLength + kCrcBytes;
  if (capacity < totalLength) return false;
  Writer writer(buffer, capacity);
  if (!header(writer, kUserType, payloadLength) ||
      !writer.u8(static_cast<uint8_t>(value.id)) ||
      !writer.u8(static_cast<uint8_t>(nameLength))) return false;
  for (size_t i = 0; i < nameLength; ++i)
    if (!writer.u8(static_cast<uint8_t>(value.name[i]))) return false;
  return writeTone(writer, value.toneSnapshot) && writeWorking(writer, value.working) &&
         finishRecord(writer, buffer, totalLength, written);
}

bool decodeUserPreset(const uint8_t* buffer, size_t length, DspPreset& out,
                      char* nameBuffer, size_t nameCapacity) {
  size_t payloadLength = 0;
  if (!nameBuffer || !checkHeader(buffer, length, kUserType,
      kUserFixedPayloadBytes + 1, kUserFixedPayloadBytes + kMaxPresetNameBytes,
      payloadLength)) return false;
  Reader reader(buffer + kHeaderBytes, payloadLength);
  uint8_t id = 0, nameLength = 0;
  if (!reader.u8(id) || !presetWritable(static_cast<PresetId>(id)) ||
      !reader.u8(nameLength) || nameLength == 0 || nameLength > kMaxPresetNameBytes ||
      payloadLength != kUserFixedPayloadBytes + nameLength ||
      nameCapacity <= nameLength) return false;
  const uint8_t* name = reader.take(nameLength);
  if (!validUtf8Name(name, nameLength)) return false;
  DspPreset decoded{};
  decoded.id = static_cast<PresetId>(id);
  decoded.writable = true;
  decoded.working = defaultWorking();
  if (!readTone(reader, decoded.toneSnapshot) ||
      !readWorking(reader, decoded.working) || !reader.finished()) return false;
  std::memcpy(nameBuffer, name, nameLength);
  nameBuffer[nameLength] = '\0';
  decoded.name = nameBuffer;
  out = decoded;
  return true;
}

bool encodePending(const Pending& value, uint8_t* buffer, size_t capacity,
                   size_t& written) {
  written = 0;
  if (!buffer || capacity < kPendingRecordBytes || !validPresetId(value.targetPresetId) ||
      value.targetLive.global.activePresetId != value.targetPresetId ||
      !validTone(value.targetTone) || !validGlobal(value.targetLive.global) ||
      !validWorking(value.targetLive.working)) return false;
  Writer writer(buffer, capacity);
  return header(writer, kPendingType, kPendingPayloadBytes) &&
         writer.u8(static_cast<uint8_t>(value.targetPresetId)) &&
         writeTone(writer, value.targetTone) &&
         writeGlobal(writer, value.targetLive.global) &&
         writeWorking(writer, value.targetLive.working) &&
         finishRecord(writer, buffer, kPendingRecordBytes, written);
}

bool decodePending(const uint8_t* buffer, size_t length, Pending& out) {
  size_t payloadLength = 0;
  if (!checkHeader(buffer, length, kPendingType, kPendingPayloadBytes,
                   kPendingPayloadBytes, payloadLength)) return false;
  Reader reader(buffer + kHeaderBytes, payloadLength);
  Pending decoded{};
  uint8_t id = 0;
  if (!reader.u8(id) || !validPresetId(static_cast<PresetId>(id))) return false;
  decoded.targetPresetId = static_cast<PresetId>(id);
  if (!readTone(reader, decoded.targetTone) ||
      !readGlobal(reader, decoded.targetLive.global) ||
      !readWorking(reader, decoded.targetLive.working) || !reader.finished() ||
      decoded.targetLive.global.activePresetId != decoded.targetPresetId) return false;
  out = decoded;
  return true;
}

}  // namespace storage_format
}  // namespace dsp
}  // namespace voxone
