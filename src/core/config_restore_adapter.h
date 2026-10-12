#ifndef VOXONE_CONFIG_RESTORE_ADAPTER_H
#define VOXONE_CONFIG_RESTORE_ADAPTER_H

#include <cstddef>
#include <cstdint>

#include "config_backup.h"

namespace voxone {
namespace config_restore {

enum class BootRecoveryStatus : uint8_t { NotPending, Recovered, Failed };

enum class BackupStage : uint8_t {
  None,
  Allocation,
  StationLock,
  ConfigRead,
  HardwareRead,
  MqttRead,
  UiRead,
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

#endif
