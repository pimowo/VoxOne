#include "display_scroll_persistence.h"

#include <string.h>

namespace voxone {
namespace display_scroll {
namespace {

constexpr uint8_t kMagic[4] = {'V', 'X', 'S', 'C'};
constexpr size_t kMagicOffset = 0;
constexpr size_t kVersionOffset = 4;
constexpr size_t kEnabledOffset = 5;
constexpr size_t kSpeedOffset = 6;
constexpr size_t kPauseOffset = 7;
constexpr size_t kCrcOffset = 9;

void write16(uint8_t* output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
}

void write32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
  output[2] = static_cast<uint8_t>(value >> 16);
  output[3] = static_cast<uint8_t>(value >> 24);
}

uint16_t read16(const uint8_t* input) {
  return static_cast<uint16_t>(input[0]) |
         static_cast<uint16_t>(input[1]) << 8;
}

uint32_t read32(const uint8_t* input) {
  return static_cast<uint32_t>(input[0]) |
         static_cast<uint32_t>(input[1]) << 8 |
         static_cast<uint32_t>(input[2]) << 16 |
         static_cast<uint32_t>(input[3]) << 24;
}

ScrollSettingsLoadResult rendererDefaults(
    ScrollSettingsLoadStatus status,
    ScrollSettingsRecordStatus recordStatus =
        ScrollSettingsRecordStatus::NotDecoded) {
  return ScrollSettingsLoadResult{defaultScrollSettings(),
                                  ScrollSettingsSource::RendererDefaults,
                                  status, recordStatus};
}

ScrollSettingsSaveStatus mapWriteStatus(
    ScrollSettingsStorageWriteStatus status) {
  switch (status) {
    case ScrollSettingsStorageWriteStatus::Stored:
      return ScrollSettingsSaveStatus::Saved;
    case ScrollSettingsStorageWriteStatus::OpenError:
      return ScrollSettingsSaveStatus::StorageOpenError;
    case ScrollSettingsStorageWriteStatus::WriteError:
      return ScrollSettingsSaveStatus::StorageWriteError;
    case ScrollSettingsStorageWriteStatus::CommitError:
      return ScrollSettingsSaveStatus::StorageCommitError;
    case ScrollSettingsStorageWriteStatus::VerifyError:
      return ScrollSettingsSaveStatus::StorageVerifyError;
  }
  return ScrollSettingsSaveStatus::StorageWriteError;
}

ScrollSettingsEraseStatus mapEraseStatus(
    ScrollSettingsStorageEraseStatus status) {
  switch (status) {
    case ScrollSettingsStorageEraseStatus::Erased:
      return ScrollSettingsEraseStatus::Erased;
    case ScrollSettingsStorageEraseStatus::Missing:
      return ScrollSettingsEraseStatus::Unchanged;
    case ScrollSettingsStorageEraseStatus::OpenError:
      return ScrollSettingsEraseStatus::StorageOpenError;
    case ScrollSettingsStorageEraseStatus::EraseError:
      return ScrollSettingsEraseStatus::StorageEraseError;
    case ScrollSettingsStorageEraseStatus::CommitError:
      return ScrollSettingsEraseStatus::StorageCommitError;
    case ScrollSettingsStorageEraseStatus::VerifyError:
      return ScrollSettingsEraseStatus::StorageVerifyError;
  }
  return ScrollSettingsEraseStatus::StorageEraseError;
}

}  // namespace

static_assert(kCrcOffset + sizeof(uint32_t) == kScrollSettingsRecordSize,
              "scroll settings record layout mismatch");

bool equalScrollSettings(const ScrollSettings& left,
                         const ScrollSettings& right) {
  return left.enabled == right.enabled && left.speed == right.speed &&
         left.pauseMs == right.pauseMs;
}

uint32_t scrollSettingsRecordCrc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  if (!data && size) return 0;
  for (size_t index = 0; index < size; ++index) {
    crc ^= data[index];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return crc ^ 0xffffffffu;
}

bool serializeScrollSettingsRecord(const ScrollSettings& settings,
                                   uint8_t* output, size_t outputSize) {
  if (!output || outputSize != kScrollSettingsRecordSize ||
      !isValidScrollSettings(settings)) return false;

  uint8_t record[kScrollSettingsRecordSize]{};
  memcpy(record + kMagicOffset, kMagic, sizeof(kMagic));
  record[kVersionOffset] = kScrollSettingsRecordVersion;
  record[kEnabledOffset] = settings.enabled ? 1 : 0;
  record[kSpeedOffset] = static_cast<uint8_t>(settings.speed);
  write16(record + kPauseOffset, settings.pauseMs);
  write32(record + kCrcOffset,
          scrollSettingsRecordCrc32(record, kCrcOffset));
  memcpy(output, record, sizeof(record));
  return true;
}

ScrollSettingsRecordStatus deserializeScrollSettingsRecord(
    const uint8_t* input, size_t inputSize, ScrollSettings& output) {
  if (!input || inputSize != kScrollSettingsRecordSize)
    return ScrollSettingsRecordStatus::InvalidSize;
  if (memcmp(input + kMagicOffset, kMagic, sizeof(kMagic)) != 0)
    return ScrollSettingsRecordStatus::InvalidMagic;
  if (input[kVersionOffset] != kScrollSettingsRecordVersion)
    return ScrollSettingsRecordStatus::InvalidVersion;
  if (read32(input + kCrcOffset) !=
      scrollSettingsRecordCrc32(input, kCrcOffset))
    return ScrollSettingsRecordStatus::InvalidCrc;
  if (!isValidScrollEnabledValue(input[kEnabledOffset]))
    return ScrollSettingsRecordStatus::InvalidEnabled;
  const ScrollSpeed speed = static_cast<ScrollSpeed>(input[kSpeedOffset]);
  if (!isValidScrollSpeed(speed))
    return ScrollSettingsRecordStatus::InvalidSpeed;
  const uint16_t pauseMs = read16(input + kPauseOffset);
  if (!isValidScrollPause(pauseMs))
    return ScrollSettingsRecordStatus::InvalidPause;

  output = ScrollSettings{input[kEnabledOffset] != 0, speed, pauseMs};
  return ScrollSettingsRecordStatus::Valid;
}

ScrollSettingsLoadResult loadScrollSettings(ScrollSettingsStorage& storage) {
  uint8_t record[kScrollSettingsRecordSize]{};
  size_t size = 0;
  const ScrollSettingsStorageReadStatus read =
      storage.read(record, sizeof(record), size);
  if (read == ScrollSettingsStorageReadStatus::Missing)
    return rendererDefaults(
        ScrollSettingsLoadStatus::RendererDefaultsMissing);
  if (read == ScrollSettingsStorageReadStatus::OpenError)
    return rendererDefaults(
        ScrollSettingsLoadStatus::RendererDefaultsOpenError);
  if (read == ScrollSettingsStorageReadStatus::ReadError)
    return rendererDefaults(
        ScrollSettingsLoadStatus::RendererDefaultsReadError);

  ScrollSettings settings = defaultScrollSettings();
  const ScrollSettingsRecordStatus decoded =
      deserializeScrollSettingsRecord(record, size, settings);
  if (decoded != ScrollSettingsRecordStatus::Valid)
    return rendererDefaults(ScrollSettingsLoadStatus::RendererDefaultsInvalid,
                            decoded);
  return ScrollSettingsLoadResult{settings, ScrollSettingsSource::UserSettings,
                                  ScrollSettingsLoadStatus::UserSettings,
                                  ScrollSettingsRecordStatus::Valid};
}

ScrollSettingsSaveStatus saveScrollSettings(
    ScrollSettingsStorage& storage, const ScrollSettingsLoadResult& current,
    const ScrollSettings& requested) {
  if (!isValidScrollSettings(requested))
    return ScrollSettingsSaveStatus::Invalid;
  if (current.source == ScrollSettingsSource::UserSettings &&
      current.recordStatus == ScrollSettingsRecordStatus::Valid &&
      equalScrollSettings(current.settings, requested))
    return ScrollSettingsSaveStatus::Unchanged;

  uint8_t record[kScrollSettingsRecordSize]{};
  if (!serializeScrollSettingsRecord(requested, record, sizeof(record)))
    return ScrollSettingsSaveStatus::Invalid;
  return mapWriteStatus(storage.write(record, sizeof(record)));
}

ScrollSettingsEraseStatus eraseScrollSettings(ScrollSettingsStorage& storage) {
  return mapEraseStatus(storage.erase());
}

}  // namespace display_scroll
}  // namespace voxone
