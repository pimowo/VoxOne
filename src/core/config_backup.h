#ifndef VOXONE_CONFIG_BACKUP_H
#define VOXONE_CONFIG_BACKUP_H

#include <cstddef>
#include <cstdint>

#include "config_format.h"
#include "mqtt_config.h"
#include "ui_timeout_config.h"
#include "../hardware/runtime_hardware_config.h"

namespace voxone {
namespace config_backup {

constexpr uint16_t kBackupSchemaVersion = 1;
constexpr std::size_t kMqttWireSize = 388;
constexpr std::size_t kUiWireSize = 3;
constexpr std::size_t kBackupFixedOverhead =
    34 + config_format::kConfigV7SerializedSize +
    hardware::kRuntimeHardwareConfigSerializedSize + kMqttWireSize +
    kUiWireSize + 4;

struct Bytes {
  const uint8_t* data;
  std::size_t size;
};

struct Data {
  config_format::config_v7_t config;
  hardware::RuntimeHardwareConfig hardware;
  MqttSettings mqtt;
  UiTimeoutSettings ui;
  Bytes wifi;
  Bytes stations;
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
  InvalidWifi,
  InvalidStations,
  OutputTooSmall,
  StorageBeginFailed,
  StorageWriteFailed,
  StorageCommitFailed,
  RecoveryPending
};

struct RestoreResult {
  Status status;
  bool restartRequired;
};

std::size_t serializedSize(const Data& data);
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

}  // namespace config_backup
}  // namespace voxone

#endif
