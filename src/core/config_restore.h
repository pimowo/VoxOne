#ifndef VOXONE_CONFIG_RESTORE_H
#define VOXONE_CONFIG_RESTORE_H

// Consolidated from config_backup.h.
#include <cstddef>
#include <cstdint>

#include "config_format.h"
#include "display_scroll.h"
#include "ui_timeout_config.h"
#include "../hardware/runtime_hardware_config.h"

namespace voxone {
namespace config_backup {

constexpr uint16_t kLegacyBackupSchemaVersion = 1;
constexpr uint16_t kBackupSchemaVersion = 2;
// Schemas 1 and 2 carried a 388-byte integration record here. Keep the wire
// slot opaque so old VXCF backups remain parseable without restoring retired
// credentials or changing the frozen backup offsets.
constexpr std::size_t kRetiredIntegrationWireSize = 388;
constexpr std::size_t kUiWireSize = 3;
constexpr std::size_t kBackupHeaderSize = 34;
constexpr std::size_t kScrollWireSize =
    1 + display_scroll::kScrollSettingsRecordSize;
constexpr std::size_t kLegacyBackupFixedOverhead =
    kBackupHeaderSize + config_format::kConfigV7SerializedSize +
    hardware::kRuntimeHardwareConfigSerializedSize + kRetiredIntegrationWireSize +
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
  uint8_t retiredIntegration[kRetiredIntegrationWireSize]{};
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

// Consolidated from config_backup_web.h.
#include <cstddef>
#include <cstdint>


namespace voxone {
namespace config_backup_web {

constexpr char kBackupRoute[] = "/api/config/backup";
constexpr char kRestoreRoute[] = "/api/config/restore";
constexpr char kContentType[] = "application/octet-stream";
constexpr char kCacheControl[] = "no-store";
constexpr std::size_t kMaxStationsBytes = 192u * 1024u;
constexpr std::size_t kMaxWifiBytes = 5u * (29u + 1u + 39u + 2u);
constexpr std::size_t kMaxUploadBytes =
    config_backup::kBackupFixedOverhead + kMaxWifiBytes + kMaxStationsBytes;
constexpr std::size_t kRestoreStagingReserveBytes = 4096;

constexpr bool restoreStagingFits(std::size_t totalBytes,
                                  std::size_t usedBytes,
                                  std::size_t candidateBytes) {
  return usedBytes <= totalBytes &&
         candidateBytes <= totalBytes - usedBytes &&
         totalBytes - usedBytes - candidateBytes >=
             kRestoreStagingReserveBytes;
}

enum class UploadDecision : uint8_t {
  Accept,
  Complete,
  InvalidSize,
  InvalidOffset,
  AlreadyComplete
};

class UploadState {
 public:
  UploadDecision append(std::size_t index, std::size_t length,
                        std::size_t total);
  std::size_t size() const { return total_; }
  bool complete() const { return complete_; }

 private:
  std::size_t total_ = 0;
  std::size_t received_ = 0;
  bool started_ = false;
  bool complete_ = false;
};

struct ApiResult {
  uint16_t httpStatus;
  const char* error;
  bool restartRequired;
};

ApiResult mapRestoreResult(const config_backup::RestoreResult& result);

}  // namespace config_backup_web
}  // namespace voxone

// Consolidated from config_restore_transaction.h.
#include <cstdint>


namespace voxone {
namespace config_restore {

enum class Phase : uint8_t {
  Prepared = 1,
  ConfigWritten,
  HardwareWritten,
  RetiredIntegrationWritten,
  UiWritten,
  WifiWritten,
  StationsWritten,
  IndexRebuilt,
  Verified,
  // Appended to preserve the durable numeric values of marker schema 1.
  ScrollWritten
};

enum class Component : uint8_t {
  Config,
  Hardware,
  Ui,
  Scroll,
  Wifi,
  Stations
};

enum class Step : uint8_t { InProgress, Complete, Failed };
enum class MarkerStatus : uint8_t { None, Pending, Error };

class Backend {
 public:
  virtual ~Backend() {}
  virtual MarkerStatus readMarker(Phase& phase) = 0;
  virtual bool cleanupStale() = 0;
  virtual bool stageCandidate(const config_backup::Data& candidate) = 0;
  virtual bool createRollbackSnapshot() = 0;
  virtual bool writeMarker(Phase phase) = 0;
  virtual bool apply(Component component) = 0;
  virtual bool rebuildStationsIndex() = 0;
  virtual bool verifyCandidate() = 0;
  virtual bool restoreRollbackSnapshot() = 0;
  virtual bool verifyRollbackSnapshot() = 0;
  virtual bool clearMarker() = 0;
};

class TransactionStorage : public config_backup::RestoreStorage {
 public:
  explicit TransactionStorage(Backend& backend) : backend_(backend) {}

  bool begin() override;
  bool stage(const config_backup::Data& candidate) override;
  bool commit() override;
  bool rollback() override;

  Step commitStep();
  bool recover();
  bool rollbackSucceeded() const { return rollbackSucceeded_; }

 private:
  Backend& backend_;
  bool rollbackSucceeded_ = false;
};

}  // namespace config_restore
}  // namespace voxone

// Consolidated from config_restore_adapter.h.
#include <cstddef>
#include <cstdint>


namespace voxone {
namespace config_restore {

enum class BootRecoveryStatus : uint8_t { NotPending, Recovered, Failed };
enum class BootRecoveryInspection : uint8_t { NotPending, Pending, Error };

enum class BackupStage : uint8_t {
  None,
  Allocation,
  StationLock,
  ConfigRead,
  HardwareRead,
  UiRead,
  ScrollRead,
  StationsMissing,
  WifiOpen,
  StationsOpen,
  Validate,
  Serialize,
  StreamWifi,
  StreamStations,
  Ready
};

const char* backupStageName(BackupStage stage);

BootRecoveryStatus recoverAtBoot();
BootRecoveryInspection inspectBootRecovery();
void cleanupStaleAfterStorageInit();

class ProductionBackupStream {
 public:
  ProductionBackupStream();
  ~ProductionBackupStream();
  bool begin(config_backup::Status& status);
  std::size_t size() const;
  std::size_t read(std::size_t offset, uint8_t* output, std::size_t capacity);
  BackupStage stage() const;
  std::size_t wifiSize() const;
  std::size_t stationsSize() const;
  uint32_t checksum() const;

 private:
  struct Impl;
  Impl* impl_;
  ProductionBackupStream(const ProductionBackupStream&) = delete;
  ProductionBackupStream& operator=(const ProductionBackupStream&) = delete;
};

class ProductionRestoreUpload {
 public:
  ProductionRestoreUpload();
  ~ProductionRestoreUpload();
  config_backup::Status begin(std::size_t total);
  config_backup::Status append(std::size_t index, const uint8_t* data,
                               std::size_t length);
  config_backup::RestoreResult finish();
  void abort();
  bool complete() const;

 private:
  struct Impl;
  Impl* impl_;
  ProductionRestoreUpload(const ProductionRestoreUpload&) = delete;
  ProductionRestoreUpload& operator=(const ProductionRestoreUpload&) = delete;
};

// Internal production entry point. A later API layer may call this after it
// has received a complete VXCF artifact; this stage intentionally exposes no
// HTTP or WWW surface.
config_backup::RestoreResult restoreProductionBackup(const uint8_t* input,
                                                     std::size_t size);

}  // namespace config_restore
}  // namespace voxone

#endif  // VOXONE_CONFIG_RESTORE_H
