#include "config_backup.h"

#include <cstring>
#include <limits>

#include "station_format.h"

namespace voxone {
namespace config_backup {
namespace {

constexpr uint8_t kMagic[4] = {'V', 'X', 'C', 'F'};
constexpr std::size_t kHeaderSize = 34;
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

void writePadded(uint8_t*& cursor, const char* text, std::size_t capacity) {
  const std::size_t length = std::strlen(text);
  std::memset(cursor, 0, capacity);
  std::memcpy(cursor, text, length);
  cursor += capacity;
}

void encodeMqtt(const MqttSettings& value, uint8_t* output) {
  uint8_t* cursor = output;
  *cursor++ = 1;
  *cursor++ = value.enabled;
  write16(cursor, value.port);
  writePadded(cursor, value.host, sizeof(value.host));
  writePadded(cursor, value.username, sizeof(value.username));
  writePadded(cursor, value.password, sizeof(value.password));
  writePadded(cursor, value.rootTopic, sizeof(value.rootTopic));
}

bool decodeMqtt(const uint8_t* input, MqttSettings& output) {
  if (input[0] != 1) return false;
  MqttSettings decoded{};
  decoded.enabled = input[1];
  const uint8_t* cursor = input + 2;
  decoded.port = read16(cursor);
  std::memcpy(decoded.host, cursor, sizeof(decoded.host)); cursor += sizeof(decoded.host);
  std::memcpy(decoded.username, cursor, sizeof(decoded.username)); cursor += sizeof(decoded.username);
  std::memcpy(decoded.password, cursor, sizeof(decoded.password)); cursor += sizeof(decoded.password);
  std::memcpy(decoded.rootTopic, cursor, sizeof(decoded.rootTopic));
  if (!mqttValidSettings(decoded)) return false;
  output = decoded;
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
  if (!mqttValidSettings(data.mqtt)) return Status::InvalidMqtt;
  if (!validUi(data.ui)) return Status::InvalidUi;
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
      data.config.fields.countStation != stationRows ||
      data.config.fields.lastStation > stationRows) return Status::InvalidConfig;
  return Status::Ok;
}

std::size_t serializedSize(const Data& data) {
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
  const std::size_t total = serializedSize(data);
  if (!total || total > std::numeric_limits<uint32_t>::max())
    return Status::InvalidLength;
  uint8_t* cursor = output;
  std::memcpy(cursor, kMagic, sizeof(kMagic)); cursor += sizeof(kMagic);
  write16(cursor, kBackupSchemaVersion);
  write32(cursor, static_cast<uint32_t>(total));
  write32(cursor, config_format::kConfigV7SerializedSize);
  write32(cursor, hardware::kRuntimeHardwareConfigSerializedSize);
  write32(cursor, kMqttWireSize);
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
  encodeMqtt(data.mqtt, cursor); cursor += kMqttWireSize;
  *cursor++ = 1;
  *cursor++ = data.ui.stationListSeconds;
  *cursor++ = data.ui.btTransportSeconds;
  return static_cast<std::size_t>(cursor - output) == kBackupVariableOffset
             ? Status::Ok : Status::InvalidLength;
}

Status decodePrefix(const uint8_t* input, std::size_t size,
                    std::size_t artifactSize,
                    const hardware::HardwareCapabilities& capabilities,
                    Data& output) {
  if (!input) return Status::InvalidArgument;
  if (size < kBackupVariableOffset || artifactSize < kBackupFixedOverhead)
    return Status::Truncated;
  if (std::memcmp(input, kMagic, sizeof(kMagic)) != 0) return Status::WrongMagic;
  const uint8_t* cursor = input + sizeof(kMagic);
  if (read16(cursor) != kBackupSchemaVersion) return Status::WrongSchema;
  const uint32_t total = read32(cursor);
  const uint32_t configSize = read32(cursor);
  const uint32_t hardwareSize = read32(cursor);
  const uint32_t mqttSize = read32(cursor);
  const uint32_t uiSize = read32(cursor);
  const uint32_t wifiSize = read32(cursor);
  const uint32_t stationsSize = read32(cursor);
  if (total > artifactSize) return Status::Truncated;
  if (total < artifactSize || configSize != config_format::kConfigV7SerializedSize ||
      hardwareSize != hardware::kRuntimeHardwareConfigSerializedSize ||
      mqttSize != kMqttWireSize || uiSize != kUiWireSize) return Status::InvalidLength;
  const uint64_t expected = static_cast<uint64_t>(kBackupFixedOverhead) +
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
  if (!decodeMqtt(cursor, candidate.mqtt)) return Status::InvalidMqtt;
  cursor += mqttSize;
  if (cursor[0] != 1) return Status::InvalidUi;
  candidate.ui.stationListSeconds = cursor[1];
  candidate.ui.btTransportSeconds = cursor[2];
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
  if (size < kBackupFixedOverhead) return Status::Truncated;
  if (std::memcmp(input, kMagic, sizeof(kMagic)) != 0) return Status::WrongMagic;
  const uint8_t* header = input + sizeof(kMagic);
  if (read16(header) != kBackupSchemaVersion) return Status::WrongSchema;
  const uint32_t total = read32(header);
  const uint32_t configSize = read32(header);
  const uint32_t hardwareSize = read32(header);
  const uint32_t mqttSize = read32(header);
  const uint32_t uiSize = read32(header);
  const uint32_t wifiSize = read32(header);
  const uint32_t stationsSize = read32(header);
  if (total > size) return Status::Truncated;
  if (total < size || configSize != config_format::kConfigV7SerializedSize ||
      hardwareSize != hardware::kRuntimeHardwareConfigSerializedSize ||
      mqttSize != kMqttWireSize || uiSize != kUiWireSize) return Status::InvalidLength;
  const uint64_t expected = static_cast<uint64_t>(kBackupFixedOverhead) +
                            wifiSize + stationsSize;
  if (expected != size) return expected > size ? Status::Truncated : Status::InvalidLength;
  const uint8_t* checksum = input + size - kChecksumSize;
  const uint8_t* checksumCursor = checksum;
  Crc32 calculated;
  calculated.update(input, size - kChecksumSize);
  if (read32(checksumCursor) != calculated.value())
    return Status::InvalidChecksum;
  Data candidate{};
  const Status decoded = decodePrefix(input, size, size, capabilities, candidate);
  if (decoded != Status::Ok) return decoded;
  const uint8_t* cursor = input + kBackupVariableOffset;
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
