#ifndef VOXONE_CONFIG_BACKUP_H
#define VOXONE_CONFIG_BACKUP_H

#include <cstddef>
#include <cstdint>

#include "config_format.h"
#include "display_scroll_persistence.h"
#include "mqtt_config.h"
#include "ui_timeout_config.h"
#include "../hardware/runtime_hardware_config.h"

namespace voxone {
namespace config_backup {

constexpr uint16_t kLegacyBackupSchemaVersion = 1;
constexpr uint16_t kBackupSchemaVersion = 2;
constexpr std::size_t kMqttWireSize = 388;
constexpr std::size_t kUiWireSize = 3;
constexpr std::size_t kBackupHeaderSize = 34;
constexpr std::size_t kScrollWireSize =
    1 + display_scroll::kScrollSettingsRecordSize;
constexpr std::size_t kLegacyBackupFixedOverhead =
    kBackupHeaderSize + config_format::kConfigV7SerializedSize +
    hardware::kRuntimeHardwareConfigSerializedSize + kMqttWireSize +
    kUiWireSize + 4;
constexpr std::size_t kLegacyBackupVariableOffset =
    kLegacyBackupFixedOverhead - 4;
constexpr std::size_t kBackupFixedOverhead =
    kLegacyBackupFixedOverhead + kScrollWireSize;
constexpr std::size_t kBackupVariableOffset = kBackupFixedOverhead - 4;

constexpr std::size_t backupVariableOffsetForSchema(uint16_t schema) {
  return schema == kLegacyBackupSchemaVersion
             ? kLegacyBackupVariableOffset
         : schema == kBackupSchemaVersion ? kBackupVariableOffset : 0;
}

constexpr std::size_t backupFixedOverheadForSchema(uint16_t schema) {
  return schema == kLegacyBackupSchemaVersion
             ? kLegacyBackupFixedOverhead
         : schema == kBackupSchemaVersion ? kBackupFixedOverhead : 0;
}

enum class ScrollBackupMode : uint8_t {
  PreserveExisting,
  RendererDefaults,
  UserSettings
};

struct Bytes {
  const uint8_t* data;
  std::size_t size;
};

struct Data {
  config_format::config_v7_t config;
  hardware::RuntimeHardwareConfig hardware;
  MqttSettings mqtt;
  UiTimeoutSettings ui;
  ScrollBackupMode scrollMode = ScrollBackupMode::RendererDefaults;
  display_scroll::ScrollSettings scrollSettings =
      display_scroll::defaultScrollSettings();
  Bytes wifi;
  Bytes stations;
};

class ByteReader {
 public:
  virtual ~ByteReader() {}
  virtual std::size_t size() const = 0;
  virtual bool read(std::size_t offset, uint8_t* output,
                    std::size_t length) = 0;
};

class Crc32 {
 public:
  Crc32() : value_(0xffffffffu) {}
  void update(const uint8_t* data, std::size_t size);
  uint32_t value() const { return value_ ^ 0xffffffffu; }

 private:
  uint32_t value_;
};

enum class Status : uint8_t {
  Ok,
  InvalidArgument,
  Empty,
  Truncated,
  WrongMagic,
  WrongSchema,
  InvalidLength,
  InvalidChecksum,
  InvalidConfig,
  InvalidHardware,
  UnsupportedHardware,
  InvalidMqtt,
  InvalidUi,
  InvalidScroll,
  InvalidWifi,
  InvalidStations,
  OutputTooSmall,
  AllocationFailed,
  StorageReadFailed,
  StorageBeginFailed,
  StorageWriteFailed,
  StorageCommitFailed,
  RecoveryPending
};

const char* statusName(Status status);

class StreamEncoder {
 public:
  Status begin(const Data& data,
               const hardware::HardwareCapabilities& capabilities,
               ByteReader& wifi, ByteReader& stations);
  std::size_t size() const { return ready_ ? total_ : 0; }
  std::size_t read(std::size_t offset, uint8_t* output,
                   std::size_t capacity);
  Status status() const { return status_; }
  uint32_t checksum() const { return checksumValue_; }

 private:
  const Data* data_ = nullptr;
  ByteReader* wifi_ = nullptr;
  ByteReader* stations_ = nullptr;
  uint8_t prefix_[kBackupVariableOffset]{};
  uint8_t checksum_[4]{};
  std::size_t total_ = 0;
  uint32_t checksumValue_ = 0;
  Status status_ = Status::InvalidArgument;
  bool ready_ = false;
};

struct RestoreResult {
  Status status;
  bool restartRequired;
};

std::size_t serializedSize(const Data& data);
Status encodePrefix(const Data& data,
                    const hardware::HardwareCapabilities& capabilities,
                    uint8_t* output, std::size_t capacity);
Status decodePrefix(const uint8_t* input, std::size_t size,
                    std::size_t artifactSize,
                    const hardware::HardwareCapabilities& capabilities,
                    Data& output);
Status validateExternal(const Data& data,
                        const hardware::HardwareCapabilities& capabilities,
                        ByteReader& wifi, ByteReader& stations);
Status serialize(const Data& data,
                 const hardware::HardwareCapabilities& capabilities,
                 uint8_t* output, std::size_t capacity,
                 std::size_t& written);
Status parse(const uint8_t* input, std::size_t size,
             const hardware::HardwareCapabilities& capabilities,
             Data& output);

// The storage adapter owns staging and rollback across its physical records.
// Restart is requested only after its commit reports success.
class RestoreStorage {
 public:
  virtual ~RestoreStorage() {}
  virtual bool begin() = 0;
  virtual bool stage(const Data& candidate) = 0;
  virtual bool commit() = 0;
  virtual bool rollback() = 0;
};

RestoreResult restore(const uint8_t* input, std::size_t size,
                      const hardware::HardwareCapabilities& capabilities,
                      RestoreStorage& storage);
RestoreResult restoreValidated(const Data& candidate, RestoreStorage& storage);

}  // namespace config_backup
}  // namespace voxone

#endif
