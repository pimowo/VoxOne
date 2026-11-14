#if defined(ARDUINO)
#include "options.h"
#endif
#include "config_restore.h"

// Consolidated from config_backup.cpp.
#include <algorithm>
#include <cstring>
#include <limits>

#include "station_format.h"

namespace voxone {
namespace config_backup {
namespace {

constexpr uint8_t kMagic[4] = {'V', 'X', 'C', 'F'};
constexpr std::size_t kChecksumSize = 4;
constexpr std::size_t kMaxStationLine = 512;
constexpr std::size_t kMaxWifiProfiles = 5;

class MemoryReader : public ByteReader {
 public:
  explicit MemoryReader(const Bytes& bytes) : bytes_(bytes) {}
  std::size_t size() const override { return bytes_.size; }
  bool read(std::size_t offset, uint8_t* output,
            std::size_t length) override {
    if ((length && (!output || !bytes_.data)) || offset > bytes_.size ||
        length > bytes_.size - offset) return false;
    if (length) std::memcpy(output, bytes_.data + offset, length);
    return true;
  }
 private:
  Bytes bytes_;
};

bool readByte(ByteReader& reader, std::size_t offset, uint8_t& value) {
  return reader.read(offset, &value, 1);
}

void write16(uint8_t*& cursor, uint16_t value) {
  *cursor++ = static_cast<uint8_t>(value);
  *cursor++ = static_cast<uint8_t>(value >> 8);
}

void write32(uint8_t*& cursor, uint32_t value) {
  write16(cursor, static_cast<uint16_t>(value));
  write16(cursor, static_cast<uint16_t>(value >> 16));
}

uint16_t read16(const uint8_t*& cursor) {
  const uint16_t value = static_cast<uint16_t>(cursor[0]) |
                         (static_cast<uint16_t>(cursor[1]) << 8);
  cursor += 2;
  return value;
}

uint32_t read32(const uint8_t*& cursor) {
  const uint32_t value = read16(cursor);
  return value | (static_cast<uint32_t>(read16(cursor)) << 16);
}

bool terminated(const char* text, std::size_t size) {
  return std::memchr(text, 0, size) != nullptr;
}

bool validConfig(const config_format::config_v7_t& value) {
  const config_format::config_v6_t& f = value.fields;
  return config_format::validateConfigV7(value) && f.volume <= 254 &&
         f.balance >= -16 && f.balance <= 16 &&
         f.trebble >= -6 && f.trebble <= 6 &&
         f.middle >= -6 && f.middle <= 6 &&
         f.bass >= -6 && f.bass <= 6 && f.lastSSID <= kMaxWifiProfiles &&
         f.smartstart <= 2 && f.brightness <= 100 &&
         f.maximumVolume >= 1 && f.maximumVolume <= 100 &&
         f.startupMode <= 1 && f.startupFixedVolume <= 100 &&
         f.lastUserVolume <= 100 && terminated(f.sntp1, sizeof(f.sntp1)) &&
         terminated(f.sntp2, sizeof(f.sntp2)) &&
         terminated(f.mdnsname, sizeof(f.mdnsname));
}

bool validUi(const UiTimeoutSettings& value) {
  return value.stationListSeconds <= 120 && value.btTransportSeconds <= 120;
}

bool validScroll(const Data& data) {
  if (data.scrollMode == ScrollBackupMode::PreserveExisting ||
      data.scrollMode == ScrollBackupMode::RendererDefaults) return true;
  return data.scrollMode == ScrollBackupMode::UserSettings &&
         display_scroll::isValidScrollSettings(data.scrollSettings);
}

bool validWifi(ByteReader& value, std::size_t& profiles) {
  profiles = 0;
  std::size_t start = 0;
  while (start < value.size()) {
    std::size_t end = start;
    uint8_t ch = 0;
    while (end < value.size()) {
      if (!readByte(value, end, ch)) return false;
      if (ch == '\n') break;
      ++end;
    }
    std::size_t length = end - start;
    if (length) {
      if (!readByte(value, start + length - 1, ch)) return false;
      if (ch == '\r') --length;
    }
    if (!length || length > 69) return false;
    uint8_t line[70];
    if (!value.read(start, line, length)) return false;
    std::size_t tab = 0;
    while (tab < length && line[tab] != '\t') ++tab;
    if (tab == 0 || tab > 29 || tab == length || length - tab - 1 > 39)
      return false;
    for (std::size_t i = 0; i < length; ++i)
      if (line[i] == 0 || (i != tab && line[i] == '\t')) return false;
    if (++profiles > kMaxWifiProfiles) return false;
    start = end < value.size() ? end + 1 : end;
  }
  return true;
}

bool validStations(ByteReader& value, std::size_t& rows) {
  const std::size_t headerSize = sizeof(kStationsHeader) - 1;
  uint8_t header[sizeof(kStationsHeader) - 1];
  if (value.size() < headerSize || !value.read(0, header, headerSize) ||
      std::memcmp(header, kStationsHeader, headerSize) != 0) return false;
  rows = 0;
  std::size_t start = headerSize;
  while (start < value.size()) {
    std::size_t end = start;
    uint8_t ch = 0;
    while (end < value.size()) {
      if (!readByte(value, end, ch)) return false;
      if (ch == '\n') break;
      ++end;
    }
    std::size_t length = end - start;
    if (length) {
      if (!readByte(value, start + length - 1, ch)) return false;
      if (ch == '\r') --length;
    }
    if (!length || length >= kMaxStationLine) return false;
    char line[kMaxStationLine];
    if (!value.read(start, reinterpret_cast<uint8_t*>(line), length)) return false;
    line[length] = '\0';
    uint64_t id = 0;
    char* name = nullptr;
    char* url = nullptr;
    int ovol = 0;
    uint8_t metadata = 0;
    if (!stationParseFields(line, id, name, url, ovol, metadata)) return false;
    std::size_t prior = headerSize;
    while (prior < start) {
      if (start - prior < 16) return false;
      char priorId[17];
      if (!value.read(prior, reinterpret_cast<uint8_t*>(priorId), 16)) return false;
      priorId[16] = '\0';
      uint64_t parsedId = 0;
      if (!stationParseId(priorId, parsedId) || parsedId == id) return false;
      while (prior < start) {
        if (!readByte(value, prior, ch)) return false;
        if (ch == '\n') break;
        ++prior;
      }
      if (prior < start) ++prior;
    }
    if (++rows > std::numeric_limits<uint16_t>::max()) return false;
    start = end < value.size() ? end + 1 : end;
  }
  return true;
}

Status validateFixed(const Data& data,
                     const hardware::HardwareCapabilities& capabilities) {
  if (!validConfig(data.config)) return Status::InvalidConfig;
  const hardware::RuntimeHardwareValidation hardwareStatus =
      hardware::validateRuntimeHardwareConfig(data.hardware, capabilities);
  if (hardwareStatus == hardware::RuntimeHardwareValidation::Unsupported)
    return Status::UnsupportedHardware;
  if (hardwareStatus != hardware::RuntimeHardwareValidation::Valid)
    return Status::InvalidHardware;
  if (!validUi(data.ui)) return Status::InvalidUi;
  if (!validScroll(data)) return Status::InvalidScroll;
  if (data.config.btEnabled && !capabilities.supportsVoxOneBt)
    return Status::InvalidConfig;
  return Status::Ok;
}

}  // namespace

void Crc32::update(const uint8_t* data, std::size_t size) {
  if (!data) return;
  for (std::size_t i = 0; i < size; ++i) {
    value_ ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      value_ = (value_ >> 1) ^ ((value_ & 1u) ? 0xedb88320u : 0u);
  }
}

Status validateExternal(const Data& data,
                        const hardware::HardwareCapabilities& capabilities,
                        ByteReader& wifi, ByteReader& stations) {
  const Status fixed = validateFixed(data, capabilities);
  if (fixed != Status::Ok) return fixed;
  if (wifi.size() != data.wifi.size || stations.size() != data.stations.size)
    return Status::InvalidLength;
  std::size_t wifiProfiles = 0;
  if (!validWifi(wifi, wifiProfiles)) return Status::InvalidWifi;
  std::size_t stationRows = 0;
  if (!validStations(stations, stationRows)) return Status::InvalidStations;
  if (data.config.fields.lastSSID > wifiProfiles ||
      data.config.fields.lastStation > stationRows) return Status::InvalidConfig;
  return Status::Ok;
}

const char* statusName(Status status) {
  switch (status) {
    case Status::Ok: return "OK";
    case Status::InvalidArgument: return "INVALID_ARGUMENT";
    case Status::Empty: return "EMPTY";
    case Status::Truncated: return "TRUNCATED";
    case Status::WrongMagic: return "WRONG_MAGIC";
    case Status::WrongSchema: return "WRONG_SCHEMA";
    case Status::InvalidLength: return "INVALID_LENGTH";
    case Status::InvalidChecksum: return "INVALID_CHECKSUM";
    case Status::InvalidConfig: return "INVALID_CONFIG";
    case Status::InvalidHardware: return "INVALID_HARDWARE";
    case Status::UnsupportedHardware: return "UNSUPPORTED_HARDWARE";
    case Status::InvalidUi: return "INVALID_UI";
    case Status::InvalidScroll: return "INVALID_SCROLL";
    case Status::InvalidWifi: return "INVALID_WIFI";
    case Status::InvalidStations: return "INVALID_STATIONS";
    case Status::OutputTooSmall: return "OUTPUT_TOO_SMALL";
    case Status::AllocationFailed: return "ALLOCATION_FAILED";
    case Status::StorageReadFailed: return "STORAGE_READ_FAILED";
    case Status::StorageBeginFailed: return "STORAGE_BEGIN_FAILED";
    case Status::StorageWriteFailed: return "STORAGE_WRITE_FAILED";
    case Status::StorageCommitFailed: return "STORAGE_COMMIT_FAILED";
    case Status::RecoveryPending: return "RECOVERY_PENDING";
  }
  return "UNKNOWN";
}

Status StreamEncoder::begin(
    const Data& data, const hardware::HardwareCapabilities& capabilities,
    ByteReader& wifi, ByteReader& stations) {
  ready_ = false;
  data_ = nullptr;
  wifi_ = nullptr;
  stations_ = nullptr;
  if (data.scrollMode == ScrollBackupMode::PreserveExisting)
    return status_ = Status::InvalidScroll;
  total_ = serializedSize(data);
  checksumValue_ = 0;
  if (!total_) return status_ = Status::InvalidLength;
  status_ = validateExternal(data, capabilities, wifi, stations);
  if (status_ != Status::Ok) return status_;
  status_ = encodePrefix(data, capabilities, prefix_, sizeof(prefix_));
  if (status_ != Status::Ok) return status_;
  Crc32 crc;
  crc.update(prefix_, sizeof(prefix_));
  uint8_t buffer[1024];
  for (ByteReader* reader : {&wifi, &stations}) {
    for (std::size_t offset = 0; offset < reader->size();) {
      const std::size_t count =
          std::min(sizeof(buffer), reader->size() - offset);
      if (!reader->read(offset, buffer, count))
        return status_ = Status::StorageReadFailed;
      crc.update(buffer, count);
      offset += count;
    }
  }
  checksumValue_ = crc.value();
  for (unsigned i = 0; i < sizeof(checksum_); ++i)
    checksum_[i] = static_cast<uint8_t>(checksumValue_ >> (8 * i));
  data_ = &data;
  wifi_ = &wifi;
  stations_ = &stations;
  ready_ = true;
  return status_ = Status::Ok;
}

std::size_t StreamEncoder::read(std::size_t offset, uint8_t* output,
                                std::size_t capacity) {
  if (!ready_ || !data_ || !wifi_ || !stations_ || !output ||
      offset >= total_) return 0;
  const std::size_t wanted = std::min(capacity, total_ - offset);
  std::size_t copied = 0;
  bool success = true;
  auto copyMemory = [&](const uint8_t* source, std::size_t length) {
    if (!success || copied >= wanted) return;
    if (offset >= length) { offset -= length; return; }
    const std::size_t count = std::min(wanted - copied, length - offset);
    std::memcpy(output + copied, source + offset, count);
    copied += count;
    offset = 0;
  };
  auto copyReader = [&](ByteReader& reader, std::size_t length) {
    if (!success || copied >= wanted) return;
    if (offset >= length) { offset -= length; return; }
    const std::size_t count = std::min(wanted - copied, length - offset);
    if (!reader.read(offset, output + copied, count)) {
      success = false;
      return;
    }
    copied += count;
    offset = 0;
  };
  copyMemory(prefix_, sizeof(prefix_));
  copyReader(*wifi_, data_->wifi.size);
  copyReader(*stations_, data_->stations.size);
  copyMemory(checksum_, sizeof(checksum_));
  if (!success) {
    ready_ = false;
    status_ = Status::StorageReadFailed;
    return 0;
  }
  return copied;
}

std::size_t serializedSize(const Data& data) {
  if (data.scrollMode == ScrollBackupMode::PreserveExisting) return 0;
  if (data.wifi.size > std::numeric_limits<uint32_t>::max() ||
      data.stations.size > std::numeric_limits<uint32_t>::max()) return 0;
  if (data.wifi.size > std::numeric_limits<std::size_t>::max() -
                           data.stations.size) return 0;
  const std::size_t variable = data.wifi.size + data.stations.size;
  if (variable > std::numeric_limits<std::size_t>::max() - kBackupFixedOverhead)
    return 0;
  return kBackupFixedOverhead + variable;
}

Status encodePrefix(const Data& data,
                    const hardware::HardwareCapabilities& capabilities,
                    uint8_t* output, std::size_t capacity) {
  if (!output) return Status::InvalidArgument;
  if (capacity < kBackupVariableOffset) return Status::OutputTooSmall;
  const Status validation = validateFixed(data, capabilities);
  if (validation != Status::Ok) return validation;
  if (data.scrollMode == ScrollBackupMode::PreserveExisting)
    return Status::InvalidScroll;
  const std::size_t total = serializedSize(data);
  if (!total || total > std::numeric_limits<uint32_t>::max())
    return Status::InvalidLength;
  uint8_t* cursor = output;
  std::memcpy(cursor, kMagic, sizeof(kMagic)); cursor += sizeof(kMagic);
  write16(cursor, kBackupSchemaVersion);
  write32(cursor, static_cast<uint32_t>(total));
  write32(cursor, config_format::kConfigV7SerializedSize);
  write32(cursor, hardware::kRuntimeHardwareConfigSerializedSize);
  write32(cursor, kRetiredIntegrationWireSize);
  write32(cursor, kUiWireSize);
  write32(cursor, static_cast<uint32_t>(data.wifi.size));
  write32(cursor, static_cast<uint32_t>(data.stations.size));
  if (!config_format::serializeConfigV7(data.config, cursor,
                                        config_format::kConfigV7SerializedSize))
    return Status::InvalidConfig;
  cursor += config_format::kConfigV7SerializedSize;
  if (!hardware::serializeRuntimeHardwareConfig(
          data.hardware, cursor, hardware::kRuntimeHardwareConfigSerializedSize))
    return Status::InvalidHardware;
  cursor += hardware::kRuntimeHardwareConfigSerializedSize;
  std::memset(cursor, 0, kRetiredIntegrationWireSize);
  cursor += kRetiredIntegrationWireSize;
  *cursor++ = 1;
  *cursor++ = data.ui.stationListSeconds;
  *cursor++ = data.ui.btTransportSeconds;
  std::memset(cursor, 0, kScrollWireSize);
  if (data.scrollMode == ScrollBackupMode::UserSettings) {
    *cursor = 1;
    if (!display_scroll::serializeScrollSettingsRecord(
            data.scrollSettings, cursor + 1,
            display_scroll::kScrollSettingsRecordSize))
      return Status::InvalidScroll;
  }
  cursor += kScrollWireSize;
  return static_cast<std::size_t>(cursor - output) == kBackupVariableOffset
             ? Status::Ok : Status::InvalidLength;
}

Status decodePrefix(const uint8_t* input, std::size_t size,
                    std::size_t artifactSize,
                    const hardware::HardwareCapabilities& capabilities,
                    Data& output) {
  if (!input) return Status::InvalidArgument;
  if (size < sizeof(kMagic) + sizeof(uint16_t)) return Status::Truncated;
  if (std::memcmp(input, kMagic, sizeof(kMagic)) != 0) return Status::WrongMagic;
  const uint8_t* cursor = input + sizeof(kMagic);
  const uint16_t schema = read16(cursor);
  const std::size_t variableOffset = backupVariableOffsetForSchema(schema);
  const std::size_t fixedOverhead = backupFixedOverheadForSchema(schema);
  if (!variableOffset) return Status::WrongSchema;
  if (size < variableOffset || artifactSize < fixedOverhead)
    return Status::Truncated;
  const uint32_t total = read32(cursor);
  const uint32_t configSize = read32(cursor);
  const uint32_t hardwareSize = read32(cursor);
  const uint32_t retiredIntegrationSize = read32(cursor);
  const uint32_t uiSize = read32(cursor);
  const uint32_t wifiSize = read32(cursor);
  const uint32_t stationsSize = read32(cursor);
  if (total > artifactSize) return Status::Truncated;
  if (total < artifactSize || configSize != config_format::kConfigV7SerializedSize ||
      hardwareSize != hardware::kRuntimeHardwareConfigSerializedSize ||
      retiredIntegrationSize != kRetiredIntegrationWireSize ||
      uiSize != kUiWireSize) return Status::InvalidLength;
  const uint64_t expected = static_cast<uint64_t>(fixedOverhead) +
                            wifiSize + stationsSize;
  if (expected != artifactSize)
    return expected > artifactSize ? Status::Truncated : Status::InvalidLength;
  Data candidate{};
  if (!config_format::deserializeConfigV7(cursor, configSize, candidate.config))
    return Status::InvalidConfig;
  cursor += configSize;
  const hardware::RuntimeHardwareDecodeStatus hardwareDecode =
      hardware::deserializeRuntimeHardwareConfig(cursor, hardwareSize,
                                                 candidate.hardware);
  if (hardwareDecode != hardware::RuntimeHardwareDecodeStatus::Valid)
    return Status::InvalidHardware;
  cursor += hardwareSize;
  std::memcpy(candidate.retiredIntegration, cursor,
              kRetiredIntegrationWireSize);
  cursor += retiredIntegrationSize;
  if (cursor[0] != 1) return Status::InvalidUi;
  candidate.ui.stationListSeconds = cursor[1];
  candidate.ui.btTransportSeconds = cursor[2];
  cursor += kUiWireSize;
  if (schema == kLegacyBackupSchemaVersion) {
    candidate.scrollMode = ScrollBackupMode::PreserveExisting;
  } else {
    const uint8_t present = cursor[0];
    const uint8_t* record = cursor + 1;
    if (present == 0) {
      for (std::size_t i = 0;
           i < display_scroll::kScrollSettingsRecordSize; ++i)
        if (record[i] != 0) return Status::InvalidScroll;
      candidate.scrollMode = ScrollBackupMode::RendererDefaults;
      candidate.scrollSettings = display_scroll::defaultScrollSettings();
    } else if (present == 1) {
      if (display_scroll::deserializeScrollSettingsRecord(
              record, display_scroll::kScrollSettingsRecordSize,
              candidate.scrollSettings) !=
          display_scroll::ScrollSettingsRecordStatus::Valid)
        return Status::InvalidScroll;
      candidate.scrollMode = ScrollBackupMode::UserSettings;
    } else {
      return Status::InvalidScroll;
    }
  }
  candidate.wifi = {nullptr, wifiSize};
  candidate.stations = {nullptr, stationsSize};
  const Status validation = validateFixed(candidate, capabilities);
  if (validation != Status::Ok) return validation;
  output = candidate;
  return Status::Ok;
}

Status serialize(const Data& data,
                 const hardware::HardwareCapabilities& capabilities,
                 uint8_t* output, std::size_t capacity,
                 std::size_t& written) {
  written = 0;
  if (!output) return Status::InvalidArgument;
  MemoryReader wifi(data.wifi);
  MemoryReader stations(data.stations);
  const Status validation = validateExternal(data, capabilities, wifi, stations);
  if (validation != Status::Ok) return validation;
  if (data.scrollMode == ScrollBackupMode::PreserveExisting)
    return Status::InvalidScroll;
  const std::size_t total = serializedSize(data);
  if (!total || total > std::numeric_limits<uint32_t>::max())
    return Status::InvalidLength;
  if (capacity < total) return Status::OutputTooSmall;

  const Status prefix = encodePrefix(data, capabilities, output, capacity);
  if (prefix != Status::Ok) return prefix;
  uint8_t* cursor = output + kBackupVariableOffset;
  if (data.wifi.size) {
    if (cursor != data.wifi.data)
      std::memcpy(cursor, data.wifi.data, data.wifi.size);
    cursor += data.wifi.size;
  }
  if (cursor != data.stations.data)
    std::memcpy(cursor, data.stations.data, data.stations.size);
  cursor += data.stations.size;
  Crc32 checksum;
  checksum.update(output, total - kChecksumSize);
  write32(cursor, checksum.value());
  written = static_cast<std::size_t>(cursor - output);
  return written == total ? Status::Ok : Status::InvalidLength;
}

Status parse(const uint8_t* input, std::size_t size,
             const hardware::HardwareCapabilities& capabilities,
             Data& output) {
  if (!input) return Status::InvalidArgument;
  if (!size) return Status::Empty;
  if (size < sizeof(kMagic) + sizeof(uint16_t)) return Status::Truncated;
  if (std::memcmp(input, kMagic, sizeof(kMagic)) != 0) return Status::WrongMagic;
  const uint8_t* header = input + sizeof(kMagic);
  const uint16_t schema = read16(header);
  const std::size_t variableOffset = backupVariableOffsetForSchema(schema);
  const std::size_t fixedOverhead = backupFixedOverheadForSchema(schema);
  if (!variableOffset) return Status::WrongSchema;
  if (size < fixedOverhead) return Status::Truncated;
  const uint32_t total = read32(header);
  const uint32_t configSize = read32(header);
  const uint32_t hardwareSize = read32(header);
  const uint32_t retiredIntegrationSize = read32(header);
  const uint32_t uiSize = read32(header);
  const uint32_t wifiSize = read32(header);
  const uint32_t stationsSize = read32(header);
  if (total > size) return Status::Truncated;
  if (total < size || configSize != config_format::kConfigV7SerializedSize ||
      hardwareSize != hardware::kRuntimeHardwareConfigSerializedSize ||
      retiredIntegrationSize != kRetiredIntegrationWireSize ||
      uiSize != kUiWireSize) return Status::InvalidLength;
  const uint64_t expected = static_cast<uint64_t>(fixedOverhead) +
                            wifiSize + stationsSize;
  if (expected != size) return expected > size ? Status::Truncated : Status::InvalidLength;
  const uint8_t* checksum = input + size - kChecksumSize;
  const uint8_t* checksumCursor = checksum;
  Crc32 calculated;
  calculated.update(input, size - kChecksumSize);
  if (read32(checksumCursor) != calculated.value())
    return Status::InvalidChecksum;
  Data candidate{};
  const Status decoded = decodePrefix(input, variableOffset, size,
                                      capabilities, candidate);
  if (decoded != Status::Ok) return decoded;
  const uint8_t* cursor = input + variableOffset;
  candidate.wifi.data = cursor;
  cursor += candidate.wifi.size;
  candidate.stations.data = cursor;
  MemoryReader wifi(candidate.wifi);
  MemoryReader stations(candidate.stations);
  const Status validation = validateExternal(candidate, capabilities, wifi, stations);
  if (validation != Status::Ok) return validation;
  output = candidate;
  return Status::Ok;
}

RestoreResult restore(const uint8_t* input, std::size_t size,
                      const hardware::HardwareCapabilities& capabilities,
                      RestoreStorage& storage) {
  Data candidate{};
  const Status parsed = parse(input, size, capabilities, candidate);
  if (parsed != Status::Ok) return {parsed, false};
  return restoreValidated(candidate, storage);
}

RestoreResult restoreValidated(const Data& candidate, RestoreStorage& storage) {
  if (!storage.begin()) return {Status::StorageBeginFailed, false};
  if (!storage.stage(candidate)) {
    return {storage.rollback() ? Status::StorageWriteFailed
                               : Status::RecoveryPending, false};
  }
  if (!storage.commit()) {
    return {storage.rollback() ? Status::StorageCommitFailed
                               : Status::RecoveryPending, false};
  }
  return {Status::Ok, true};
}

}  // namespace config_backup
}  // namespace voxone

// Consolidated from config_backup_web.cpp.
namespace voxone {
namespace config_backup_web {

UploadDecision UploadState::append(std::size_t index, std::size_t length,
                                   std::size_t total) {
  if (complete_) return UploadDecision::AlreadyComplete;
  if (!started_) {
    if (index != 0) return UploadDecision::InvalidOffset;
    if (!total || total > kMaxUploadBytes) return UploadDecision::InvalidSize;
    total_ = total;
    started_ = true;
  }
  if (total != total_ || index != received_ || length > total_ - received_)
    return UploadDecision::InvalidOffset;
  received_ += length;
  if (received_ == total_) {
    complete_ = true;
    return UploadDecision::Complete;
  }
  return UploadDecision::Accept;
}

ApiResult mapRestoreResult(const config_backup::RestoreResult& result) {
  using config_backup::Status;
  switch (result.status) {
    case Status::Ok:
      return {200, nullptr, result.restartRequired};
    case Status::WrongSchema:
      return {422, "wrong_schema", false};
    case Status::InvalidChecksum:
      return {422, "bad_crc", false};
    case Status::UnsupportedHardware:
      return {422, "unsupported_hardware", false};
    case Status::RecoveryPending:
      return {503, "recovery_pending", false};
    case Status::StorageBeginFailed:
    case Status::StorageReadFailed:
    case Status::AllocationFailed:
    case Status::StorageWriteFailed:
    case Status::StorageCommitFailed:
      return {500, "restore_failed", false};
    case Status::Empty:
    case Status::Truncated:
    case Status::WrongMagic:
    case Status::InvalidLength:
    case Status::InvalidConfig:
    case Status::InvalidHardware:
    case Status::InvalidUi:
    case Status::InvalidScroll:
    case Status::InvalidWifi:
    case Status::InvalidStations:
    case Status::InvalidArgument:
    case Status::OutputTooSmall:
      return {400, "invalid_file", false};
  }
  return {500, "restore_failed", false};
}

}  // namespace config_backup_web
}  // namespace voxone

// Consolidated from config_restore_transaction.cpp.
namespace voxone {
namespace config_restore {

bool TransactionStorage::begin() {
  Phase phase = Phase::Prepared;
  const MarkerStatus marker = backend_.readMarker(phase);
  if (marker != MarkerStatus::None) return false;
  return backend_.cleanupStale();
}

bool TransactionStorage::stage(const config_backup::Data& candidate) {
  // Neither operation changes active configuration. The marker is made
  // durable only after both candidate staging and rollback snapshot verify.
  return backend_.stageCandidate(candidate) &&
         backend_.createRollbackSnapshot() &&
         backend_.writeMarker(Phase::Prepared);
}

Step TransactionStorage::commitStep() {
  Phase phase = Phase::Prepared;
  if (backend_.readMarker(phase) != MarkerStatus::Pending) return Step::Failed;
  switch (phase) {
    case Phase::Prepared:
      return backend_.apply(Component::Config) &&
                     backend_.writeMarker(Phase::ConfigWritten)
                 ? Step::InProgress : Step::Failed;
    case Phase::ConfigWritten:
      return backend_.apply(Component::Hardware) &&
                     backend_.writeMarker(Phase::HardwareWritten)
                 ? Step::InProgress : Step::Failed;
    case Phase::HardwareWritten:
      // Marker schema 1 used this numeric checkpoint for a retired
      // integration. Advance through it without touching legacy NVS data.
      return backend_.writeMarker(Phase::RetiredIntegrationWritten)
                 ? Step::InProgress : Step::Failed;
    case Phase::RetiredIntegrationWritten:
      return backend_.apply(Component::Ui) &&
                     backend_.writeMarker(Phase::UiWritten)
                 ? Step::InProgress : Step::Failed;
    case Phase::UiWritten:
      return backend_.apply(Component::Scroll) &&
                     backend_.writeMarker(Phase::ScrollWritten)
                 ? Step::InProgress : Step::Failed;
    case Phase::ScrollWritten:
      return backend_.apply(Component::Wifi) &&
                     backend_.writeMarker(Phase::WifiWritten)
                 ? Step::InProgress : Step::Failed;
    case Phase::WifiWritten:
      return backend_.apply(Component::Stations) &&
                     backend_.writeMarker(Phase::StationsWritten)
                 ? Step::InProgress : Step::Failed;
    case Phase::StationsWritten:
      return backend_.rebuildStationsIndex() &&
                     backend_.writeMarker(Phase::IndexRebuilt)
                 ? Step::InProgress : Step::Failed;
    case Phase::IndexRebuilt:
      return backend_.verifyCandidate() &&
                     backend_.writeMarker(Phase::Verified)
                 ? Step::InProgress : Step::Failed;
    case Phase::Verified:
      if (!backend_.clearMarker()) return Step::Failed;
      // Once the marker is durably absent, the new complete configuration is
      // authoritative. Stale cleanup can safely be retried on a later boot.
      backend_.cleanupStale();
      return Step::Complete;
  }
  return Step::Failed;
}

bool TransactionStorage::commit() {
  for (;;) {
    const Step step = commitStep();
    if (step == Step::Complete) return true;
    if (step == Step::Failed) return false;
  }
}

bool TransactionStorage::rollback() {
  return rollbackSucceeded_ = recover();
}

bool TransactionStorage::recover() {
  Phase phase = Phase::Prepared;
  const MarkerStatus marker = backend_.readMarker(phase);
  if (marker == MarkerStatus::None) return backend_.cleanupStale();
  if (marker == MarkerStatus::Error) return false;
  if (!backend_.restoreRollbackSnapshot() ||
      !backend_.verifyRollbackSnapshot() || !backend_.clearMarker()) return false;
  backend_.cleanupStale();
  return true;
}

}  // namespace config_restore
}  // namespace voxone

// Consolidated from config_restore_adapter.cpp.
#if defined(ARDUINO)
#include <EEPROM.h>
#include <SPIFFS.h>
#include <nvs.h>

#include <algorithm>
#include <cstring>
#include <new>

#include "config.h"
#include "config_eeprom_storage.h"
#include "display_scroll.h"
#include "playlist_store.h"
#include "ui_timeout_config.h"
#include "../hardware/hardware_descriptor.h"
#include "../hardware/runtime_hardware_config.h"

namespace voxone {
namespace config_restore {
namespace {

constexpr char kNamespace[] = "voxrestore";
constexpr char kMarkerKey[] = "marker";
constexpr char kFlagsKey[] = "flags";
constexpr char kConfigKey[] = "config";
constexpr char kHardwareKey[] = "hardware";
constexpr char kUiKey[] = "ui";
constexpr char kScrollKey[] = "scroll";
constexpr char kWifiMetaKey[] = "wifimeta";
constexpr char kStationsMetaKey[] = "stameta";
constexpr char kWifiOld[] = "/data/.vxrestore.wifi.old";
constexpr char kStationsOld[] = "/data/.vxrestore.stations.old";
constexpr char kWifiNew[] = "/data/.vxrestore.wifi.new";
constexpr char kStationsNew[] = "/data/.vxrestore.stations.new";
constexpr uint32_t kMarkerMagic = 0x52584356u;  // VXCR, little endian.
constexpr uint8_t kMarkerSchema = 1;
constexpr std::size_t kMarkerSize = 12;

enum SnapshotFlag : uint8_t {
  ConfigPresent = 1u << 0,
  HardwarePresent = 1u << 1,
  // Bit 2 was used by a retired integration. It remains reserved so marker
  // schema 1 snapshots keep their durable bit assignments.
  UiPresent = 1u << 3,
  WifiPresent = 1u << 4,
  StationsPresent = 1u << 5,
  ScrollPresent = 1u << 6,
  // Distinguishes a new snapshot of an absent scroll record from snapshots
  // created by older firmware, where scroll was outside the transaction.
  ScrollCaptured = 1u << 7
};

struct FileFingerprint {
  uint32_t size;
  uint32_t crc;
};

uint32_t crc32(const uint8_t* data, std::size_t size) {
  uint32_t crc = 0xffffffffu;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return crc ^ 0xffffffffu;
}

void put32(uint8_t* output, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    output[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t get32(const uint8_t* input) {
  return static_cast<uint32_t>(input[0]) |
         (static_cast<uint32_t>(input[1]) << 8) |
         (static_cast<uint32_t>(input[2]) << 16) |
         (static_cast<uint32_t>(input[3]) << 24);
}

void encodeMarker(Phase phase, uint8_t (&bytes)[kMarkerSize]) {
  std::memset(bytes, 0, sizeof(bytes));
  put32(bytes, kMarkerMagic);
  bytes[4] = kMarkerSchema;
  bytes[5] = static_cast<uint8_t>(phase);
  put32(bytes + 8, crc32(bytes, 8));
}

MarkerStatus readMarkerRecord(Phase& phase) {
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return MarkerStatus::None;
  if (opened != ESP_OK) return MarkerStatus::Error;
  uint8_t bytes[kMarkerSize]{};
  std::size_t size = sizeof(bytes);
  const esp_err_t result = nvs_get_blob(handle, kMarkerKey, bytes, &size);
  nvs_close(handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return MarkerStatus::None;
  if (result != ESP_OK || size != sizeof(bytes) ||
      get32(bytes) != kMarkerMagic || bytes[4] != kMarkerSchema ||
      bytes[5] < static_cast<uint8_t>(Phase::Prepared) ||
      bytes[5] > static_cast<uint8_t>(Phase::ScrollWritten) ||
      get32(bytes + 8) != crc32(bytes, 8)) return MarkerStatus::Error;
  phase = static_cast<Phase>(bytes[5]);
  return MarkerStatus::Pending;
}

bool removeIfPresent(const char* path) {
  return !SPIFFS.exists(path) || SPIFFS.remove(path);
}

bool writeFile(const char* path, const uint8_t* bytes, std::size_t size) {
  if ((size && !bytes) || !removeIfPresent(path)) return false;
  File file = SPIFFS.open(path, "w");
  if (!file) return false;
  const bool written = !size || file.write(bytes, size) == size;
  file.flush();
  file.close();
  return written;
}

bool fileEquals(const char* path, const uint8_t* bytes, std::size_t size) {
  if ((size && !bytes) || !SPIFFS.exists(path)) return false;
  File file = SPIFFS.open(path, "r");
  if (!file || file.size() != size) { file.close(); return false; }
  uint8_t buffer[256];
  std::size_t position = 0;
  while (position < size) {
    const std::size_t count = size - position < sizeof(buffer)
                                  ? size - position : sizeof(buffer);
    if (file.read(buffer, count) != count ||
        std::memcmp(buffer, bytes + position, count) != 0) {
      file.close();
      return false;
    }
    position += count;
  }
  file.close();
  return true;
}

bool fingerprintFile(const char* path, FileFingerprint& result) {
  File file = SPIFFS.open(path, "r");
  if (!file || file.size() > UINT32_MAX) { file.close(); return false; }
  config_backup::Crc32 crc;
  uint8_t buffer[512];
  while (file.available()) {
    const std::size_t count = file.read(buffer, sizeof(buffer));
    if (!count) { file.close(); return false; }
    crc.update(buffer, count);
  }
  result.size = static_cast<uint32_t>(file.size());
  result.crc = crc.value();
  file.close();
  return true;
}

bool fileMatches(const char* path, const FileFingerprint& expected) {
  FileFingerprint actual{};
  return fingerprintFile(path, actual) && actual.size == expected.size &&
         actual.crc == expected.crc;
}

class FileByteReader : public config_backup::ByteReader {
 public:
  FileByteReader() = default;
  FileByteReader(File& file, std::size_t size, bool* failed = nullptr)
      : file_(&file), size_(size), failed_(failed) {}
  void reset(File& file, std::size_t size, bool* failed = nullptr) {
    file_ = &file;
    size_ = size;
    failed_ = failed;
  }
  std::size_t size() const override { return size_; }
  bool read(std::size_t offset, uint8_t* output,
            std::size_t length) override {
    if ((length && !output) || offset > size_ || length > size_ - offset) {
      if (failed_) *failed_ = true;
      return false;
    }
    const bool ok = !length ||
        (file_ && *file_ && file_->seek(offset) &&
         file_->read(output, length) == length);
    if (!ok && failed_) *failed_ = true;
    return ok;
  }
 private:
  File* file_ = nullptr;
  std::size_t size_ = 0;
  bool* failed_ = nullptr;
};

bool installTransactionalFile(const char* staged, const char* active,
                              const char* rollback) {
  if (!SPIFFS.exists(staged) || !removeIfPresent(rollback)) return false;
  if (SPIFFS.exists(active) && !SPIFFS.rename(active, rollback)) return false;
  return SPIFFS.rename(staged, active);
}

template <typename T>
bool setBlob(nvs_handle_t handle, const char* key, const T& value) {
  return nvs_set_blob(handle, key, &value, sizeof(value)) == ESP_OK;
}

template <typename T>
bool getBlob(nvs_handle_t handle, const char* key, T& value) {
  std::size_t size = sizeof(value);
  return nvs_get_blob(handle, key, &value, &size) == ESP_OK &&
         size == sizeof(value);
}

bool readScrollState(config_backup::ScrollBackupMode& mode,
                     display_scroll::ScrollSettings& settings) {
  const display_scroll::ScrollSettingsLoadResult loaded =
      display_scroll::readScrollSettingsPersistent();
  if (loaded.status ==
      display_scroll::ScrollSettingsLoadStatus::RendererDefaultsMissing) {
    mode = config_backup::ScrollBackupMode::RendererDefaults;
    settings = display_scroll::defaultScrollSettings();
    return true;
  }
  if (loaded.status == display_scroll::ScrollSettingsLoadStatus::UserSettings &&
      loaded.source == display_scroll::ScrollSettingsSource::UserSettings &&
      loaded.recordStatus ==
          display_scroll::ScrollSettingsRecordStatus::Valid) {
    mode = config_backup::ScrollBackupMode::UserSettings;
    settings = loaded.settings;
    return true;
  }
  return false;
}

bool applyScrollState(config_backup::ScrollBackupMode mode,
                      const display_scroll::ScrollSettings& settings) {
  using config_backup::ScrollBackupMode;
  if (mode == ScrollBackupMode::PreserveExisting) return true;
  if (mode == ScrollBackupMode::RendererDefaults) {
    const display_scroll::ScrollSettingsEraseStatus erased =
        display_scroll::eraseScrollSettingsPersistent();
    return erased == display_scroll::ScrollSettingsEraseStatus::Erased ||
           erased == display_scroll::ScrollSettingsEraseStatus::Unchanged;
  }
  if (mode != ScrollBackupMode::UserSettings ||
      !display_scroll::isValidScrollSettings(settings)) return false;
  const display_scroll::ScrollSettingsLoadResult current =
      display_scroll::readScrollSettingsPersistent();
  const display_scroll::ScrollSettingsSaveStatus saved =
      display_scroll::writeScrollSettingsPersistent(current, settings);
  return saved == display_scroll::ScrollSettingsSaveStatus::Saved ||
         saved == display_scroll::ScrollSettingsSaveStatus::Unchanged;
}

bool verifyScrollState(config_backup::ScrollBackupMode mode,
                       const display_scroll::ScrollSettings& settings) {
  using config_backup::ScrollBackupMode;
  if (mode == ScrollBackupMode::PreserveExisting) return true;
  const display_scroll::ScrollSettingsLoadResult actual =
      display_scroll::readScrollSettingsPersistent();
  if (mode == ScrollBackupMode::RendererDefaults)
    return actual.status ==
           display_scroll::ScrollSettingsLoadStatus::RendererDefaultsMissing;
  return mode == ScrollBackupMode::UserSettings &&
         actual.status == display_scroll::ScrollSettingsLoadStatus::UserSettings &&
         actual.source == display_scroll::ScrollSettingsSource::UserSettings &&
         actual.recordStatus == display_scroll::ScrollSettingsRecordStatus::Valid &&
         display_scroll::equalScrollSettings(actual.settings, settings);
}

class ProductionBackend : public Backend {
 public:
  explicit ProductionBackend(bool stagedFiles = false)
      : stagedFiles_(stagedFiles) {}
  MarkerStatus readMarker(Phase& phase) override { return readMarkerRecord(phase); }

  bool cleanupStale() override {
    bool files = removeIfPresent(kWifiOld) && removeIfPresent(kStationsOld) &&
                 ((stagedFiles_ && !candidate_) ||
                  (removeIfPresent(kWifiNew) &&
                   removeIfPresent(kStationsNew)));
    nvs_handle_t handle;
    const esp_err_t opened = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return files;
    if (opened != ESP_OK) return false;
    const bool nvs = nvs_erase_all(handle) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return files && nvs;
  }

  bool stageCandidate(const config_backup::Data& candidate) override {
    candidate_ = &candidate;
    if (stagedFiles_) {
      return fingerprintFile(kWifiNew, candidateWifi_) &&
             fingerprintFile(kStationsNew, candidateStations_) &&
             candidateWifi_.size == candidate.wifi.size &&
             candidateStations_.size == candidate.stations.size;
    }
    return writeFile(kWifiNew, candidate.wifi.data, candidate.wifi.size) &&
           fileEquals(kWifiNew, candidate.wifi.data, candidate.wifi.size) &&
           writeFile(kStationsNew, candidate.stations.data, candidate.stations.size) &&
           fileEquals(kStationsNew, candidate.stations.data, candidate.stations.size);
  }

  bool createRollbackSnapshot() override {
    uint8_t flags = 0;
    uint8_t configBytes[config_format::kConfigV7SerializedSize]{};
    bool found = false;
    config_storage::ConfigEepromStorage configStore;
    if (!configStore.readRecord(configBytes, sizeof(configBytes), found)) return false;
    if (found) {
      config_format::config_v7_t checked{};
      if (config_format::parseConfigV7(configBytes, sizeof(configBytes), checked) !=
          config_format::ConfigRecordStatus::LOADED_V7) return false;
      flags |= ConfigPresent;
    }
    hardware::RuntimeHardwareConfig hardwareConfig{};
    if (!hardware::readRuntimeHardwarePersistent(hardwareConfig, found)) return false;
    if (found) {
      if (hardware::validateRuntimeHardwareConfig(
              hardwareConfig, hardware::hardwareCapabilities()) !=
          hardware::RuntimeHardwareValidation::Valid) return false;
      flags |= HardwarePresent;
    }
    UiTimeoutSettings ui{};
    if (!uiTimeoutReadPersistent(ui, found)) return false;
    if (found) flags |= UiPresent;
    config_backup::ScrollBackupMode scrollMode =
        config_backup::ScrollBackupMode::RendererDefaults;
    display_scroll::ScrollSettings scrollSettings =
        display_scroll::defaultScrollSettings();
    uint8_t scrollBytes[display_scroll::kScrollSettingsRecordSize]{};
    if (!readScrollState(scrollMode, scrollSettings)) return false;
    flags |= ScrollCaptured;
    if (scrollMode == config_backup::ScrollBackupMode::UserSettings) {
      if (!display_scroll::serializeScrollSettingsRecord(
              scrollSettings, scrollBytes, sizeof(scrollBytes))) return false;
      flags |= ScrollPresent;
    }
    FileFingerprint wifiMeta{}, stationsMeta{};
    if (SPIFFS.exists(SSIDS_PATH)) {
      if (!fingerprintFile(SSIDS_PATH, wifiMeta)) return false;
      flags |= WifiPresent;
    }
    if (SPIFFS.exists(PLAYLIST_PATH)) {
      if (!fingerprintFile(PLAYLIST_PATH, stationsMeta)) return false;
      flags |= StationsPresent;
    }

    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    bool ok = nvs_erase_all(handle) == ESP_OK &&
              nvs_set_u8(handle, kFlagsKey, flags) == ESP_OK;
    if (ok && (flags & ConfigPresent))
      ok = nvs_set_blob(handle, kConfigKey, configBytes, sizeof(configBytes)) == ESP_OK;
    if (ok && (flags & HardwarePresent)) ok = setBlob(handle, kHardwareKey, hardwareConfig);
    if (ok && (flags & UiPresent)) ok = setBlob(handle, kUiKey, ui);
    if (ok && (flags & ScrollPresent))
      ok = nvs_set_blob(handle, kScrollKey, scrollBytes,
                        sizeof(scrollBytes)) == ESP_OK;
    if (ok && (flags & WifiPresent)) ok = setBlob(handle, kWifiMetaKey, wifiMeta);
    if (ok && (flags & StationsPresent))
      ok = setBlob(handle, kStationsMetaKey, stationsMeta);
    ok = ok && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok && verifyStoredSnapshot(flags, configBytes, hardwareConfig,
                                      ui, scrollBytes, wifiMeta, stationsMeta);
  }

  bool writeMarker(Phase phase) override {
    uint8_t expected[kMarkerSize];
    encodeMarker(phase, expected);
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    const bool written = nvs_set_blob(handle, kMarkerKey, expected, sizeof(expected)) == ESP_OK &&
                         nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    Phase actual = Phase::Prepared;
    return written && readMarker(actual) == MarkerStatus::Pending && actual == phase;
  }

  bool apply(Component component) override {
    if (!candidate_) return false;
    switch (component) {
      case Component::Config: return writeConfig(candidate_->config);
      case Component::Hardware:
        return hardware::writeRuntimeHardwarePersistent(candidate_->hardware);
      case Component::Ui: return uiTimeoutWritePersistent(candidate_->ui);
      case Component::Scroll:
        return applyScrollState(candidate_->scrollMode,
                                candidate_->scrollSettings);
      case Component::Wifi:
        return installTransactionalFile(kWifiNew, SSIDS_PATH, kWifiOld);
      case Component::Stations:
        return installTransactionalFile(kStationsNew, PLAYLIST_PATH, kStationsOld);
    }
    return false;
  }

  bool rebuildStationsIndex() override {
    if (!removeIfPresent(INDEX_PATH) || !playlistStore.begin()) return false;
    return playlistStore.rebuildIndex();
  }

  bool verifyCandidate() override {
    if (!candidate_ || !verifyConfig(candidate_->config)) return false;
    hardware::RuntimeHardwareConfig hardwareConfig{};
    bool found = false;
    if (!hardware::readRuntimeHardwarePersistent(hardwareConfig, found) || !found ||
        hardwareConfig != candidate_->hardware) return false;
    UiTimeoutSettings ui{};
    if (!uiTimeoutReadPersistent(ui, found) || !found ||
        std::memcmp(&ui, &candidate_->ui, sizeof(ui)) != 0) return false;
    if (!verifyScrollState(candidate_->scrollMode,
                           candidate_->scrollSettings)) return false;
    if (stagedFiles_)
      return fileMatches(SSIDS_PATH, candidateWifi_) &&
             fileMatches(PLAYLIST_PATH, candidateStations_);
    return fileEquals(SSIDS_PATH, candidate_->wifi.data, candidate_->wifi.size) &&
           fileEquals(PLAYLIST_PATH, candidate_->stations.data, candidate_->stations.size);
  }

  bool restoreRollbackSnapshot() override {
    uint8_t flags = 0;
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK ||
        nvs_get_u8(handle, kFlagsKey, &flags) != ESP_OK) return false;
    uint8_t configBytes[config_format::kConfigV7SerializedSize]{};
    hardware::RuntimeHardwareConfig hardwareConfig{};
    UiTimeoutSettings ui{};
    uint8_t scrollBytes[display_scroll::kScrollSettingsRecordSize]{};
    display_scroll::ScrollSettings scrollSettings =
        display_scroll::defaultScrollSettings();
    FileFingerprint wifiMeta{}, stationsMeta{};
    bool read = true;
    if (flags & ConfigPresent) {
      std::size_t size = sizeof(configBytes);
      read = nvs_get_blob(handle, kConfigKey, configBytes, &size) == ESP_OK &&
             size == sizeof(configBytes);
    }
    if (read && (flags & HardwarePresent)) read = getBlob(handle, kHardwareKey, hardwareConfig);
    if (read && (flags & UiPresent)) read = getBlob(handle, kUiKey, ui);
    if (read && (flags & ScrollPresent)) {
      std::size_t size = sizeof(scrollBytes);
      read = nvs_get_blob(handle, kScrollKey, scrollBytes, &size) == ESP_OK &&
             size == sizeof(scrollBytes) &&
             display_scroll::deserializeScrollSettingsRecord(
                 scrollBytes, sizeof(scrollBytes), scrollSettings) ==
                 display_scroll::ScrollSettingsRecordStatus::Valid;
    }
    if (read && (flags & WifiPresent))
      read = getBlob(handle, kWifiMetaKey, wifiMeta);
    if (read && (flags & StationsPresent))
      read = getBlob(handle, kStationsMetaKey, stationsMeta);
    nvs_close(handle);
    if (!read) return false;

    config_storage::ConfigEepromStorage configStore;
    bool restored = (flags & ConfigPresent)
        ? configStore.writeRecord(configBytes, sizeof(configBytes)) && configStore.commit()
        : config_storage::eraseEepromRecord();
    restored = restored && ((flags & HardwarePresent)
        ? hardware::writeRuntimeHardwarePersistent(hardwareConfig)
        : hardware::eraseRuntimeHardwarePersistent());
    restored = restored && ((flags & UiPresent)
        ? uiTimeoutWritePersistent(ui) : uiTimeoutErasePersistent());
    restored = restored && applyScrollState(
        !(flags & ScrollCaptured)
            ? config_backup::ScrollBackupMode::PreserveExisting
        : flags & ScrollPresent
            ? config_backup::ScrollBackupMode::UserSettings
            : config_backup::ScrollBackupMode::RendererDefaults,
        scrollSettings);
    if (!restored || !restoreTransactionalFile(flags & WifiPresent, kWifiOld,
                                               SSIDS_PATH, wifiMeta) ||
        !restoreTransactionalFile(flags & StationsPresent, kStationsOld,
                                  PLAYLIST_PATH, stationsMeta) ||
        !removeIfPresent(INDEX_PATH)) return false;
    if (flags & StationsPresent) return rebuildStationsIndex();
    return true;
  }

  bool verifyRollbackSnapshot() override {
    uint8_t flags = 0;
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK ||
        nvs_get_u8(handle, kFlagsKey, &flags) != ESP_OK) return false;
    uint8_t configBytes[config_format::kConfigV7SerializedSize]{};
    hardware::RuntimeHardwareConfig hardwareConfig{};
    UiTimeoutSettings ui{};
    uint8_t scrollBytes[display_scroll::kScrollSettingsRecordSize]{};
    display_scroll::ScrollSettings scrollSettings =
        display_scroll::defaultScrollSettings();
    FileFingerprint wifiMeta{}, stationsMeta{};
    bool read = true;
    if (flags & ConfigPresent) {
      std::size_t size = sizeof(configBytes);
      read = nvs_get_blob(handle, kConfigKey, configBytes, &size) == ESP_OK &&
             size == sizeof(configBytes);
    }
    if (read && (flags & HardwarePresent)) read = getBlob(handle, kHardwareKey, hardwareConfig);
    if (read && (flags & UiPresent)) read = getBlob(handle, kUiKey, ui);
    if (read && (flags & ScrollPresent)) {
      std::size_t size = sizeof(scrollBytes);
      read = nvs_get_blob(handle, kScrollKey, scrollBytes, &size) == ESP_OK &&
             size == sizeof(scrollBytes) &&
             display_scroll::deserializeScrollSettingsRecord(
                 scrollBytes, sizeof(scrollBytes), scrollSettings) ==
                 display_scroll::ScrollSettingsRecordStatus::Valid;
    }
    if (read && (flags & WifiPresent))
      read = getBlob(handle, kWifiMetaKey, wifiMeta);
    if (read && (flags & StationsPresent))
      read = getBlob(handle, kStationsMetaKey, stationsMeta);
    nvs_close(handle);
    if (!read || !verifyConfigPresence(flags & ConfigPresent, configBytes) ||
        !verifyHardwarePresence(flags & HardwarePresent, hardwareConfig) ||
        !verifyUiPresence(flags & UiPresent, ui) ||
        !verifyScrollState(
            !(flags & ScrollCaptured)
                ? config_backup::ScrollBackupMode::PreserveExisting
            : flags & ScrollPresent
                ? config_backup::ScrollBackupMode::UserSettings
                : config_backup::ScrollBackupMode::RendererDefaults,
            scrollSettings)) return false;
    const bool wifi = flags & WifiPresent
        ? fileMatches(SSIDS_PATH, wifiMeta) : !SPIFFS.exists(SSIDS_PATH);
    const bool stations = flags & StationsPresent
        ? fileMatches(PLAYLIST_PATH, stationsMeta) : !SPIFFS.exists(PLAYLIST_PATH);
    return wifi && stations;
  }

  bool clearMarker() override {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t erased = nvs_erase_key(handle, kMarkerKey);
    const bool cleared = erased == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    Phase ignored = Phase::Prepared;
    return cleared && readMarker(ignored) == MarkerStatus::None;
  }

 private:
  const config_backup::Data* candidate_ = nullptr;
  bool stagedFiles_ = false;
  FileFingerprint candidateWifi_{};
  FileFingerprint candidateStations_{};

  bool writeConfig(const config_format::config_v7_t& configValue) {
    uint8_t bytes[config_format::kConfigV7SerializedSize]{};
    if (!config_format::serializeConfigV7(configValue, bytes, sizeof(bytes))) return false;
    config_storage::ConfigEepromStorage storage;
    return storage.writeRecord(bytes, sizeof(bytes)) && storage.commit() &&
           verifyConfig(configValue);
  }

  bool verifyConfig(const config_format::config_v7_t& expected) {
    uint8_t expectedBytes[config_format::kConfigV7SerializedSize]{};
    uint8_t actual[config_format::kConfigV7SerializedSize]{};
    config_storage::ConfigEepromStorage storage;
    return config_format::serializeConfigV7(expected, expectedBytes, sizeof(expectedBytes)) &&
           storage.readRecord(actual, sizeof(actual)) &&
           std::memcmp(expectedBytes, actual, sizeof(actual)) == 0;
  }

  bool verifyStoredSnapshot(uint8_t flags, const uint8_t* configBytes,
                            const hardware::RuntimeHardwareConfig& hardwareConfig,
                            const UiTimeoutSettings& ui,
                            const uint8_t* scrollBytes,
                            const FileFingerprint& wifiMeta,
                            const FileFingerprint& stationsMeta) {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
    uint8_t actualFlags = 0;
    bool ok = nvs_get_u8(handle, kFlagsKey, &actualFlags) == ESP_OK &&
              actualFlags == flags;
    uint8_t actualConfig[config_format::kConfigV7SerializedSize]{};
    if (ok && (flags & ConfigPresent)) {
      std::size_t size = sizeof(actualConfig);
      ok = nvs_get_blob(handle, kConfigKey, actualConfig, &size) == ESP_OK &&
           size == sizeof(actualConfig) &&
           std::memcmp(actualConfig, configBytes, sizeof(actualConfig)) == 0;
    }
    hardware::RuntimeHardwareConfig actualHardware{};
    UiTimeoutSettings actualUi{};
    if (ok && (flags & HardwarePresent))
      ok = getBlob(handle, kHardwareKey, actualHardware) && actualHardware == hardwareConfig;
    if (ok && (flags & UiPresent))
      ok = getBlob(handle, kUiKey, actualUi) &&
           std::memcmp(&actualUi, &ui, sizeof(ui)) == 0;
    uint8_t actualScroll[display_scroll::kScrollSettingsRecordSize]{};
    if (ok && (flags & ScrollPresent)) {
      std::size_t size = sizeof(actualScroll);
      display_scroll::ScrollSettings checked =
          display_scroll::defaultScrollSettings();
      ok = scrollBytes &&
           nvs_get_blob(handle, kScrollKey, actualScroll, &size) == ESP_OK &&
           size == sizeof(actualScroll) &&
           std::memcmp(actualScroll, scrollBytes, sizeof(actualScroll)) == 0 &&
           display_scroll::deserializeScrollSettingsRecord(
               actualScroll, sizeof(actualScroll), checked) ==
               display_scroll::ScrollSettingsRecordStatus::Valid;
    }
    FileFingerprint actualWifi{}, actualStations{};
    if (ok && (flags & WifiPresent))
      ok = getBlob(handle, kWifiMetaKey, actualWifi) &&
           std::memcmp(&actualWifi, &wifiMeta, sizeof(wifiMeta)) == 0;
    if (ok && (flags & StationsPresent))
      ok = getBlob(handle, kStationsMetaKey, actualStations) &&
           std::memcmp(&actualStations, &stationsMeta, sizeof(stationsMeta)) == 0;
    nvs_close(handle);
    if (!ok) return false;
    return (!(flags & WifiPresent) || fileMatches(SSIDS_PATH, wifiMeta)) &&
           (!(flags & StationsPresent) || fileMatches(PLAYLIST_PATH, stationsMeta));
  }

  bool restoreTransactionalFile(bool present, const char* rollback,
                                const char* active,
                                const FileFingerprint& expected) {
    if (!present) return removeIfPresent(active);
    if (SPIFFS.exists(rollback)) {
      // The durable marker makes the rollback file authoritative. Removing a
      // partial NEW file and renaming OLD is retryable after a power cut and
      // does not require a third full-size SPIFFS copy.
      if (!removeIfPresent(active) || !SPIFFS.rename(rollback, active))
        return false;
    }
    return fileMatches(active, expected);
  }

  bool verifyConfigPresence(bool present, const uint8_t* expected) {
    uint8_t actual[config_format::kConfigV7SerializedSize]{};
    bool found = false;
    config_storage::ConfigEepromStorage storage;
    return storage.readRecord(actual, sizeof(actual), found) && found == present &&
           (!present || std::memcmp(actual, expected, sizeof(actual)) == 0);
  }

  bool verifyHardwarePresence(bool present,
                              const hardware::RuntimeHardwareConfig& expected) {
    hardware::RuntimeHardwareConfig actual{};
    bool found = false;
    return hardware::readRuntimeHardwarePersistent(actual, found) && found == present &&
           (!present || actual == expected);
  }

  bool verifyUiPresence(bool present, const UiTimeoutSettings& expected) {
    UiTimeoutSettings actual{};
    bool found = false;
    return uiTimeoutReadPersistent(actual, found) && found == present &&
           (!present || std::memcmp(&actual, &expected, sizeof(actual)) == 0);
  }
};

}  // namespace

BootRecoveryInspection inspectBootRecovery() {
  Phase phase = Phase::Prepared;
  const MarkerStatus marker = readMarkerRecord(phase);
  if (marker == MarkerStatus::None) return BootRecoveryInspection::NotPending;
  if (marker == MarkerStatus::Pending) return BootRecoveryInspection::Pending;
  return BootRecoveryInspection::Error;
}

BootRecoveryStatus recoverAtBoot() {
  Phase phase = Phase::Prepared;
  const MarkerStatus marker = readMarkerRecord(phase);
  if (marker == MarkerStatus::None) return BootRecoveryStatus::NotPending;
  if (marker == MarkerStatus::Error || !config_storage::beginEeprom() ||
      !SPIFFS.begin(false)) return BootRecoveryStatus::Failed;
  ProductionBackend backend;
  TransactionStorage transaction(backend);
  const bool recovered = transaction.recover();
  SPIFFS.end();
  config_storage::endEeprom();
  return recovered ? BootRecoveryStatus::Recovered : BootRecoveryStatus::Failed;
}

void cleanupStaleAfterStorageInit() {
  Phase phase = Phase::Prepared;
  if (readMarkerRecord(phase) != MarkerStatus::None) return;
  ProductionBackend backend;
  backend.cleanupStale();
}

struct ProductionBackupStream::Impl {
  config_backup::Data data{};
  File wifi;
  File stations;
  PlaylistGuard playlistGuard;
  FileByteReader wifiReader;
  FileByteReader stationsReader;
  config_backup::StreamEncoder encoder;
  BackupStage stage = BackupStage::None;
  bool wifiReadFailed = false;
  bool stationsReadFailed = false;
  bool streamFailureLogged = false;
};

const char* backupStageName(BackupStage stage) {
  switch (stage) {
    case BackupStage::None: return "BACKUP_NONE";
    case BackupStage::Allocation: return "BACKUP_ALLOC";
    case BackupStage::StationLock: return "BACKUP_STATION_LOCK";
    case BackupStage::ConfigRead: return "BACKUP_CONFIG_READ";
    case BackupStage::HardwareRead: return "BACKUP_HARDWARE_READ";
    case BackupStage::UiRead: return "BACKUP_UI_READ";
    case BackupStage::ScrollRead: return "BACKUP_SCROLL_READ";
    case BackupStage::StationsMissing: return "BACKUP_STATIONS_MISSING";
    case BackupStage::WifiOpen: return "BACKUP_WIFI_READ";
    case BackupStage::StationsOpen: return "BACKUP_STATIONS_READ";
    case BackupStage::Validate: return "BACKUP_VALIDATE";
    case BackupStage::Serialize: return "BACKUP_SERIALIZE";
    case BackupStage::StreamWifi: return "BACKUP_STREAM_WIFI";
    case BackupStage::StreamStations: return "BACKUP_STREAM_STATIONS";
    case BackupStage::Ready: return "BACKUP_READY";
  }
  return "BACKUP_UNKNOWN";
}

ProductionBackupStream::ProductionBackupStream()
    : impl_(new (std::nothrow) Impl) {
  static_assert(sizeof(Impl) <= 2048,
                "backup download transport must remain a small allocation");
}
ProductionBackupStream::~ProductionBackupStream() {
  if (impl_) {
    impl_->wifi.close();
    impl_->stations.close();
    delete impl_;
  }
}

bool ProductionBackupStream::begin(config_backup::Status& status) {
  status = config_backup::Status::StorageReadFailed;
  if (!impl_) { status = config_backup::Status::AllocationFailed; return false; }
  impl_->stage = BackupStage::Allocation;
  if (!impl_->playlistGuard) {
    impl_->stage = BackupStage::StationLock;
    return false;
  }
  config_backup::Data& data = impl_->data;
  uint8_t configBytes[config_format::kConfigV7SerializedSize]{};
  bool found = false;
  config_storage::ConfigEepromStorage configStore;
  impl_->stage = BackupStage::ConfigRead;
  if (!configStore.readRecord(configBytes, sizeof(configBytes), found) || !found ||
      config_format::parseConfigV7(configBytes, sizeof(configBytes), data.config) !=
          config_format::ConfigRecordStatus::LOADED_V7)
    return false;

  impl_->stage = BackupStage::HardwareRead;
  if (!hardware::readRuntimeHardwarePersistent(data.hardware, found))
    return false;
  if (!found) data.hardware = hardware::runtimeHardwareConfig();
  impl_->stage = BackupStage::UiRead;
  if (!uiTimeoutReadPersistent(data.ui, found))
    return false;
  if (!found) data.ui = uiTimeoutConfig();
  impl_->stage = BackupStage::ScrollRead;
  const display_scroll::ScrollSettingsLoadResult scroll =
      display_scroll::readScrollSettingsPersistent();
  if (scroll.status ==
      display_scroll::ScrollSettingsLoadStatus::RendererDefaultsMissing) {
    data.scrollMode = config_backup::ScrollBackupMode::RendererDefaults;
    data.scrollSettings = display_scroll::defaultScrollSettings();
  } else if (scroll.status ==
                 display_scroll::ScrollSettingsLoadStatus::UserSettings &&
             scroll.source ==
                 display_scroll::ScrollSettingsSource::UserSettings &&
             scroll.recordStatus ==
                 display_scroll::ScrollSettingsRecordStatus::Valid) {
    data.scrollMode = config_backup::ScrollBackupMode::UserSettings;
    data.scrollSettings = scroll.settings;
  } else {
    if (scroll.status ==
        display_scroll::ScrollSettingsLoadStatus::RendererDefaultsInvalid)
      status = config_backup::Status::InvalidScroll;
    return false;
  }

  if (!SPIFFS.exists(PLAYLIST_PATH)) {
    impl_->stage = BackupStage::StationsMissing;
    return false;
  }
  impl_->wifi = SPIFFS.open(SSIDS_PATH, "r");
  if (SPIFFS.exists(SSIDS_PATH) && !impl_->wifi) {
    impl_->stage = BackupStage::WifiOpen;
    return false;
  }
  impl_->stations = SPIFFS.open(PLAYLIST_PATH, "r");
  const std::size_t wifiSize = impl_->wifi ? impl_->wifi.size() : 0;
  const std::size_t stationsSize = impl_->stations ? impl_->stations.size() : 0;
  if (!impl_->stations) {
    impl_->stage = BackupStage::StationsOpen;
    return false;
  }
  data.wifi = {nullptr, wifiSize};
  data.stations = {nullptr, stationsSize};
  impl_->wifiReader.reset(impl_->wifi, wifiSize, &impl_->wifiReadFailed);
  impl_->stationsReader.reset(impl_->stations, stationsSize,
                              &impl_->stationsReadFailed);
  impl_->stage = BackupStage::Validate;
  status = impl_->encoder.begin(data, hardware::hardwareCapabilities(),
                                impl_->wifiReader, impl_->stationsReader);
  if (status != config_backup::Status::Ok) {
    if (impl_->wifiReadFailed) impl_->stage = BackupStage::StreamWifi;
    else if (impl_->stationsReadFailed)
      impl_->stage = BackupStage::StreamStations;
    else if (status != config_backup::Status::InvalidConfig &&
             status != config_backup::Status::InvalidHardware &&
             status != config_backup::Status::UnsupportedHardware &&
             status != config_backup::Status::InvalidUi &&
             status != config_backup::Status::InvalidScroll &&
             status != config_backup::Status::InvalidWifi &&
             status != config_backup::Status::InvalidStations)
      impl_->stage = BackupStage::Serialize;
    return false;
  }
  impl_->stage = BackupStage::Ready;
  return true;
}

std::size_t ProductionBackupStream::size() const {
  return impl_ ? impl_->encoder.size() : 0;
}

std::size_t ProductionBackupStream::read(std::size_t offset, uint8_t* output,
                                         std::size_t capacity) {
  if (!impl_) return 0;
  impl_->wifiReadFailed = false;
  impl_->stationsReadFailed = false;
  const std::size_t copied = impl_->encoder.read(offset, output, capacity);
  if (!copied && impl_->encoder.status() == config_backup::Status::StorageReadFailed) {
    impl_->stage = impl_->wifiReadFailed ? BackupStage::StreamWifi
                                        : BackupStage::StreamStations;
    if (!impl_->streamFailureLogged) {
      Serial.printf("##[CONFIG-BACKUP]# failed stage=%s status=%s\n",
                    backupStageName(impl_->stage),
                    config_backup::statusName(impl_->encoder.status()));
      impl_->streamFailureLogged = true;
    }
  }
  return copied;
}

BackupStage ProductionBackupStream::stage() const {
  return impl_ ? impl_->stage : BackupStage::Allocation;
}

std::size_t ProductionBackupStream::wifiSize() const {
  return impl_ ? impl_->data.wifi.size : 0;
}

std::size_t ProductionBackupStream::stationsSize() const {
  return impl_ ? impl_->data.stations.size : 0;
}

uint32_t ProductionBackupStream::checksum() const {
  return impl_ ? impl_->encoder.checksum() : 0;
}

struct ProductionRestoreUpload::Impl {
  config_backup::Data candidate{};
  config_backup::Crc32 crc;
  File wifi;
  File stations;
  uint8_t prefix[config_backup::kBackupVariableOffset]{};
  uint8_t checksum[4]{};
  std::size_t prefixExpected = 6;
  std::size_t total = 0;
  std::size_t received = 0;
  config_backup::Status status = config_backup::Status::InvalidArgument;
  bool decoded = false;
  bool finished = false;
};

ProductionRestoreUpload::ProductionRestoreUpload()
    : impl_(new (std::nothrow) Impl) {
  static_assert(sizeof(Impl) <= 2048,
                "restore upload transport must remain a small allocation");
}

ProductionRestoreUpload::~ProductionRestoreUpload() {
  abort();
  delete impl_;
}

config_backup::Status ProductionRestoreUpload::begin(std::size_t total) {
  using config_backup::Status;
  if (!impl_) return Status::AllocationFailed;
  if (total < config_backup::kLegacyBackupFixedOverhead || total > UINT32_MAX)
    return impl_->status = total ? Status::Truncated : Status::Empty;
  Phase phase = Phase::Prepared;
  const MarkerStatus marker = readMarkerRecord(phase);
  if (marker != MarkerStatus::None)
    return impl_->status = Status::RecoveryPending;
  if (!removeIfPresent(kWifiNew) || !removeIfPresent(kStationsNew))
    return impl_->status = Status::StorageWriteFailed;
  impl_->total = total;
  impl_->status = Status::Ok;
  return Status::Ok;
}

config_backup::Status ProductionRestoreUpload::append(
    std::size_t index, const uint8_t* data, std::size_t length) {
  using config_backup::Status;
  if (!impl_ || impl_->status != Status::Ok || impl_->finished)
    return impl_ ? impl_->status : Status::AllocationFailed;
  if ((length && !data) || index != impl_->received ||
      length > impl_->total - impl_->received)
    return impl_->status = Status::InvalidLength;

  const std::size_t checksumOffset = impl_->total - sizeof(impl_->checksum);
  if (index < checksumOffset) {
    const std::size_t crcLength =
        std::min(length, checksumOffset - index);
    impl_->crc.update(data, crcLength);
  }

  while (length) {
    const std::size_t position = impl_->received;
    if (!impl_->decoded && position < impl_->prefixExpected) {
      const std::size_t count =
          std::min(length, impl_->prefixExpected - position);
      std::memcpy(impl_->prefix + position, data, count);
      impl_->received += count;
      data += count;
      length -= count;
      if (impl_->received == 6 && impl_->prefixExpected == 6) {
        const uint16_t schema =
            static_cast<uint16_t>(impl_->prefix[4]) |
            (static_cast<uint16_t>(impl_->prefix[5]) << 8);
        impl_->prefixExpected =
            config_backup::backupVariableOffsetForSchema(schema);
        const std::size_t fixed =
            config_backup::backupFixedOverheadForSchema(schema);
        if (!impl_->prefixExpected)
          return impl_->status = Status::WrongSchema;
        if (impl_->total < fixed)
          return impl_->status = Status::Truncated;
      }
      if (impl_->received == impl_->prefixExpected) {
        impl_->status = config_backup::decodePrefix(
            impl_->prefix, impl_->prefixExpected, impl_->total,
            hardware::hardwareCapabilities(), impl_->candidate);
        if (impl_->status != Status::Ok) return impl_->status;

        // SPIFFS has no cross-file transaction. Both candidate files must fit
        // beside the active generation; one erase block is reserved for FS
        // metadata and rename bookkeeping.
        const std::size_t stagedBytes = impl_->candidate.wifi.size +
                                        impl_->candidate.stations.size;
        const std::size_t freeBytes = SPIFFS.totalBytes() - SPIFFS.usedBytes();
        if (stagedBytes > freeBytes ||
            freeBytes - stagedBytes <
                config_backup_web::kRestoreStagingReserveBytes)
          return impl_->status = Status::StorageWriteFailed;
        impl_->wifi = SPIFFS.open(kWifiNew, "w");
        impl_->stations = SPIFFS.open(kStationsNew, "w");
        if (!impl_->wifi || !impl_->stations)
          return impl_->status = Status::StorageWriteFailed;
        impl_->decoded = true;
      }
      continue;
    }

    const std::size_t wifiEnd = impl_->prefixExpected +
                                impl_->candidate.wifi.size;
    const std::size_t stationsEnd = wifiEnd + impl_->candidate.stations.size;
    if (position < wifiEnd) {
      const std::size_t count = std::min(length, wifiEnd - position);
      if (impl_->wifi.write(data, count) != count)
        return impl_->status = Status::StorageWriteFailed;
      impl_->received += count;
      data += count;
      length -= count;
      continue;
    }
    if (position < stationsEnd) {
      const std::size_t count = std::min(length, stationsEnd - position);
      if (impl_->stations.write(data, count) != count)
        return impl_->status = Status::StorageWriteFailed;
      impl_->received += count;
      data += count;
      length -= count;
      continue;
    }
    if (position < impl_->total) {
      const std::size_t checksumIndex = position - stationsEnd;
      const std::size_t count =
          std::min(length, sizeof(impl_->checksum) - checksumIndex);
      std::memcpy(impl_->checksum + checksumIndex, data, count);
      impl_->received += count;
      data += count;
      length -= count;
      continue;
    }
    return impl_->status = Status::InvalidLength;
  }
  return Status::Ok;
}

config_backup::RestoreResult ProductionRestoreUpload::finish() {
  using config_backup::Status;
  if (!impl_) return {Status::AllocationFailed, false};
  if (impl_->status != Status::Ok) return {impl_->status, false};
  if (!impl_->decoded || impl_->received != impl_->total)
    return {Status::Truncated, false};
  impl_->wifi.flush();
  impl_->stations.flush();
  impl_->wifi.close();
  impl_->stations.close();
  if (get32(impl_->checksum) != impl_->crc.value()) {
    impl_->status = Status::InvalidChecksum;
    abort();
    return {Status::InvalidChecksum, false};
  }

  impl_->wifi = SPIFFS.open(kWifiNew, "r");
  impl_->stations = SPIFFS.open(kStationsNew, "r");
  if (!impl_->wifi || !impl_->stations) {
    impl_->status = Status::StorageReadFailed;
    abort();
    return {impl_->status, false};
  }
  FileByteReader wifiReader(impl_->wifi, impl_->candidate.wifi.size);
  FileByteReader stationsReader(impl_->stations, impl_->candidate.stations.size);
  impl_->status = config_backup::validateExternal(
      impl_->candidate, hardware::hardwareCapabilities(), wifiReader,
      stationsReader);
  impl_->wifi.close();
  impl_->stations.close();
  if (impl_->status != Status::Ok) {
    abort();
    return {impl_->status, false};
  }

  ProductionBackend backend(true);
  TransactionStorage transaction(backend);
  const config_backup::RestoreResult result =
      config_backup::restoreValidated(impl_->candidate, transaction);
  impl_->status = result.status;
  impl_->finished = true;
  return result;
}

void ProductionRestoreUpload::abort() {
  if (!impl_) return;
  impl_->wifi.close();
  impl_->stations.close();
  if (impl_->finished) return;
  Phase phase = Phase::Prepared;
  if (readMarkerRecord(phase) == MarkerStatus::None) {
    removeIfPresent(kWifiNew);
    removeIfPresent(kStationsNew);
  }
  impl_->finished = true;
}

bool ProductionRestoreUpload::complete() const {
  return impl_ && impl_->status == config_backup::Status::Ok &&
         impl_->received == impl_->total;
}

config_backup::RestoreResult restoreProductionBackup(const uint8_t* input,
                                                     std::size_t size) {
  ProductionBackend backend;
  TransactionStorage transaction(backend);
  return config_backup::restore(input, size, hardware::hardwareCapabilities(),
                                transaction);
}

}  // namespace config_restore
}  // namespace voxone
#endif  // ARDUINO
