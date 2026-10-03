#ifndef VOXONE_DSP_STORAGE_FORMAT_H
#define VOXONE_DSP_STORAGE_FORMAT_H

#include "dsp_model.h"

#include <cstddef>
#include <cstdint>

namespace voxone {
namespace dsp {
namespace storage_format {

constexpr uint8_t kSchemaVersion = 1;
constexpr size_t kMaxPresetNameBytes = kMaxDspPresetNameBytes;
constexpr size_t kHeaderBytes = 8;
constexpr size_t kCrcBytes = 4;
constexpr size_t kGlobalPayloadBytes = 7 + kOutputCount * 4 + 2 + 4 + 1;
constexpr size_t kWorkingPayloadBytes = 1 + kPeqBands * 5 + 6;
constexpr size_t kLivePayloadBytes = kGlobalPayloadBytes + kWorkingPayloadBytes;
constexpr size_t kUserFixedPayloadBytes = 1 + 1 + 3 + kWorkingPayloadBytes;
constexpr size_t kPendingPayloadBytes = 1 + 3 + kLivePayloadBytes;
constexpr size_t kLiveRecordBytes = kHeaderBytes + kLivePayloadBytes + kCrcBytes;
constexpr size_t kUserPresetMaxRecordBytes =
    kHeaderBytes + kUserFixedPayloadBytes + kMaxPresetNameBytes + kCrcBytes;
constexpr size_t kPendingRecordBytes = kHeaderBytes + kPendingPayloadBytes + kCrcBytes;
constexpr size_t kMaxRecordBytes = kPendingRecordBytes;

static_assert(kLiveRecordBytes == 74, "Unexpected DSP live format size");
static_assert(kUserPresetMaxRecordBytes == 73, "Unexpected DSP user format size");
static_assert(kPendingRecordBytes == 78, "Unexpected DSP pending format size");

struct Live {
  DspGlobal global{};
  DspWorkingState working{};
};

struct Pending {
  PresetId targetPresetId = PresetId::Flat;
  ToneSnapshot targetTone{};
  Live targetLive{};
};

Live defaultLive();
bool defaultUserPreset(PresetId id, DspPreset& out);

// All encoders write to caller-owned memory and set written=0 on failure.
bool encodeLive(const Live& value, uint8_t* buffer, size_t capacity, size_t& written);
bool decodeLive(const uint8_t* buffer, size_t length, Live& out);

bool encodeUserPreset(const DspPreset& value, uint8_t* buffer, size_t capacity,
                      size_t& written);
// On success out.name points to nameBuffer, which the caller must keep alive.
bool decodeUserPreset(const uint8_t* buffer, size_t length, DspPreset& out,
                      char* nameBuffer, size_t nameCapacity);

bool encodePending(const Pending& value, uint8_t* buffer, size_t capacity, size_t& written);
bool decodePending(const uint8_t* buffer, size_t length, Pending& out);

}  // namespace storage_format
}  // namespace dsp
}  // namespace voxone

#endif
