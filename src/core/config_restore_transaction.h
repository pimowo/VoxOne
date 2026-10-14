#ifndef VOXONE_CONFIG_RESTORE_TRANSACTION_H
#define VOXONE_CONFIG_RESTORE_TRANSACTION_H

#include <cstdint>

#include "config_backup.h"

namespace voxone {
namespace config_restore {

enum class Phase : uint8_t {
  Prepared = 1,
  ConfigWritten,
  HardwareWritten,
  MqttWritten,
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
  Mqtt,
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

#endif
