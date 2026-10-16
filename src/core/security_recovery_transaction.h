#ifndef VOXONE_SECURITY_RECOVERY_TRANSACTION_H
#define VOXONE_SECURITY_RECOVERY_TRANSACTION_H

#include "security_recovery_pin.h"
#include "security_settings.h"

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace security_recovery {

constexpr size_t kRecoveryJournalRecordSize = 120U;

class RecoveryTransactionStorage {
 public:
  virtual ~RecoveryTransactionStorage() {}
  virtual security::SecurityStorageReadStatus readSettings(
      uint8_t* output, size_t capacity, size_t& size) = 0;
  virtual bool writeSettings(const uint8_t* input, size_t size) = 0;
  virtual security::SecurityStorageReadStatus readJournal(
      uint8_t* output, size_t capacity, size_t& size) = 0;
  virtual bool writeJournal(const uint8_t* input, size_t size) = 0;
  virtual bool eraseJournal() = 0;
};

enum class RecoveryTransactionResult : uint8_t {
  Applied,
  AlreadyAttempted,
  InvalidCandidate,
  BaselineMissing,
  BaselineCorrupt,
  StorageError,
  JournalConflict,
  VerificationFailed,
  FinalizeFailed,
  Busy,
};

enum class RecoveryJournalResolution : uint8_t {
  Clean,
  Completed,
  Locked,
};

enum class RecoveryBaselineStatus : uint8_t {
  Eligible,
  Missing,
  Corrupt,
  StorageError,
  JournalConflict,
};

class RecoveryTransaction {
 public:
  explicit RecoveryTransaction(RecoveryTransactionStorage& storage)
      : storage_(storage) {}

  RecoveryTransactionResult commit(const PreparedRecoveryCredential& prepared);
  bool attempted() const { return attempted_; }

 private:
  RecoveryTransactionStorage& storage_;
  bool attempted_ = false;
};

RecoveryJournalResolution resolveRecoveryJournal(
    RecoveryTransactionStorage& storage);
RecoveryBaselineStatus inspectRecoveryBaseline(
    RecoveryTransactionStorage& storage);

// Production adapter. It uses voxsecurity/settings and a journal key in the
// same NVS namespace, acquires the shared OTA/update lock, and invalidates all
// administrator sessions only after a verified commit.
RecoveryTransactionResult commitDeviceSecurityRecovery(
    const PreparedRecoveryCredential& prepared);
RecoveryJournalResolution resolveDeviceSecurityRecoveryJournal();
RecoveryBaselineStatus inspectDeviceSecurityRecoveryBaseline();

const char* recoveryTransactionResultName(RecoveryTransactionResult result);
const char* recoveryJournalResolutionName(RecoveryJournalResolution result);
const char* recoveryBaselineStatusName(RecoveryBaselineStatus result);

}  // namespace security_recovery
}  // namespace voxone

#endif
