#ifndef VOXONE_FIRST_BOOT_TRANSACTION_H
#define VOXONE_FIRST_BOOT_TRANSACTION_H

#include <stddef.h>
#include <stdint.h>

#include "security_settings.h"
#include "wifi_config_parser.h"
#include "../hardware/runtime_hardware_config.h"

namespace voxone {
namespace first_boot_transaction {

constexpr size_t kJournalRecordSize = 106U;

enum class RecordStatus : uint8_t { Found, Missing, Error };

enum class Phase : uint8_t {
  Prepared = 1,
  HardwareWritten,
  WifiWritten,
  SecurityWritten,
};

enum class Result : uint8_t {
  Applied,
  SecurityUnavailable,
  InvalidCandidate,
  BaselineConflict,
  JournalReadFailed,
  JournalWriteFailed,
  HardwareWriteFailed,
  WifiWriteFailed,
  SecurityCompletionFailed,
  FinalizeFailed,
  RollbackFailed,
};

enum class RecoveryStatus : uint8_t { Clean, Recovered, Failed };

struct Candidate {
  bool writeHardware;
  uint8_t hardware[hardware::kRuntimeHardwareConfigSerializedSize];
  size_t hardwareSize;
  uint8_t wifi[wifi_config::kMaxRecordBytes];
  size_t wifiSize;
  uint8_t security[security::kSecurityRecordSize];
  size_t securitySize;
};

class Backend {
 public:
  virtual ~Backend() {}

  virtual RecordStatus readJournal(uint8_t* output, size_t capacity,
                                   size_t& size) = 0;
  virtual bool writeJournal(const uint8_t* input, size_t size) = 0;
  virtual bool eraseJournal() = 0;

  virtual RecordStatus readHardware(uint8_t* output, size_t capacity,
                                    size_t& size) = 0;
  virtual bool writeHardware(const uint8_t* input, size_t size) = 0;
  virtual bool eraseHardware() = 0;

  virtual RecordStatus readWifi(uint8_t* output, size_t capacity,
                                size_t& size) = 0;
  virtual bool writeWifi(const uint8_t* input, size_t size) = 0;
  virtual bool eraseWifi() = 0;
  virtual bool rollbackWifi(bool priorEmpty, size_t candidateSize,
                            uint32_t candidateCrc) = 0;

  virtual RecordStatus readSecurity(uint8_t* output, size_t capacity,
                                    size_t& size) = 0;
  // The production implementation must use SecurityService rather than
  // bypassing its state machine. The expected record is supplied for strict
  // post-write verification and deterministic native tests.
  virtual bool completeSecurityOpen(const uint8_t* expected, size_t size) = 0;
  virtual bool eraseSecurity() = 0;
};

class Transaction {
 public:
  explicit Transaction(Backend& backend) : backend_(backend) {}

  Result commit(const Candidate& candidate, bool securityLockHeld);
  RecoveryStatus recover();
  Result failureCause() const { return failureCause_; }

 private:
  Backend& backend_;
  Result failureCause_ = Result::Applied;
};

const char* resultName(Result result);
const char* recoveryStatusName(RecoveryStatus status);

}  // namespace first_boot_transaction
}  // namespace voxone

#endif
