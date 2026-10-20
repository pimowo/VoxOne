#include "first_boot_transaction.h"
#include "wifi_config_atomic.h"

#include <cstring>

namespace voxone {
namespace first_boot_transaction {
namespace {

constexpr uint32_t kJournalMagic = 0x58544246U;  // "FBTX" little-endian.
constexpr uint8_t kJournalVersion = 1U;
constexpr uint8_t kPriorHardwarePresent = 1U << 0U;
constexpr uint8_t kCandidateHardwarePresent = 1U << 1U;
constexpr uint8_t kPriorWifiEmptyPresent = 1U << 2U;
constexpr size_t kPriorHardwareOffset = 8U;
constexpr size_t kCandidateHardwareOffset = 18U;
constexpr size_t kWifiSizeOffset = 28U;
constexpr size_t kWifiCrcOffset = 30U;
constexpr size_t kCandidateSecurityOffset = 34U;
constexpr size_t kJournalCrcOffset = 102U;

struct Journal {
  Phase phase;
  bool priorHardwarePresent;
  bool candidateHardwarePresent;
  bool priorWifiEmptyPresent;
  uint8_t priorHardware[hardware::kRuntimeHardwareConfigSerializedSize];
  uint8_t candidateHardware[hardware::kRuntimeHardwareConfigSerializedSize];
  uint16_t wifiSize;
  uint32_t wifiCrc;
  uint8_t candidateSecurity[security::kSecurityRecordSize];
};

uint16_t readLe16(const uint8_t* input) {
  return static_cast<uint16_t>(input[0]) |
         (static_cast<uint16_t>(input[1]) << 8U);
}

uint32_t readLe32(const uint8_t* input) {
  return static_cast<uint32_t>(input[0]) |
         (static_cast<uint32_t>(input[1]) << 8U) |
         (static_cast<uint32_t>(input[2]) << 16U) |
         (static_cast<uint32_t>(input[3]) << 24U);
}

void writeLe16(uint8_t* output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8U);
}

void writeLe32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8U);
  output[2] = static_cast<uint8_t>(value >> 16U);
  output[3] = static_cast<uint8_t>(value >> 24U);
}

bool equalBytes(const uint8_t* left, const uint8_t* right, size_t size) {
  if ((!left || !right) && size) return false;
  uint8_t difference = 0;
  for (size_t index = 0; index < size; ++index)
    difference |= static_cast<uint8_t>(left[index] ^ right[index]);
  return difference == 0U;
}

bool validHardware(const uint8_t* record, size_t size) {
  hardware::RuntimeHardwareConfig decoded{};
  return hardware::deserializeRuntimeHardwareConfig(record, size, decoded) ==
         hardware::RuntimeHardwareDecodeStatus::Valid;
}

bool validSecurityCandidate(const uint8_t* record, size_t size) {
  security::SecuritySettings decoded{};
  return security::deserializeSecuritySettings(record, size, decoded) ==
             security::SecurityDecodeStatus::Valid &&
         decoded.state == security::SecurityState::Open &&
         decoded.generation == 1U;
}

bool validWifiCandidate(const uint8_t* record, size_t size) {
  if (!record || !size || size > wifi_config::kMaxRecordBytes) return false;
  wifi_config::Profile profiles[wifi_config::kMaxProfiles]{};
  const wifi_config::ParseResult parsed = wifi_config::parseFile(
      record, size, profiles, wifi_config::kMaxProfiles);
  return parsed.valid() && parsed.profileCount == 1U;
}

bool validPhase(uint8_t value) {
  return value >= static_cast<uint8_t>(Phase::Prepared) &&
         value <= static_cast<uint8_t>(Phase::SecurityWritten);
}

bool serializeJournal(const Journal& journal,
                      uint8_t output[kJournalRecordSize]) {
  if (!output || journal.wifiSize == 0U ||
      journal.wifiSize > wifi_config::kMaxRecordBytes ||
      (journal.priorHardwarePresent &&
       !validHardware(journal.priorHardware,
                      sizeof(journal.priorHardware))) ||
      (journal.candidateHardwarePresent &&
       !validHardware(journal.candidateHardware,
                      sizeof(journal.candidateHardware))) ||
      !validSecurityCandidate(journal.candidateSecurity,
                              sizeof(journal.candidateSecurity)))
    return false;

  std::memset(output, 0, kJournalRecordSize);
  writeLe32(output, kJournalMagic);
  output[4] = kJournalVersion;
  output[5] = static_cast<uint8_t>(journal.phase);
  output[6] = static_cast<uint8_t>(
      (journal.priorHardwarePresent ? kPriorHardwarePresent : 0U) |
      (journal.candidateHardwarePresent ? kCandidateHardwarePresent : 0U) |
      (journal.priorWifiEmptyPresent ? kPriorWifiEmptyPresent : 0U));
  std::memcpy(output + kPriorHardwareOffset, journal.priorHardware,
              sizeof(journal.priorHardware));
  std::memcpy(output + kCandidateHardwareOffset, journal.candidateHardware,
              sizeof(journal.candidateHardware));
  writeLe16(output + kWifiSizeOffset, journal.wifiSize);
  writeLe32(output + kWifiCrcOffset, journal.wifiCrc);
  std::memcpy(output + kCandidateSecurityOffset, journal.candidateSecurity,
              sizeof(journal.candidateSecurity));
  writeLe32(output + kJournalCrcOffset,
            wifi_config_atomic::recordCrc32(output, kJournalCrcOffset));
  return true;
}

bool deserializeJournal(const uint8_t* input, size_t size, Journal& journal) {
  std::memset(&journal, 0, sizeof(journal));
  if (!input || size != kJournalRecordSize ||
      readLe32(input) != kJournalMagic || input[4] != kJournalVersion ||
      !validPhase(input[5]) || (input[6] & 0xf8U) != 0U || input[7] != 0U ||
      readLe32(input + kJournalCrcOffset) !=
          wifi_config_atomic::recordCrc32(input, kJournalCrcOffset))
    return false;

  journal.phase = static_cast<Phase>(input[5]);
  journal.priorHardwarePresent =
      (input[6] & kPriorHardwarePresent) != 0U;
  journal.candidateHardwarePresent =
      (input[6] & kCandidateHardwarePresent) != 0U;
  journal.priorWifiEmptyPresent =
      (input[6] & kPriorWifiEmptyPresent) != 0U;
  std::memcpy(journal.priorHardware, input + kPriorHardwareOffset,
              sizeof(journal.priorHardware));
  std::memcpy(journal.candidateHardware, input + kCandidateHardwareOffset,
              sizeof(journal.candidateHardware));
  journal.wifiSize = readLe16(input + kWifiSizeOffset);
  journal.wifiCrc = readLe32(input + kWifiCrcOffset);
  std::memcpy(journal.candidateSecurity,
              input + kCandidateSecurityOffset,
              sizeof(journal.candidateSecurity));

  return journal.wifiSize > 0U &&
         journal.wifiSize <= wifi_config::kMaxRecordBytes &&
         (!journal.priorHardwarePresent ||
          validHardware(journal.priorHardware,
                        sizeof(journal.priorHardware))) &&
         (!journal.candidateHardwarePresent ||
          validHardware(journal.candidateHardware,
                        sizeof(journal.candidateHardware))) &&
         validSecurityCandidate(journal.candidateSecurity,
                                sizeof(journal.candidateSecurity));
}

bool writeJournal(Backend& backend, const Journal& journal) {
  uint8_t expected[kJournalRecordSize]{};
  if (!serializeJournal(journal, expected) ||
      !backend.writeJournal(expected, sizeof(expected)))
    return false;
  uint8_t actual[kJournalRecordSize]{};
  size_t size = 0;
  return backend.readJournal(actual, sizeof(actual), size) ==
             RecordStatus::Found &&
         size == sizeof(expected) && equalBytes(actual, expected, size);
}

bool recordMatches(RecordStatus status, const uint8_t* record, size_t size,
                   bool present, const uint8_t* expected,
                   size_t expectedSize) {
  if (!present) return status == RecordStatus::Missing;
  return status == RecordStatus::Found && size == expectedSize &&
         equalBytes(record, expected, expectedSize);
}

bool hardwareStateAllowed(Backend& backend, const Journal& journal) {
  uint8_t record[hardware::kRuntimeHardwareConfigSerializedSize]{};
  size_t size = 0;
  const RecordStatus status = backend.readHardware(record, sizeof(record), size);
  return recordMatches(status, record, size, journal.priorHardwarePresent,
                       journal.priorHardware, sizeof(journal.priorHardware)) ||
         recordMatches(status, record, size, journal.candidateHardwarePresent,
                       journal.candidateHardware,
                       sizeof(journal.candidateHardware));
}

bool hardwareMatchesCandidate(Backend& backend, const Journal& journal) {
  uint8_t record[hardware::kRuntimeHardwareConfigSerializedSize]{};
  size_t size = 0;
  const RecordStatus status = backend.readHardware(record, sizeof(record), size);
  return recordMatches(status, record, size,
                       journal.candidateHardwarePresent,
                       journal.candidateHardware,
                       sizeof(journal.candidateHardware));
}

bool wifiStateAllowed(Backend& backend, const Journal& journal) {
  uint8_t record[wifi_config::kMaxFileBytes]{};
  size_t size = 0;
  const RecordStatus status = backend.readWifi(record, sizeof(record), size);
  const bool prior = journal.priorWifiEmptyPresent
                         ? status == RecordStatus::Found && size == 0U
                         : status == RecordStatus::Missing;
  const bool candidate = status == RecordStatus::Found &&
                         size == journal.wifiSize &&
                         wifi_config_atomic::recordCrc32(record, size) ==
                             journal.wifiCrc;
  return prior || candidate;
}

bool securityStateAllowed(Backend& backend, const Journal& journal) {
  uint8_t record[security::kSecurityRecordSize]{};
  size_t size = 0;
  const RecordStatus status = backend.readSecurity(record, sizeof(record), size);
  return status == RecordStatus::Missing ||
         (status == RecordStatus::Found &&
          size == sizeof(journal.candidateSecurity) &&
          equalBytes(record, journal.candidateSecurity, size));
}

bool restoreSecurity(Backend& backend) {
  uint8_t record[security::kSecurityRecordSize]{};
  size_t size = 0;
  const RecordStatus status = backend.readSecurity(record, sizeof(record), size);
  if (status == RecordStatus::Error) return false;
  if (status == RecordStatus::Found && !backend.eraseSecurity()) return false;
  size = 0;
  return backend.readSecurity(record, sizeof(record), size) ==
         RecordStatus::Missing;
}

bool restoreWifi(Backend& backend, const Journal& journal) {
  return backend.rollbackWifi(journal.priorWifiEmptyPresent,
                              journal.wifiSize, journal.wifiCrc) &&
         wifiStateAllowed(backend, journal);
}

bool restoreHardware(Backend& backend, const Journal& journal) {
  bool restored = false;
  if (journal.priorHardwarePresent) {
    restored = backend.writeHardware(journal.priorHardware,
                                     sizeof(journal.priorHardware));
  } else {
    uint8_t current[hardware::kRuntimeHardwareConfigSerializedSize]{};
    size_t size = 0;
    const RecordStatus status =
        backend.readHardware(current, sizeof(current), size);
    restored = status == RecordStatus::Missing ||
               (status == RecordStatus::Found && backend.eraseHardware());
  }
  if (!restored) return false;
  uint8_t record[hardware::kRuntimeHardwareConfigSerializedSize]{};
  size_t size = 0;
  const RecordStatus status = backend.readHardware(record, sizeof(record), size);
  return recordMatches(status, record, size, journal.priorHardwarePresent,
                       journal.priorHardware, sizeof(journal.priorHardware));
}

bool eraseJournalAndConfirm(Backend& backend) {
  const bool erased = backend.eraseJournal();
  uint8_t record[kJournalRecordSize]{};
  size_t size = 0;
  const RecordStatus status = backend.readJournal(record, sizeof(record), size);
  return status == RecordStatus::Missing && (erased || size == 0U);
}

}  // namespace

RecoveryStatus Transaction::recover() {
  uint8_t record[kJournalRecordSize]{};
  size_t size = 0;
  const RecordStatus status = backend_.readJournal(record, sizeof(record), size);
  if (status == RecordStatus::Missing) return RecoveryStatus::Clean;
  if (status != RecordStatus::Found) return RecoveryStatus::Failed;

  Journal journal{};
  if (!deserializeJournal(record, size, journal) ||
      !securityStateAllowed(backend_, journal) ||
      !hardwareStateAllowed(backend_, journal))
    return RecoveryStatus::Failed;

  // SECURITY is restored first. A failed rollback therefore cannot leave an
  // OPEN administrator state while the other components are still partial.
  if (!restoreSecurity(backend_) || !restoreWifi(backend_, journal) ||
      !restoreHardware(backend_, journal) ||
      !eraseJournalAndConfirm(backend_))
    return RecoveryStatus::Failed;
  return RecoveryStatus::Recovered;
}

Result Transaction::commit(const Candidate& candidate,
                           bool securityLockHeld) {
  failureCause_ = Result::Applied;
  if (!securityLockHeld) return Result::SecurityUnavailable;
  if ((candidate.writeHardware &&
       (candidate.hardwareSize != sizeof(candidate.hardware) ||
        !validHardware(candidate.hardware, candidate.hardwareSize))) ||
      !validWifiCandidate(candidate.wifi, candidate.wifiSize) ||
      candidate.securitySize != sizeof(candidate.security) ||
      !validSecurityCandidate(candidate.security, candidate.securitySize))
    return Result::InvalidCandidate;

  uint8_t existingJournal[kJournalRecordSize]{};
  size_t existingJournalSize = 0;
  const RecordStatus journalStatus = backend_.readJournal(
      existingJournal, sizeof(existingJournal), existingJournalSize);
  if (journalStatus == RecordStatus::Error)
    return Result::JournalReadFailed;
  if (journalStatus == RecordStatus::Found)
    return Result::BaselineConflict;

  Journal journal{};
  journal.phase = Phase::Prepared;
  uint8_t hardwareRecord[hardware::kRuntimeHardwareConfigSerializedSize]{};
  size_t hardwareSize = 0;
  const RecordStatus hardwareStatus = backend_.readHardware(
      hardwareRecord, sizeof(hardwareRecord), hardwareSize);
  if (hardwareStatus == RecordStatus::Error ||
      (hardwareStatus == RecordStatus::Found &&
       (hardwareSize != sizeof(hardwareRecord) ||
        !validHardware(hardwareRecord, hardwareSize))))
    return Result::BaselineConflict;
  journal.priorHardwarePresent = hardwareStatus == RecordStatus::Found;
  if (journal.priorHardwarePresent)
    std::memcpy(journal.priorHardware, hardwareRecord,
                sizeof(journal.priorHardware));
  journal.candidateHardwarePresent = candidate.writeHardware
                                         ? true
                                         : journal.priorHardwarePresent;
  if (candidate.writeHardware) {
    std::memcpy(journal.candidateHardware, candidate.hardware,
                sizeof(journal.candidateHardware));
  } else if (journal.priorHardwarePresent) {
    std::memcpy(journal.candidateHardware, journal.priorHardware,
                sizeof(journal.candidateHardware));
  }

  uint8_t wifiRecord[wifi_config::kMaxFileBytes]{};
  size_t wifiSize = 0;
  const RecordStatus wifiStatus =
      backend_.readWifi(wifiRecord, sizeof(wifiRecord), wifiSize);
  if (wifiStatus == RecordStatus::Error ||
      (wifiStatus == RecordStatus::Found && wifiSize != 0U))
    return Result::BaselineConflict;
  journal.priorWifiEmptyPresent = wifiStatus == RecordStatus::Found;
  journal.wifiSize = static_cast<uint16_t>(candidate.wifiSize);
  journal.wifiCrc = wifi_config_atomic::recordCrc32(candidate.wifi,
                                                    candidate.wifiSize);

  uint8_t securityRecord[security::kSecurityRecordSize]{};
  size_t securitySize = 0;
  if (backend_.readSecurity(securityRecord, sizeof(securityRecord),
                            securitySize) != RecordStatus::Missing)
    return Result::BaselineConflict;
  std::memcpy(journal.candidateSecurity, candidate.security,
              sizeof(journal.candidateSecurity));

  auto fail = [this](Result cause) {
    failureCause_ = cause;
    const RecoveryStatus recovered = recover();
    return recovered == RecoveryStatus::Failed ? Result::RollbackFailed
                                                : cause;
  };

  if (!writeJournal(backend_, journal))
    return fail(Result::JournalWriteFailed);

  if (candidate.writeHardware &&
      !backend_.writeHardware(candidate.hardware, candidate.hardwareSize))
    return fail(Result::HardwareWriteFailed);
  if (!hardwareMatchesCandidate(backend_, journal))
    return fail(Result::HardwareWriteFailed);
  journal.phase = Phase::HardwareWritten;
  if (!writeJournal(backend_, journal))
    return fail(Result::JournalWriteFailed);

  if (!backend_.writeWifi(candidate.wifi, candidate.wifiSize))
    return fail(Result::WifiWriteFailed);
  uint8_t verifiedWifi[wifi_config::kMaxFileBytes]{};
  size_t verifiedWifiSize = 0;
  if (backend_.readWifi(verifiedWifi, sizeof(verifiedWifi),
                        verifiedWifiSize) != RecordStatus::Found ||
      verifiedWifiSize != candidate.wifiSize ||
      !equalBytes(verifiedWifi, candidate.wifi, candidate.wifiSize))
    return fail(Result::WifiWriteFailed);
  journal.phase = Phase::WifiWritten;
  if (!writeJournal(backend_, journal))
    return fail(Result::JournalWriteFailed);

  if (!backend_.completeSecurityOpen(candidate.security,
                                     candidate.securitySize))
    return fail(Result::SecurityCompletionFailed);
  uint8_t verifiedSecurity[security::kSecurityRecordSize]{};
  size_t verifiedSecuritySize = 0;
  if (backend_.readSecurity(verifiedSecurity, sizeof(verifiedSecurity),
                            verifiedSecuritySize) != RecordStatus::Found ||
      verifiedSecuritySize != candidate.securitySize ||
      !equalBytes(verifiedSecurity, candidate.security,
                  candidate.securitySize))
    return fail(Result::SecurityCompletionFailed);
  journal.phase = Phase::SecurityWritten;
  if (!writeJournal(backend_, journal))
    return fail(Result::JournalWriteFailed);

  if (!eraseJournalAndConfirm(backend_))
    return fail(Result::FinalizeFailed);
  return Result::Applied;
}

const char* resultName(Result result) {
  switch (result) {
    case Result::Applied: return "applied";
    case Result::SecurityUnavailable: return "security-unavailable";
    case Result::InvalidCandidate: return "invalid-candidate";
    case Result::BaselineConflict: return "baseline-conflict";
    case Result::JournalReadFailed: return "journal-read-failed";
    case Result::JournalWriteFailed: return "journal-write-failed";
    case Result::HardwareWriteFailed: return "hardware-write-failed";
    case Result::WifiWriteFailed: return "wifi-write-failed";
    case Result::SecurityCompletionFailed:
      return "security-completion-failed";
    case Result::FinalizeFailed: return "finalize-failed";
    case Result::RollbackFailed: return "rollback-failed";
  }
  return "unknown";
}

const char* recoveryStatusName(RecoveryStatus status) {
  switch (status) {
    case RecoveryStatus::Clean: return "clean";
    case RecoveryStatus::Recovered: return "recovered";
    case RecoveryStatus::Failed: return "failed";
  }
  return "unknown";
}

}  // namespace first_boot_transaction
}  // namespace voxone
