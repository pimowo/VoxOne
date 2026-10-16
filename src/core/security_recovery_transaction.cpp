#include "security_recovery_transaction.h"

#include <cstring>

namespace voxone {
namespace security_recovery {
namespace {

constexpr uint32_t kJournalMagic = 0x4a525856U;  // "VXRJ" little-endian.
constexpr uint8_t kJournalVersion = 1U;
constexpr uint8_t kJournalPhasePrepared = 1U;
constexpr uint8_t kPriorPresent = 1U;
constexpr size_t kPriorHashOffset = 16U;
constexpr size_t kCandidateOffset = 48U;
constexpr size_t kJournalCrcOffset = 116U;

struct DecodedJournal {
  uint32_t priorGeneration;
  uint8_t priorHash[security::kSecurityVerifierSize];
  uint8_t candidate[security::kSecurityRecordSize];
};

void secureZero(void* memory, size_t size) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(memory);
  while (size--) *bytes++ = 0;
}

bool allZero(const uint8_t* bytes, size_t size) {
  uint8_t combined = 0;
  for (size_t index = 0; index < size; ++index) combined |= bytes[index];
  return combined == 0U;
}

uint32_t readLe32(const uint8_t* input) {
  return static_cast<uint32_t>(input[0]) |
         (static_cast<uint32_t>(input[1]) << 8U) |
         (static_cast<uint32_t>(input[2]) << 16U) |
         (static_cast<uint32_t>(input[3]) << 24U);
}

void writeLe32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8U);
  output[2] = static_cast<uint8_t>(value >> 16U);
  output[3] = static_cast<uint8_t>(value >> 24U);
}

bool equalBytes(const uint8_t* left, const uint8_t* right, size_t size) {
  uint8_t difference = 0;
  for (size_t index = 0; index < size; ++index)
    difference |= static_cast<uint8_t>(left[index] ^ right[index]);
  return difference == 0U;
}

bool validProtectedRecord(const uint8_t* record, size_t size,
                          security::SecuritySettings* decoded = nullptr) {
  security::SecuritySettings settings{};
  const bool valid = security::deserializeSecuritySettings(record, size,
                                                            settings) ==
                         security::SecurityDecodeStatus::Valid &&
                     settings.state == security::SecurityState::Protected;
  if (valid && decoded) *decoded = settings;
  secureZero(&settings, sizeof(settings));
  return valid;
}

bool serializeJournal(const uint8_t* priorRecord, uint32_t priorGeneration,
                      const uint8_t* candidate,
                      uint8_t output[kRecoveryJournalRecordSize]) {
  if (!priorRecord || !priorGeneration || !candidate || !output ||
      !validProtectedRecord(candidate, security::kSecurityRecordSize))
    return false;
  std::memset(output, 0, kRecoveryJournalRecordSize);
  writeLe32(output, kJournalMagic);
  output[4] = kJournalVersion;
  output[5] = kJournalPhasePrepared;
  output[6] = kPriorPresent;
  writeLe32(output + 8U, priorGeneration);
  if (!security::securitySha256(priorRecord, security::kSecurityRecordSize,
                                output + kPriorHashOffset)) {
    secureZero(output, kRecoveryJournalRecordSize);
    return false;
  }
  std::memcpy(output + kCandidateOffset, candidate,
              security::kSecurityRecordSize);
  writeLe32(output + kJournalCrcOffset,
            security::securityRecordCrc32(output, kJournalCrcOffset));
  return true;
}

bool deserializeJournal(const uint8_t* input, size_t size,
                        DecodedJournal& output) {
  secureZero(&output, sizeof(output));
  if (!input || size != kRecoveryJournalRecordSize ||
      readLe32(input) != kJournalMagic || input[4] != kJournalVersion ||
      input[5] != kJournalPhasePrepared || input[7] != 0U ||
      readLe32(input + kJournalCrcOffset) !=
          security::securityRecordCrc32(input, kJournalCrcOffset))
    return false;
  if (input[6] != kPriorPresent) return false;
  output.priorGeneration = readLe32(input + 8U);
  std::memcpy(output.priorHash, input + kPriorHashOffset,
              sizeof(output.priorHash));
  std::memcpy(output.candidate, input + kCandidateOffset,
              sizeof(output.candidate));
  if (!validProtectedRecord(output.candidate, sizeof(output.candidate))) {
    secureZero(&output, sizeof(output));
    return false;
  }
  security::SecuritySettings candidate{};
  (void)security::deserializeSecuritySettings(output.candidate,
                                               sizeof(output.candidate),
                                               candidate);
  const uint32_t expectedGeneration = output.priorGeneration + 1U;
  const uint32_t normalizedGeneration = expectedGeneration ? expectedGeneration
                                                            : 1U;
  const bool validPrior = output.priorGeneration != 0U &&
                          !allZero(output.priorHash,
                                   sizeof(output.priorHash));
  const bool valid = validPrior &&
                     candidate.generation == normalizedGeneration;
  secureZero(&candidate, sizeof(candidate));
  if (!valid) secureZero(&output, sizeof(output));
  return valid;
}

bool eraseJournalAndConfirm(RecoveryTransactionStorage& storage) {
  if (!storage.eraseJournal()) return false;
  uint8_t scratch[kRecoveryJournalRecordSize]{};
  size_t size = 0;
  const security::SecurityStorageReadStatus status =
      storage.readJournal(scratch, sizeof(scratch), size);
  secureZero(scratch, sizeof(scratch));
  return status == security::SecurityStorageReadStatus::Missing;
}

}  // namespace

RecoveryJournalResolution resolveRecoveryJournal(
    RecoveryTransactionStorage& storage) {
  uint8_t journalRecord[kRecoveryJournalRecordSize]{};
  size_t journalSize = 0;
  const security::SecurityStorageReadStatus journalStatus =
      storage.readJournal(journalRecord, sizeof(journalRecord), journalSize);
  if (journalStatus == security::SecurityStorageReadStatus::Missing) {
    secureZero(journalRecord, sizeof(journalRecord));
    return RecoveryJournalResolution::Clean;
  }
  if (journalStatus != security::SecurityStorageReadStatus::Found) {
    secureZero(journalRecord, sizeof(journalRecord));
    return RecoveryJournalResolution::Locked;
  }

  DecodedJournal journal{};
  if (!deserializeJournal(journalRecord, journalSize, journal)) {
    secureZero(journalRecord, sizeof(journalRecord));
    secureZero(&journal, sizeof(journal));
    return RecoveryJournalResolution::Locked;
  }
  secureZero(journalRecord, sizeof(journalRecord));

  uint8_t settingsRecord[security::kSecurityRecordSize]{};
  size_t settingsSize = 0;
  const security::SecurityStorageReadStatus settingsStatus =
      storage.readSettings(settingsRecord, sizeof(settingsRecord), settingsSize);
  if (settingsStatus == security::SecurityStorageReadStatus::Error) {
    secureZero(settingsRecord, sizeof(settingsRecord));
    secureZero(&journal, sizeof(journal));
    return RecoveryJournalResolution::Locked;
  }

  bool committed = false;
  bool original = false;
  if (settingsStatus == security::SecurityStorageReadStatus::Found &&
      settingsSize == sizeof(settingsRecord)) {
    committed = equalBytes(settingsRecord, journal.candidate,
                           sizeof(settingsRecord)) &&
                validProtectedRecord(settingsRecord, sizeof(settingsRecord));
    if (!committed) {
      security::SecuritySettings decoded{};
      uint8_t hash[security::kSecurityVerifierSize]{};
      original = security::deserializeSecuritySettings(
                     settingsRecord, sizeof(settingsRecord), decoded) ==
                     security::SecurityDecodeStatus::Valid &&
                 decoded.generation == journal.priorGeneration &&
                 security::securitySha256(settingsRecord, sizeof(settingsRecord),
                                          hash) &&
                 equalBytes(hash, journal.priorHash, sizeof(hash));
      secureZero(hash, sizeof(hash));
      secureZero(&decoded, sizeof(decoded));
    }
  }

  if (!committed && !original) {
    secureZero(settingsRecord, sizeof(settingsRecord));
    secureZero(&journal, sizeof(journal));
    return RecoveryJournalResolution::Locked;
  }

  if (original) {
    if (!storage.writeSettings(journal.candidate,
                               sizeof(journal.candidate))) {
      secureZero(settingsRecord, sizeof(settingsRecord));
      secureZero(&journal, sizeof(journal));
      return RecoveryJournalResolution::Locked;
    }
    size_t verifiedSize = 0;
    const security::SecurityStorageReadStatus verifiedStatus =
        storage.readSettings(settingsRecord, sizeof(settingsRecord),
                             verifiedSize);
    committed = verifiedStatus == security::SecurityStorageReadStatus::Found &&
                verifiedSize == sizeof(settingsRecord) &&
                equalBytes(settingsRecord, journal.candidate,
                           sizeof(settingsRecord)) &&
                validProtectedRecord(settingsRecord, verifiedSize);
    if (!committed) {
      secureZero(settingsRecord, sizeof(settingsRecord));
      secureZero(&journal, sizeof(journal));
      return RecoveryJournalResolution::Locked;
    }
  }

  secureZero(settingsRecord, sizeof(settingsRecord));
  secureZero(&journal, sizeof(journal));
  if (!eraseJournalAndConfirm(storage)) return RecoveryJournalResolution::Locked;
  return RecoveryJournalResolution::Completed;
}

RecoveryBaselineStatus inspectRecoveryBaseline(
    RecoveryTransactionStorage& storage) {
  uint8_t record[security::kSecurityRecordSize]{};
  size_t size = 0;
  const security::SecurityStorageReadStatus status =
      storage.readSettings(record, sizeof(record), size);
  if (status == security::SecurityStorageReadStatus::Missing) {
    secureZero(record, sizeof(record));
    return RecoveryBaselineStatus::Missing;
  }
  if (status != security::SecurityStorageReadStatus::Found) {
    secureZero(record, sizeof(record));
    return RecoveryBaselineStatus::StorageError;
  }
  security::SecuritySettings decoded{};
  const bool valid = security::deserializeSecuritySettings(
                         record, size, decoded) ==
                     security::SecurityDecodeStatus::Valid &&
                     (decoded.state == security::SecurityState::Protected ||
                      decoded.state == security::SecurityState::Open);
  secureZero(&decoded, sizeof(decoded));
  secureZero(record, sizeof(record));
  return valid ? RecoveryBaselineStatus::Eligible
               : RecoveryBaselineStatus::Corrupt;
}

RecoveryTransactionResult RecoveryTransaction::commit(
    const PreparedRecoveryCredential& prepared) {
  if (attempted_) return RecoveryTransactionResult::AlreadyAttempted;
  attempted_ = true;
  if (!prepared.valid ||
      prepared.iterations != security::kPbkdf2Iterations ||
      allZero(prepared.salt, sizeof(prepared.salt)) ||
      allZero(prepared.verifier, sizeof(prepared.verifier)))
    return RecoveryTransactionResult::InvalidCandidate;

  const RecoveryJournalResolution pending = resolveRecoveryJournal(storage_);
  if (pending == RecoveryJournalResolution::Locked)
    return RecoveryTransactionResult::JournalConflict;

  uint8_t priorRecord[security::kSecurityRecordSize]{};
  size_t priorSize = 0;
  const security::SecurityStorageReadStatus priorStatus =
      storage_.readSettings(priorRecord, sizeof(priorRecord), priorSize);
  if (priorStatus == security::SecurityStorageReadStatus::Error) {
    secureZero(priorRecord, sizeof(priorRecord));
    return RecoveryTransactionResult::StorageError;
  }

  security::SecuritySettings prior{};
  if (priorStatus == security::SecurityStorageReadStatus::Missing) {
    secureZero(priorRecord, sizeof(priorRecord));
    secureZero(&prior, sizeof(prior));
    return RecoveryTransactionResult::BaselineMissing;
  }
  if (security::deserializeSecuritySettings(priorRecord, priorSize, prior) !=
      security::SecurityDecodeStatus::Valid) {
    secureZero(priorRecord, sizeof(priorRecord));
    secureZero(&prior, sizeof(prior));
    return RecoveryTransactionResult::BaselineCorrupt;
  }

  security::SecuritySettings candidate{};
  candidate.state = security::SecurityState::Protected;
  candidate.generation = prior.generation + 1U;
  if (!candidate.generation) candidate.generation = 1U;
  candidate.iterations = prepared.iterations;
  std::memcpy(candidate.salt, prepared.salt, sizeof(candidate.salt));
  std::memcpy(candidate.verifier, prepared.verifier,
              sizeof(candidate.verifier));

  uint8_t candidateRecord[security::kSecurityRecordSize]{};
  uint8_t journalRecord[kRecoveryJournalRecordSize]{};
  const bool encoded = security::serializeSecuritySettings(
                           candidate, candidateRecord,
                           sizeof(candidateRecord)) &&
                       serializeJournal(priorRecord, prior.generation,
                                        candidateRecord,
                                        journalRecord);
  secureZero(&candidate, sizeof(candidate));
  secureZero(&prior, sizeof(prior));
  secureZero(priorRecord, sizeof(priorRecord));
  if (!encoded) {
    secureZero(candidateRecord, sizeof(candidateRecord));
    secureZero(journalRecord, sizeof(journalRecord));
    return RecoveryTransactionResult::InvalidCandidate;
  }

  if (!storage_.writeJournal(journalRecord, sizeof(journalRecord))) {
    secureZero(candidateRecord, sizeof(candidateRecord));
    secureZero(journalRecord, sizeof(journalRecord));
    return RecoveryTransactionResult::StorageError;
  }
  uint8_t verifiedJournal[kRecoveryJournalRecordSize]{};
  size_t verifiedJournalSize = 0;
  const bool journalVerified =
      storage_.readJournal(verifiedJournal, sizeof(verifiedJournal),
                           verifiedJournalSize) ==
          security::SecurityStorageReadStatus::Found &&
      verifiedJournalSize == sizeof(journalRecord) &&
      equalBytes(verifiedJournal, journalRecord, sizeof(journalRecord));
  secureZero(verifiedJournal, sizeof(verifiedJournal));
  secureZero(journalRecord, sizeof(journalRecord));
  if (!journalVerified) {
    secureZero(candidateRecord, sizeof(candidateRecord));
    return RecoveryTransactionResult::VerificationFailed;
  }

  if (!storage_.writeSettings(candidateRecord, sizeof(candidateRecord))) {
    secureZero(candidateRecord, sizeof(candidateRecord));
    return RecoveryTransactionResult::StorageError;
  }
  uint8_t verifiedSettings[security::kSecurityRecordSize]{};
  size_t verifiedSettingsSize = 0;
  const bool settingsVerified =
      storage_.readSettings(verifiedSettings, sizeof(verifiedSettings),
                            verifiedSettingsSize) ==
          security::SecurityStorageReadStatus::Found &&
      verifiedSettingsSize == sizeof(candidateRecord) &&
      equalBytes(verifiedSettings, candidateRecord, sizeof(candidateRecord)) &&
      validProtectedRecord(verifiedSettings, verifiedSettingsSize);
  secureZero(verifiedSettings, sizeof(verifiedSettings));
  secureZero(candidateRecord, sizeof(candidateRecord));
  if (!settingsVerified)
    return RecoveryTransactionResult::VerificationFailed;

  if (!eraseJournalAndConfirm(storage_))
    return RecoveryTransactionResult::FinalizeFailed;
  return RecoveryTransactionResult::Applied;
}

const char* recoveryTransactionResultName(RecoveryTransactionResult result) {
  switch (result) {
    case RecoveryTransactionResult::Applied: return "applied";
    case RecoveryTransactionResult::AlreadyAttempted: return "already_attempted";
    case RecoveryTransactionResult::InvalidCandidate: return "invalid_candidate";
    case RecoveryTransactionResult::BaselineMissing: return "baseline_missing";
    case RecoveryTransactionResult::BaselineCorrupt: return "baseline_corrupt";
    case RecoveryTransactionResult::StorageError: return "storage_error";
    case RecoveryTransactionResult::JournalConflict: return "journal_conflict";
    case RecoveryTransactionResult::VerificationFailed:
      return "verification_failed";
    case RecoveryTransactionResult::FinalizeFailed: return "finalize_failed";
    case RecoveryTransactionResult::Busy: return "busy";
  }
  return "unknown";
}

const char* recoveryJournalResolutionName(RecoveryJournalResolution result) {
  switch (result) {
    case RecoveryJournalResolution::Clean: return "clean";
    case RecoveryJournalResolution::Completed: return "completed";
    case RecoveryJournalResolution::Locked: return "locked";
  }
  return "unknown";
}

const char* recoveryBaselineStatusName(RecoveryBaselineStatus result) {
  switch (result) {
    case RecoveryBaselineStatus::Eligible: return "eligible";
    case RecoveryBaselineStatus::Missing: return "missing";
    case RecoveryBaselineStatus::Corrupt: return "corrupt";
    case RecoveryBaselineStatus::StorageError: return "storage_error";
    case RecoveryBaselineStatus::JournalConflict: return "journal_conflict";
  }
  return "unknown";
}

}  // namespace security_recovery
}  // namespace voxone
