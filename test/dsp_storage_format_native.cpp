#include "../src/core/dsp_storage_format.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace voxone::dsp;
using namespace voxone::dsp::storage_format;

namespace {

void equalWorking(const DspWorkingState& left, const DspWorkingState& right) {
  for (size_t i = 0; i < kPeqBands; ++i) {
    assert(left.peq[i].enabled == right.peq[i].enabled);
    assert(left.peq[i].frequencyHz == right.peq[i].frequencyHz);
    assert(left.peq[i].gainDb == right.peq[i].gainDb);
    assert(left.peq[i].q == right.peq[i].q);
    assert(std::isfinite(right.peq[i].gainDb) && std::isfinite(right.peq[i].q));
  }
  const Dynamics& a = left.dynamics;
  const Dynamics& b = right.dynamics;
  assert(a.enabled == b.enabled && a.thresholdDbfs == b.thresholdDbfs &&
         a.ratio == b.ratio && a.attackMs == b.attackMs &&
         a.releaseMs == b.releaseMs && a.makeupGainDb == b.makeupGainDb);
  assert(std::isfinite(b.thresholdDbfs) && std::isfinite(b.ratio) &&
         std::isfinite(b.attackMs) && std::isfinite(b.releaseMs) &&
         std::isfinite(b.makeupGainDb));
}

void equalGlobal(const DspGlobal& a, const DspGlobal& b) {
  assert(a.masterTrimDb == b.masterTrimDb &&
         a.processingEnabled == b.processingEnabled && a.outputMode == b.outputMode &&
         a.crossoverHz == b.crossoverHz && a.hpfMains == b.hpfMains &&
         a.subRouting == b.subRouting && a.activePresetId == b.activePresetId);
  for (size_t i = 0; i < kOutputCount; ++i) {
    assert(a.outputs[i].trimDb == b.outputs[i].trimDb &&
           a.outputs[i].delayMs == b.outputs[i].delayMs &&
           a.outputs[i].polarity == b.outputs[i].polarity &&
           a.outputs[i].userMute == b.outputs[i].userMute);
    assert(std::isfinite(b.outputs[i].trimDb) && std::isfinite(b.outputs[i].delayMs));
  }
  assert(a.loudness.enabled == b.loudness.enabled &&
         a.loudness.intensity == b.loudness.intensity &&
         a.limiter.enabled == b.limiter.enabled &&
         a.limiter.thresholdDbfs == b.limiter.thresholdDbfs &&
         a.limiter.attackMs == b.limiter.attackMs &&
         a.limiter.releaseMs == b.limiter.releaseMs);
  assert(std::isfinite(b.masterTrimDb) && std::isfinite(b.limiter.thresholdDbfs) &&
         std::isfinite(b.limiter.attackMs) && std::isfinite(b.limiter.releaseMs));
}

void equalLive(const Live& a, const Live& b) {
  equalGlobal(a.global, b.global);
  equalWorking(a.working, b.working);
}

// Independent test implementation: recompute CRC after deliberately invalid payload edits.
void refreshCrc(uint8_t* bytes, size_t length) {
  uint32_t crc = 0xffffffffUL;
  for (size_t i = 0; i < length - kCrcBytes; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320UL : 0UL);
  }
  crc ^= 0xffffffffUL;
  for (unsigned i = 0; i < kCrcBytes; ++i)
    bytes[length - kCrcBytes + i] = static_cast<uint8_t>(crc >> (8 * i));
}

}  // namespace

int main() {
  std::array<uint8_t, kMaxRecordBytes + 1> encoded{};
  std::array<uint8_t, kMaxRecordBytes + 1> second{};
  size_t liveBytes = 0;
  const Live defaults = defaultLive();
  assert(validGlobal(defaults.global) && validWorking(defaults.working));
  assert(encodeLive(defaults, encoded.data(), encoded.size(), liveBytes));
  assert(liveBytes == kLiveRecordBytes);
  assert(std::memcmp(encoded.data(), "VDSP", 4) == 0);
  assert(encoded[4] == 1 && encoded[5] == kSchemaVersion);
  assert(encoded[6] == kLivePayloadBytes && encoded[7] == 0);
  Live decoded{};
  assert(decodeLive(encoded.data(), liveBytes, decoded));
  equalLive(defaults, decoded);

  Live modified = defaults;
  modified.global.masterTrimDb = -4.5f;
  modified.global.processingEnabled = false;
  modified.global.outputMode = OutputMode::Stereo22;
  modified.global.crossoverHz = 123;
  modified.global.hpfMains = false;
  modified.global.subRouting = SubRouting::Stereo;
  modified.global.outputs[0].trimDb = -6.5f;
  modified.global.outputs[0].delayMs = 1.05f;
  modified.global.outputs[1].polarity = 180;
  modified.global.outputs[3].userMute = true;
  modified.global.loudness.enabled = true;
  modified.global.loudness.intensity = 80;
  modified.global.limiter.thresholdDbfs = -7.5f;
  modified.global.limiter.attackMs = 2.5f;
  modified.global.limiter.releaseMs = 430;
  modified.global.activePresetId = PresetId::User2;
  modified.working.peq[0].enabled = true;
  modified.working.peq[0].frequencyHz = 20;
  modified.working.peq[0].gainDb = -12;
  modified.working.peq[0].q = .3f;
  modified.working.peq[4].enabled = false;
  modified.working.peq[4].frequencyHz = 20000;
  modified.working.peq[4].gainDb = 12;
  modified.working.peq[4].q = 10;
  modified.working.dynamics.enabled = true;
  modified.working.dynamics.thresholdDbfs = -30.5f;
  modified.working.dynamics.ratio = 3.7f;
  modified.working.dynamics.attackMs = 21;
  modified.working.dynamics.releaseMs = 430;
  modified.working.dynamics.makeupGainDb = 5.5f;
  assert(validGlobal(modified.global) && validWorking(modified.working));
  assert(encodeLive(modified, encoded.data(), encoded.size(), liveBytes));
  assert(decodeLive(encoded.data(), liveBytes, decoded));
  equalLive(modified, decoded);
  size_t secondBytes = 0;
  assert(encodeLive(modified, second.data(), second.size(), secondBytes));
  assert(secondBytes == liveBytes && std::memcmp(encoded.data(), second.data(), liveBytes) == 0);
  assert(!encodeLive(modified, second.data(), liveBytes - 1, secondBytes) && secondBytes == 0);

  auto damaged = encoded;
  damaged[0] ^= 1;
  assert(!decodeLive(damaged.data(), liveBytes, decoded));
  damaged = encoded;
  damaged[5] = kSchemaVersion + 1;
  refreshCrc(damaged.data(), liveBytes);
  assert(!decodeLive(damaged.data(), liveBytes, decoded));
  damaged = encoded;
  damaged[6]++;
  refreshCrc(damaged.data(), liveBytes);
  assert(!decodeLive(damaged.data(), liveBytes, decoded));
  assert(!decodeLive(encoded.data(), liveBytes - 1, decoded));
  damaged = encoded;
  damaged[liveBytes] = 0xaa;
  assert(!decodeLive(damaged.data(), liveBytes + 1, decoded));
  damaged = encoded;
  damaged[8 + 2] = 99;  // outputMode
  refreshCrc(damaged.data(), liveBytes);
  assert(!decodeLive(damaged.data(), liveBytes, decoded));
  damaged = encoded;
  damaged[8 + 1] = 2;  // noncanonical boolean
  refreshCrc(damaged.data(), liveBytes);
  assert(!decodeLive(damaged.data(), liveBytes, decoded));
  damaged = encoded;
  damaged[8 + 6] = 3;  // output count
  refreshCrc(damaged.data(), liveBytes);
  assert(!decodeLive(damaged.data(), liveBytes, decoded));
  damaged = encoded;
  damaged[8 + kGlobalPayloadBytes] = 4;  // PEQ count
  refreshCrc(damaged.data(), liveBytes);
  assert(!decodeLive(damaged.data(), liveBytes, decoded));
  damaged = encoded;
  damaged[8 + kGlobalPayloadBytes + 2] = 19;  // first PEQ frequency, LE
  damaged[8 + kGlobalPayloadBytes + 3] = 0;
  refreshCrc(damaged.data(), liveBytes);
  assert(!decodeLive(damaged.data(), liveBytes, decoded));
  damaged = encoded;
  damaged[8 + 8] = 255;  // first output delay: 12.75 ms
  refreshCrc(damaged.data(), liveBytes);
  assert(!decodeLive(damaged.data(), liveBytes, decoded));
  damaged = encoded;
  damaged[8 + 6] ^= 1;  // CRC rejects a one-byte change without repair
  assert(!decodeLive(damaged.data(), liveBytes, decoded));
  Live invalid = modified;
  invalid.global.masterTrimDb = std::numeric_limits<float>::quiet_NaN();
  assert(!encodeLive(invalid, second.data(), second.size(), secondBytes));
  invalid = modified;
  invalid.working.peq[0].q = std::numeric_limits<float>::infinity();
  assert(!encodeLive(invalid, second.data(), second.size(), secondBytes));

  DspPreset user{};
  assert(defaultUserPreset(PresetId::User1, user));
  assert(!defaultUserPreset(PresetId::Flat, user));
  assert(user.writable && user.id == PresetId::User1);
  size_t asciiBytes = 0;
  assert(encodeUserPreset(user, encoded.data(), encoded.size(), asciiBytes));
  assert(asciiBytes == 49 + std::strlen("User 1"));
  char nameBuffer[kMaxPresetNameBytes + 1]{};
  DspPreset decodedUser{};
  assert(decodeUserPreset(encoded.data(), asciiBytes, decodedUser,
                          nameBuffer, sizeof(nameBuffer)));
  assert(decodedUser.id == PresetId::User1 && decodedUser.writable);
  assert(decodedUser.name == nameBuffer && std::strcmp(decodedUser.name, "User 1") == 0);
  equalWorking(user.working, decodedUser.working);
  assert(!encodeUserPreset(factoryPreset(PresetId::Flat), second.data(),
                           second.size(), secondBytes));

  user.name = u8"Żółć";
  user.toneSnapshot = ToneSnapshot{-2, 3, 6};
  size_t utf8Bytes = 0;
  assert(encodeUserPreset(user, encoded.data(), encoded.size(), utf8Bytes));
  assert(utf8Bytes == 49 + std::strlen(user.name));
  assert(decodeUserPreset(encoded.data(), utf8Bytes, decodedUser,
                          nameBuffer, sizeof(nameBuffer)));
  assert(std::strcmp(decodedUser.name, user.name) == 0);
  assert(decodedUser.toneSnapshot.bass == -2 &&
         decodedUser.toneSnapshot.middle == 3 && decodedUser.toneSnapshot.treble == 6);
  assert(!decodeUserPreset(encoded.data(), utf8Bytes, decodedUser, nameBuffer, 4));
  damaged = encoded;
  damaged[10] = 0xc0;  // invalid UTF-8 lead, but valid CRC
  refreshCrc(damaged.data(), utf8Bytes);
  assert(!decodeUserPreset(damaged.data(), utf8Bytes, decodedUser,
                           nameBuffer, sizeof(nameBuffer)));
  const char brokenUtf8[] = {'B', static_cast<char>(0xc3), 0};
  user.name = brokenUtf8;
  assert(!encodeUserPreset(user, second.data(), second.size(), secondBytes));

  char maxName[kMaxPresetNameBytes + 1];
  std::memset(maxName, 'A', kMaxPresetNameBytes);
  maxName[kMaxPresetNameBytes] = '\0';
  user.name = maxName;
  size_t maxUserBytes = 0;
  assert(encodeUserPreset(user, encoded.data(), encoded.size(), maxUserBytes));
  assert(maxUserBytes == kUserPresetMaxRecordBytes);
  char longName[kMaxPresetNameBytes + 2];
  std::memset(longName, 'A', kMaxPresetNameBytes + 1);
  longName[kMaxPresetNameBytes + 1] = '\0';
  user.name = longName;
  assert(!encodeUserPreset(user, second.data(), second.size(), secondBytes));

  Pending pending{};
  pending.targetPresetId = PresetId::User2;
  pending.targetTone = ToneSnapshot{5, -2, 4};
  pending.targetLive = modified;
  size_t pendingBytes = 0;
  assert(encodePending(pending, encoded.data(), encoded.size(), pendingBytes));
  assert(pendingBytes == kPendingRecordBytes);
  Pending decodedPending{};
  assert(decodePending(encoded.data(), pendingBytes, decodedPending));
  assert(decodedPending.targetPresetId == pending.targetPresetId);
  assert(decodedPending.targetTone.bass == pending.targetTone.bass &&
         decodedPending.targetTone.middle == pending.targetTone.middle &&
         decodedPending.targetTone.treble == pending.targetTone.treble);
  equalLive(pending.targetLive, decodedPending.targetLive);
  pending.targetPresetId = PresetId::User1;
  assert(!encodePending(pending, second.data(), second.size(), secondBytes));
  damaged = encoded;
  damaged[8] = static_cast<uint8_t>(PresetId::User1);
  refreshCrc(damaged.data(), pendingBytes);
  assert(!decodePending(damaged.data(), pendingBytes, decodedPending));

  DemoBackend backend;
  DspModel model(backend);
  assert(model.init());
  Live fromModel{};
  fromModel.global = model.global();
  fromModel.working = model.working();
  assert(encodeLive(fromModel, encoded.data(), encoded.size(), liveBytes));
  assert(model.setMasterMute(true));
  assert(model.runtime().revision > 0 && model.runtime().appliedRevision > 0);
  fromModel.global = model.global();
  fromModel.working = model.working();
  assert(encodeLive(fromModel, second.data(), second.size(), secondBytes));
  assert(liveBytes == secondBytes &&
         std::memcmp(encoded.data(), second.data(), liveBytes) == 0);

  std::printf("DSP storage v%u: live=%zu user_ascii=%zu user_utf8=%zu "
              "user_max=%zu pending=%zu max=%zu\n",
              kSchemaVersion, liveBytes, asciiBytes, utf8Bytes,
              maxUserBytes, pendingBytes, kMaxRecordBytes);
}
