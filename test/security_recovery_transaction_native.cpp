#include "../src/core/security_recovery_transaction.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace voxone;
using namespace voxone::security;
using namespace voxone::security_recovery;

namespace {

enum class WriteFault {
  None,
  FailBefore,
  PersistThenFail,
  CorruptPersist,
};

class FakeStorage : public RecoveryTransactionStorage {
 public:
  std::vector<uint8_t> settings;
  std::vector<uint8_t> journal;
  WriteFault settingsWriteFault = WriteFault::None;
  WriteFault journalWriteFault = WriteFault::None;
  bool settingsReadError = false;
  bool journalReadError = false;
  bool eraseJournalError = false;
  unsigned settingsWrites = 0;
  unsigned journalWrites = 0;
  unsigned journalErases = 0;
  uint32_t unrelatedNamespace = 0xa55aa55aU;

  SecurityStorageReadStatus readSettings(uint8_t* output, size_t capacity,
                                         size_t& size) override {
    return read(settings, settingsReadError, output, capacity, size);
  }

  bool writeSettings(const uint8_t* input, size_t size) override {
    ++settingsWrites;
    return write(settings, settingsWriteFault, input, size);
  }

  SecurityStorageReadStatus readJournal(uint8_t* output, size_t capacity,
                                        size_t& size) override {
    return read(journal, journalReadError, output, capacity, size);
  }

  bool writeJournal(const uint8_t* input, size_t size) override {
    ++journalWrites;
    return write(journal, journalWriteFault, input, size);
  }

  bool eraseJournal() override {
    ++journalErases;
    if (eraseJournalError) return false;
    journal.clear();
    return true;
  }

 private:
  static SecurityStorageReadStatus read(const std::vector<uint8_t>& source,
                                        bool error, uint8_t* output,
                                        size_t capacity, size_t& size) {
    size = 0;
    if (error) return SecurityStorageReadStatus::Error;
    if (source.empty()) return SecurityStorageReadStatus::Missing;
    size = source.size();
    if (output && source.size() <= capacity)
      std::memcpy(output, source.data(), source.size());
    return SecurityStorageReadStatus::Found;
  }

  static bool write(std::vector<uint8_t>& destination, WriteFault fault,
                    const uint8_t* input, size_t size) {
    if (fault == WriteFault::FailBefore) return false;
    destination.assign(input, input + size);
    if (fault == WriteFault::CorruptPersist && !destination.empty())
      destination[destination.size() / 2U] ^= 0x80U;
    return fault != WriteFault::PersistThenFail;
  }
};

class ServiceStorage : public SecurityStorage {
 public:
  explicit ServiceStorage(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}

  SecurityStorageReadStatus read(uint8_t* output, size_t capacity,
                                 size_t& size) override {
    size = bytes_.size();
    if (bytes_.empty()) return SecurityStorageReadStatus::Missing;
    if (bytes_.size() <= capacity)
      std::memcpy(output, bytes_.data(), bytes_.size());
    return SecurityStorageReadStatus::Found;
  }
  bool write(const uint8_t*, size_t) override { return false; }
  bool erase() override { return false; }

 private:
  std::vector<uint8_t> bytes_;
};

class FixedRandom : public SecurityRandomSource {
 public:
  bool fill(uint8_t* output, size_t size) override {
    for (size_t index = 0; index < size; ++index)
      output[index] = static_cast<uint8_t>(index + 1U);
    return true;
  }
};

std::vector<uint8_t> record(SecurityState state, uint32_t generation,
                            uint8_t seed) {
  SecuritySettings settings{};
  settings.state = state;
  settings.generation = generation;
  if (state == SecurityState::Protected) {
    settings.iterations = kPbkdf2Iterations;
    for (size_t index = 0; index < sizeof(settings.salt); ++index)
      settings.salt[index] = static_cast<uint8_t>(seed + index);
    for (size_t index = 0; index < sizeof(settings.verifier); ++index)
      settings.verifier[index] = static_cast<uint8_t>(seed + 31U + index);
  }
  std::vector<uint8_t> bytes(kSecurityRecordSize);
  assert(serializeSecuritySettings(settings, bytes.data(), bytes.size()));
  return bytes;
}

PreparedRecoveryCredential prepared(uint8_t seed = 0x40U) {
  PreparedRecoveryCredential value{};
  value.valid = true;
  value.iterations = kPbkdf2Iterations;
  for (size_t index = 0; index < sizeof(value.salt); ++index)
    value.salt[index] = static_cast<uint8_t>(seed + index);
  for (size_t index = 0; index < sizeof(value.verifier); ++index)
    value.verifier[index] = static_cast<uint8_t>(seed + 17U + index);
  return value;
}

SecuritySettings decode(const std::vector<uint8_t>& bytes) {
  SecuritySettings result{};
  assert(deserializeSecuritySettings(bytes.data(), bytes.size(), result) ==
         SecurityDecodeStatus::Valid);
  return result;
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void assertUnrelatedIntact(const FakeStorage& storage) {
  assert(storage.unrelatedNamespace == 0xa55aa55aU);
}

}  // namespace

int main() {
  const std::vector<uint8_t> oldProtected =
      record(SecurityState::Protected, 7U, 0x10U);

  FakeStorage success;
  success.settings = oldProtected;
  assert(inspectRecoveryBaseline(success) == RecoveryBaselineStatus::Eligible);
  RecoveryTransaction successful(success);
  assert(successful.commit(prepared()) == RecoveryTransactionResult::Applied);
  assert(successful.attempted());
  assert(success.journal.empty());
  assert(success.settingsWrites == 1 && success.journalWrites == 1 &&
         success.journalErases == 1);
  SecuritySettings installed = decode(success.settings);
  assert(installed.state == SecurityState::Protected);
  assert(installed.generation == 8U);
  assert(successful.commit(prepared(0x60U)) ==
         RecoveryTransactionResult::AlreadyAttempted);
  assert(success.settingsWrites == 1);
  assertUnrelatedIntact(success);

  FakeStorage legacy;
  ServiceStorage legacyBeforeStorage({});
  FixedRandom random;
  SecurityService legacyBefore(legacyBeforeStorage, random);
  assert(legacyBefore.begin(true) == SecurityLoadStatus::LegacyUnclaimed);
  assert(inspectRecoveryBaseline(legacy) == RecoveryBaselineStatus::Missing);
  RecoveryTransaction legacyTransaction(legacy);
  assert(legacyTransaction.commit(prepared()) ==
         RecoveryTransactionResult::BaselineMissing);
  assert(legacy.settings.empty() && legacy.journal.empty());
  assert(legacy.settingsWrites == 0 && legacy.journalWrites == 0);
  assertUnrelatedIntact(legacy);

  FakeStorage unconfigured;
  ServiceStorage unconfiguredBeforeStorage({});
  SecurityService unconfiguredBefore(unconfiguredBeforeStorage, random);
  assert(unconfiguredBefore.begin(false) == SecurityLoadStatus::Unconfigured);
  assert(inspectRecoveryBaseline(unconfigured) ==
         RecoveryBaselineStatus::Missing);
  RecoveryTransaction unconfiguredTransaction(unconfigured);
  assert(unconfiguredTransaction.commit(prepared()) ==
         RecoveryTransactionResult::BaselineMissing);
  assert(unconfigured.settings.empty() && unconfigured.journal.empty());
  assert(unconfigured.settingsWrites == 0 &&
         unconfigured.journalWrites == 0);

  FakeStorage open;
  open.settings = record(SecurityState::Open, 9U, 0);
  assert(inspectRecoveryBaseline(open) == RecoveryBaselineStatus::Eligible);
  RecoveryTransaction openTransaction(open);
  assert(openTransaction.commit(prepared()) ==
         RecoveryTransactionResult::Applied);
  installed = decode(open.settings);
  assert(installed.state == SecurityState::Protected &&
         installed.generation == 10U);

  FakeStorage corruptBaseline;
  corruptBaseline.settings.assign(kSecurityRecordSize, 0xa5U);
  ServiceStorage corruptServiceStorage(corruptBaseline.settings);
  SecurityService locked(corruptServiceStorage, random);
  assert(locked.begin(true) == SecurityLoadStatus::Corrupt);
  assert(locked.state() == SecurityState::Locked);
  assert(inspectRecoveryBaseline(corruptBaseline) ==
         RecoveryBaselineStatus::Corrupt);
  RecoveryTransaction corruptTransaction(corruptBaseline);
  assert(corruptTransaction.commit(prepared()) ==
         RecoveryTransactionResult::BaselineCorrupt);
  assert(corruptBaseline.settingsWrites == 0 &&
         corruptBaseline.journalWrites == 0);

  FakeStorage unreadableBaseline;
  unreadableBaseline.settings = oldProtected;
  unreadableBaseline.settingsReadError = true;
  assert(inspectRecoveryBaseline(unreadableBaseline) ==
         RecoveryBaselineStatus::StorageError);
  RecoveryTransaction unreadableTransaction(unreadableBaseline);
  assert(unreadableTransaction.commit(prepared()) ==
         RecoveryTransactionResult::StorageError);
  assert(unreadableBaseline.journalWrites == 0);

  PreparedRecoveryCredential invalid{};
  FakeStorage invalidCandidate;
  invalidCandidate.settings = oldProtected;
  RecoveryTransaction invalidTransaction(invalidCandidate);
  assert(invalidTransaction.commit(invalid) ==
         RecoveryTransactionResult::InvalidCandidate);
  assert(invalidCandidate.journalWrites == 0 &&
         invalidCandidate.settingsWrites == 0);

  PreparedRecoveryCredential invalidIterations = prepared();
  invalidIterations.iterations = kPbkdf2Iterations + 1U;
  FakeStorage invalidIterationsStorage;
  invalidIterationsStorage.settings = oldProtected;
  RecoveryTransaction invalidIterationsTransaction(invalidIterationsStorage);
  assert(invalidIterationsTransaction.commit(invalidIterations) ==
         RecoveryTransactionResult::InvalidCandidate);

  PreparedRecoveryCredential invalidSalt = prepared();
  std::memset(invalidSalt.salt, 0, sizeof(invalidSalt.salt));
  FakeStorage invalidSaltStorage;
  invalidSaltStorage.settings = oldProtected;
  RecoveryTransaction invalidSaltTransaction(invalidSaltStorage);
  assert(invalidSaltTransaction.commit(invalidSalt) ==
         RecoveryTransactionResult::InvalidCandidate);

  PreparedRecoveryCredential invalidVerifier = prepared();
  std::memset(invalidVerifier.verifier, 0, sizeof(invalidVerifier.verifier));
  FakeStorage invalidVerifierStorage;
  invalidVerifierStorage.settings = oldProtected;
  RecoveryTransaction invalidVerifierTransaction(invalidVerifierStorage);
  assert(invalidVerifierTransaction.commit(invalidVerifier) ==
         RecoveryTransactionResult::InvalidCandidate);

  FakeStorage nvsFullBeforeJournal;
  nvsFullBeforeJournal.settings = oldProtected;
  nvsFullBeforeJournal.journalWriteFault = WriteFault::FailBefore;
  RecoveryTransaction nvsFullBeforeJournalTransaction(nvsFullBeforeJournal);
  assert(nvsFullBeforeJournalTransaction.commit(prepared()) ==
         RecoveryTransactionResult::StorageError);
  assert(nvsFullBeforeJournal.settings == oldProtected);
  assert(nvsFullBeforeJournal.journal.empty());

  FakeStorage powerAfterJournal;
  powerAfterJournal.settings = oldProtected;
  powerAfterJournal.journalWriteFault = WriteFault::PersistThenFail;
  RecoveryTransaction powerAfterJournalTransaction(powerAfterJournal);
  assert(powerAfterJournalTransaction.commit(prepared()) ==
         RecoveryTransactionResult::StorageError);
  assert(powerAfterJournal.settings == oldProtected);
  assert(!powerAfterJournal.journal.empty());
  assert(decode(powerAfterJournal.settings).generation == 7U);
  ServiceStorage oldSlotBeforeRollForward(powerAfterJournal.settings);
  SecurityService oldSlotOldPassword(oldSlotBeforeRollForward, random);
  assert(oldSlotOldPassword.begin(true) == SecurityLoadStatus::Loaded);
  assert(oldSlotOldPassword.state() == SecurityState::Protected);
  assert(oldSlotOldPassword.generation() == 7U);
  powerAfterJournal.journalWriteFault = WriteFault::None;
  assert(resolveRecoveryJournal(powerAfterJournal) ==
         RecoveryJournalResolution::Completed);
  assert(powerAfterJournal.journal.empty());
  assert(decode(powerAfterJournal.settings).generation == 8U);

  FakeStorage nvsFullDuringSettings;
  nvsFullDuringSettings.settings = oldProtected;
  nvsFullDuringSettings.settingsWriteFault = WriteFault::FailBefore;
  RecoveryTransaction nvsFullDuringSettingsTransaction(nvsFullDuringSettings);
  assert(nvsFullDuringSettingsTransaction.commit(prepared()) ==
         RecoveryTransactionResult::StorageError);
  assert(!nvsFullDuringSettings.journal.empty());
  assert(nvsFullDuringSettings.settings == oldProtected);
  assert(resolveRecoveryJournal(nvsFullDuringSettings) ==
         RecoveryJournalResolution::Locked);
  assert(!nvsFullDuringSettings.journal.empty());
  assert(nvsFullDuringSettings.settings == oldProtected);
  nvsFullDuringSettings.settingsWriteFault = WriteFault::None;
  assert(resolveRecoveryJournal(nvsFullDuringSettings) ==
         RecoveryJournalResolution::Completed);
  assert(nvsFullDuringSettings.journal.empty());
  assert(decode(nvsFullDuringSettings.settings).generation == 8U);

  FakeStorage baselineChangedBeforeCommit;
  baselineChangedBeforeCommit.settings = oldProtected;
  assert(inspectRecoveryBaseline(baselineChangedBeforeCommit) ==
         RecoveryBaselineStatus::Eligible);
  baselineChangedBeforeCommit.settings.clear();
  RecoveryTransaction changedBaselineTransaction(baselineChangedBeforeCommit);
  assert(changedBaselineTransaction.commit(prepared()) ==
         RecoveryTransactionResult::BaselineMissing);
  assert(baselineChangedBeforeCommit.settingsWrites == 0 &&
         baselineChangedBeforeCommit.journalWrites == 0 &&
         baselineChangedBeforeCommit.journal.empty());

  FakeStorage powerAfterSettingsCommit;
  powerAfterSettingsCommit.settings = oldProtected;
  powerAfterSettingsCommit.settingsWriteFault = WriteFault::PersistThenFail;
  RecoveryTransaction powerAfterSettingsTransaction(powerAfterSettingsCommit);
  assert(powerAfterSettingsTransaction.commit(prepared()) ==
         RecoveryTransactionResult::StorageError);
  assert(!powerAfterSettingsCommit.journal.empty());
  assert(decode(powerAfterSettingsCommit.settings).generation == 8U);
  ServiceStorage oldSlotAfterSettings(powerAfterSettingsCommit.settings);
  SecurityService oldSlotNewPassword(oldSlotAfterSettings, random);
  assert(oldSlotNewPassword.begin(true) == SecurityLoadStatus::Loaded);
  assert(oldSlotNewPassword.state() == SecurityState::Protected);
  assert(oldSlotNewPassword.generation() == 8U);
  powerAfterSettingsCommit.settingsWriteFault = WriteFault::None;
  assert(resolveRecoveryJournal(powerAfterSettingsCommit) ==
         RecoveryJournalResolution::Completed);
  assert(powerAfterSettingsCommit.journal.empty());
  assert(decode(powerAfterSettingsCommit.settings).state ==
         SecurityState::Protected);

  FakeStorage powerBeforeJournalCleanup;
  powerBeforeJournalCleanup.settings = oldProtected;
  powerBeforeJournalCleanup.eraseJournalError = true;
  RecoveryTransaction cleanupTransaction(powerBeforeJournalCleanup);
  assert(cleanupTransaction.commit(prepared()) ==
         RecoveryTransactionResult::FinalizeFailed);
  assert(!powerBeforeJournalCleanup.journal.empty());
  assert(decode(powerBeforeJournalCleanup.settings).generation == 8U);
  powerBeforeJournalCleanup.eraseJournalError = false;
  assert(resolveRecoveryJournal(powerBeforeJournalCleanup) ==
         RecoveryJournalResolution::Completed);

  FakeStorage corruptJournal;
  corruptJournal.settings = oldProtected;
  corruptJournal.journalWriteFault = WriteFault::CorruptPersist;
  RecoveryTransaction corruptJournalTransaction(corruptJournal);
  assert(corruptJournalTransaction.commit(prepared()) ==
         RecoveryTransactionResult::VerificationFailed);
  assert(resolveRecoveryJournal(corruptJournal) ==
         RecoveryJournalResolution::Locked);
  assert(!corruptJournal.journal.empty());
  assert(corruptJournal.settings == oldProtected);

  FakeStorage unreadableJournal;
  unreadableJournal.settings = oldProtected;
  unreadableJournal.journal.assign(kRecoveryJournalRecordSize, 0x5aU);
  unreadableJournal.journalReadError = true;
  assert(resolveRecoveryJournal(unreadableJournal) ==
         RecoveryJournalResolution::Locked);
  assert(unreadableJournal.journalErases == 0);

  FakeStorage corruptSettingsAfterWrite;
  corruptSettingsAfterWrite.settings = oldProtected;
  corruptSettingsAfterWrite.settingsWriteFault = WriteFault::CorruptPersist;
  RecoveryTransaction corruptSettingsTransaction(corruptSettingsAfterWrite);
  assert(corruptSettingsTransaction.commit(prepared()) ==
         RecoveryTransactionResult::VerificationFailed);
  assert(resolveRecoveryJournal(corruptSettingsAfterWrite) ==
         RecoveryJournalResolution::Locked);

  FakeStorage concurrentMutation;
  concurrentMutation.settings = oldProtected;
  concurrentMutation.journalWriteFault = WriteFault::PersistThenFail;
  RecoveryTransaction interrupted(concurrentMutation);
  assert(interrupted.commit(prepared()) ==
         RecoveryTransactionResult::StorageError);
  concurrentMutation.settings = record(SecurityState::Protected, 99U, 0x70U);
  concurrentMutation.journalWriteFault = WriteFault::None;
  assert(resolveRecoveryJournal(concurrentMutation) ==
         RecoveryJournalResolution::Locked);

  const std::string transactionSource =
      readFile("src/core/security_recovery_transaction.cpp");
  const std::string nvsSource =
      readFile("src/core/security_recovery_transaction_nvs.cpp");
  const std::string runtime =
      readFile("src/core/security_recovery_ap_runtime.cpp");
  const std::string main = readFile("src/main.cpp");
  assert(transactionSource.find("secureZero") != std::string::npos);
  assert(transactionSource.find("new_password") == std::string::npos);
  assert(transactionSource.find("RecoveryTransactionResult::BaselineMissing") !=
         std::string::npos);
  assert(transactionSource.find("input[6] != kPriorPresent") !=
         std::string::npos);
  assert(nvsSource.find("voxsecurity") != std::string::npos);
  assert(nvsSource.find("settings") != std::string::npos);
  assert(nvsSource.find("recovery_jrnl") != std::string::npos);
  assert(nvsSource.find("nvs_commit") != std::string::npos);
  assert(nvsSource.find("nvs_erase_all") == std::string::npos);
  assert(nvsSource.find("SPIFFS") == std::string::npos);
  assert(nvsSource.find("esp_ota") == std::string::npos);
  assert(nvsSource.find("beginUpdate(UpdateTarget::SecurityRecovery") !=
         std::string::npos);
  assert(nvsSource.find("inspectDeviceSecurityRecoveryBaseline") !=
         std::string::npos);
  const size_t confirmed = runtime.find("ConfirmationKeyState::Accepted");
  const size_t committing = runtime.find("beginRecoveryCommit(nowMs)");
  const size_t persisted = runtime.find("commitDeviceSecurityRecovery");
  assert(confirmed < committing && committing < persisted);
  assert(runtime.find("ESP.restart()") != std::string::npos);
  assert(main.find("beginSecurityRecoveryAp(") < main.find("config.init()"));
  assertUnrelatedIntact(powerAfterJournal);
  assertUnrelatedIntact(powerAfterSettingsCommit);
  assertUnrelatedIntact(powerBeforeJournalCleanup);
}
