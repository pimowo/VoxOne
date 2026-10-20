#include "../src/core/first_boot_transaction.h"
#include "../src/core/first_boot_core.h"
#include "../src/core/configuration_mutation.h"
#include "../src/core/wifi_config_atomic.h"

#include <atomic>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace voxone;
using namespace voxone::first_boot_transaction;

namespace {

struct Record {
  bool present = false;
  std::vector<uint8_t> bytes;
};

struct DurableState {
  Record journal;
  Record hardware;
  Record wifi;
  Record security;
};

struct PowerCut {};

enum class RuntimeSecurity { Unconfigured, LegacyUnclaimed, Open, Protected };

struct RuntimeState {
  bool hardwarePresent;
  bool wifiConfigured;
  RuntimeSecurity security;
  bool setupAp;
};

RuntimeState bootFromPersistent(const DurableState& state) {
  RuntimeSecurity securityState = RuntimeSecurity::Unconfigured;
  if (state.security.present) {
    security::SecuritySettings decoded{};
    assert(security::deserializeSecuritySettings(
               state.security.bytes.data(), state.security.bytes.size(),
               decoded) == security::SecurityDecodeStatus::Valid);
    securityState = decoded.state == security::SecurityState::Protected
                        ? RuntimeSecurity::Protected
                        : RuntimeSecurity::Open;
  } else if (state.wifi.present && !state.wifi.bytes.empty()) {
    securityState = RuntimeSecurity::LegacyUnclaimed;
  }
  const bool wifiConfigured = state.wifi.present && !state.wifi.bytes.empty();
  return {state.hardware.present, wifiConfigured, securityState,
          first_boot::shouldStartOpenSetupAp(wifiConfigured ? 1U : 0U)};
}

class FakeBackend : public Backend {
 public:
  explicit FakeBackend(DurableState& state) : state_(state) {}

  bool journalReadError = false;
  bool hardwareReadError = false;
  bool wifiReadError = false;
  bool securityReadError = false;
  int failJournalWriteCall = -1;
  int failHardwareWriteCall = -1;
  int failWifiWriteCall = -1;
  int failSecurityCall = -1;
  bool failJournalErase = false;
  bool failHardwareErase = false;
  bool failWifiErase = false;
  bool failSecurityErase = false;
  bool ignoreHardwareWrite = false;
  int cutAt = -1;
  bool cutBefore = false;
  int durableOperations = 0;

  RecordStatus readJournal(uint8_t* output, size_t capacity,
                           size_t& size) override {
    if (journalReadError) {
      size = 0;
      return RecordStatus::Error;
    }
    return read(state_.journal, output, capacity, size);
  }
  bool writeJournal(const uint8_t* input, size_t size) override {
    ++journalWrites_;
    return mutate(state_.journal, input, size,
                  journalWrites_ == failJournalWriteCall);
  }
  bool eraseJournal() override {
    return erase(state_.journal, failJournalErase);
  }

  RecordStatus readHardware(uint8_t* output, size_t capacity,
                            size_t& size) override {
    if (hardwareReadError) {
      size = 0;
      return RecordStatus::Error;
    }
    return read(state_.hardware, output, capacity, size);
  }
  bool writeHardware(const uint8_t* input, size_t size) override {
    ++hardwareWrites_;
    if (ignoreHardwareWrite) return true;
    return mutate(state_.hardware, input, size,
                  hardwareWrites_ == failHardwareWriteCall);
  }
  bool eraseHardware() override {
    return erase(state_.hardware, failHardwareErase);
  }

  RecordStatus readWifi(uint8_t* output, size_t capacity,
                        size_t& size) override {
    if (wifiReadError) {
      size = 0;
      return RecordStatus::Error;
    }
    return read(state_.wifi, output, capacity, size);
  }
  bool writeWifi(const uint8_t* input, size_t size) override {
    ++wifiWrites_;
    return mutate(state_.wifi, input, size,
                  wifiWrites_ == failWifiWriteCall);
  }
  bool eraseWifi() override { return erase(state_.wifi, failWifiErase); }
  bool rollbackWifi(bool priorEmpty, size_t, uint32_t) override {
    return priorEmpty ? writeWifi(nullptr, 0U) : eraseWifi();
  }

  RecordStatus readSecurity(uint8_t* output, size_t capacity,
                            size_t& size) override {
    if (securityReadError) {
      size = 0;
      return RecordStatus::Error;
    }
    return read(state_.security, output, capacity, size);
  }
  bool completeSecurityOpen(const uint8_t* expected, size_t size) override {
    ++securityWrites_;
    return mutate(state_.security, expected, size,
                  securityWrites_ == failSecurityCall);
  }
  bool eraseSecurity() override {
    return erase(state_.security, failSecurityErase);
  }

 private:
  DurableState& state_;
  int journalWrites_ = 0;
  int hardwareWrites_ = 0;
  int wifiWrites_ = 0;
  int securityWrites_ = 0;

  RecordStatus read(const Record& record, uint8_t* output, size_t capacity,
                    size_t& size) const {
    size = 0;
    if (!record.present) return RecordStatus::Missing;
    size = record.bytes.size();
    if (output && size <= capacity && size)
      std::memcpy(output, record.bytes.data(), size);
    return RecordStatus::Found;
  }

  void cut(bool before) {
    if (cutAt < 0 || cutBefore != before) return;
    if (before) {
      ++durableOperations;
      if (durableOperations == cutAt) throw PowerCut{};
    } else if (durableOperations == cutAt) {
      throw PowerCut{};
    }
  }

  bool mutate(Record& record, const uint8_t* input, size_t size, bool fail) {
    cut(true);
    if (fail) return false;
    record.present = true;
    if (size)
      record.bytes.assign(input, input + size);
    else
      record.bytes.clear();
    if (!cutBefore) ++durableOperations;
    cut(false);
    return true;
  }

  bool erase(Record& record, bool fail) {
    cut(true);
    if (fail) return false;
    record.present = false;
    record.bytes.clear();
    if (!cutBefore) ++durableOperations;
    cut(false);
    return true;
  }
};

std::vector<uint8_t> hardwareRecord(hardware::RuntimeDisplay display,
                                    hardware::RuntimeInput input) {
  const hardware::RuntimeHardwareConfig config{
      hardware::kRuntimeHardwareConfigVersion, display, input,
      hardware::RuntimeAudioOutput::Pcm5102a};
  std::vector<uint8_t> output(hardware::kRuntimeHardwareConfigSerializedSize);
  assert(hardware::serializeRuntimeHardwareConfig(config, output.data(),
                                                  output.size()));
  return output;
}

std::vector<uint8_t> openSecurityRecord() {
  security::SecuritySettings settings{};
  settings.state = security::SecurityState::Open;
  settings.generation = 1U;
  std::vector<uint8_t> output(security::kSecurityRecordSize);
  assert(security::serializeSecuritySettings(settings, output.data(),
                                             output.size()));
  return output;
}

std::vector<uint8_t> protectedSecurityRecord() {
  security::SecuritySettings settings{};
  settings.state = security::SecurityState::Protected;
  settings.generation = 9U;
  settings.iterations = security::kPbkdf2Iterations;
  std::memset(settings.salt, 0x11, sizeof(settings.salt));
  std::memset(settings.verifier, 0x22, sizeof(settings.verifier));
  std::vector<uint8_t> output(security::kSecurityRecordSize);
  assert(security::serializeSecuritySettings(settings, output.data(),
                                             output.size()));
  return output;
}

void writeLe32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8U);
  output[2] = static_cast<uint8_t>(value >> 16U);
  output[3] = static_cast<uint8_t>(value >> 24U);
}

Candidate candidate(bool writeHardware = true) {
  Candidate result{};
  result.writeHardware = writeHardware;
  const auto hardware = hardwareRecord(hardware::RuntimeDisplay::None,
                                       hardware::RuntimeInput::Buttons3);
  std::memcpy(result.hardware, hardware.data(), hardware.size());
  result.hardwareSize = hardware.size();
  const std::string wifi = "Home\tsecret\n";
  std::memcpy(result.wifi, wifi.data(), wifi.size());
  result.wifiSize = wifi.size();
  const auto security = openSecurityRecord();
  std::memcpy(result.security, security.data(), security.size());
  result.securitySize = security.size();
  return result;
}

void assertInitialState(const DurableState& state,
                        const Record& priorHardware = Record{}) {
  assert(!state.journal.present);
  assert(state.hardware.present == priorHardware.present);
  assert(state.hardware.bytes == priorHardware.bytes);
  assert(!state.wifi.present);
  assert(!state.security.present);
  const RuntimeState runtime = bootFromPersistent(state);
  assert(!runtime.wifiConfigured);
  assert(runtime.security == RuntimeSecurity::Unconfigured);
  assert(runtime.setupAp);
}

void assertCompletedState(const DurableState& state) {
  const Candidate expected = candidate();
  assert(!state.journal.present);
  assert(state.hardware.present);
  assert(state.hardware.bytes == std::vector<uint8_t>(
      expected.hardware, expected.hardware + expected.hardwareSize));
  assert(state.wifi.present && !state.wifi.bytes.empty());
  assert(state.security.present &&
         state.security.bytes == openSecurityRecord());
  const RuntimeState runtime = bootFromPersistent(state);
  assert(runtime.wifiConfigured);
  assert(runtime.security == RuntimeSecurity::Open);
  assert(!runtime.setupAp);
}

void recoverAfterCut(DurableState& state, const Record& priorHardware) {
  const bool hadJournal = state.journal.present;
  FakeBackend rebooted(state);
  Transaction recovery(rebooted);
  const RecoveryStatus status = recovery.recover();
  if (hadJournal) assert(status == RecoveryStatus::Recovered);
  assertInitialState(state, priorHardware);
}

}  // namespace

int main() {
  // No SECURITY lock means no journal and no persistent mutation.
  {
    DurableState state;
    FakeBackend backend(state);
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), false) ==
           Result::SecurityUnavailable);
    assert(backend.durableOperations == 0);
    assertInitialState(state);
  }

  // Normal completion persists one coherent generation.
  {
    DurableState state;
    FakeBackend backend(state);
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) == Result::Applied);
    assertCompletedState(state);
  }

  // An existing valid hardware record is snapshotted and preserved by every
  // failed attempt; a missing record stays missing.
  for (bool withPriorHardware : {false, true}) {
    DurableState state;
    Record prior;
    if (withPriorHardware) {
      prior.present = true;
      prior.bytes = hardwareRecord(hardware::RuntimeDisplay::Ssd1306_128x64,
                                   hardware::RuntimeInput::Ec11);
      state.hardware = prior;
    }
    FakeBackend backend(state);
    backend.failWifiWriteCall = 1;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) == Result::WifiWriteFailed);
    assertInitialState(state, prior);
  }

  // Explicit hardware, Wi-Fi and SECURITY failures all roll back completely.
  {
    DurableState state;
    FakeBackend backend(state);
    backend.failHardwareWriteCall = 1;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) ==
           Result::HardwareWriteFailed);
    assertInitialState(state);
  }
  {
    DurableState state;
    FakeBackend backend(state);
    backend.ignoreHardwareWrite = true;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) ==
           Result::HardwareWriteFailed);
    assertInitialState(state);
  }
  {
    DurableState state;
    FakeBackend backend(state);
    backend.failSecurityCall = 1;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) ==
           Result::SecurityCompletionFailed);
    assertInitialState(state);
  }

  // Journal write/read faults happen before mutation or retain a recoverable
  // marker. They never turn a partial generation into a completed one.
  {
    DurableState state;
    FakeBackend backend(state);
    backend.journalReadError = true;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) == Result::JournalReadFailed);
    assert(!state.hardware.present && !state.wifi.present &&
           !state.security.present);
  }
  for (int readFault = 0; readFault < 3; ++readFault) {
    DurableState state;
    FakeBackend backend(state);
    backend.hardwareReadError = readFault == 0;
    backend.wifiReadError = readFault == 1;
    backend.securityReadError = readFault == 2;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) == Result::BaselineConflict);
    assert(!state.journal.present && !state.hardware.present &&
           !state.wifi.present && !state.security.present);
  }
  {
    DurableState state;
    FakeBackend backend(state);
    backend.failJournalWriteCall = 1;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) == Result::JournalWriteFailed);
    assertInitialState(state);
  }
  for (int failedPhaseWrite = 2; failedPhaseWrite <= 4;
       ++failedPhaseWrite) {
    DurableState state;
    FakeBackend backend(state);
    backend.failJournalWriteCall = failedPhaseWrite;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) == Result::JournalWriteFailed);
    assertInitialState(state);
  }
  {
    DurableState state;
    FakeBackend backend(state);
    backend.failJournalErase = true;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) == Result::RollbackFailed);
    assert(transaction.failureCause() == Result::FinalizeFailed);
    assert(state.journal.present);
    backend.failJournalErase = false;
    Transaction retry(backend);
    assert(retry.recover() == RecoveryStatus::Recovered);
    assertInitialState(state);
  }
  {
    DurableState state;
    Record prior;
    prior.present = true;
    prior.bytes = hardwareRecord(hardware::RuntimeDisplay::Ssd1306_128x64,
                                 hardware::RuntimeInput::Ec11);
    state.hardware = prior;
    FakeBackend backend(state);
    backend.failSecurityCall = 1;
    backend.failHardwareWriteCall = 2;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) == Result::RollbackFailed);
    assert(transaction.failureCause() == Result::SecurityCompletionFailed);
    assert(state.journal.present);
    backend.failHardwareWriteCall = -1;
    Transaction retry(backend);
    assert(retry.recover() == RecoveryStatus::Recovered);
    assertInitialState(state, prior);
  }

  // Rollback errors are surfaced together with the original failure cause.
  {
    DurableState state;
    FakeBackend backend(state);
    backend.failWifiWriteCall = 1;
    backend.failHardwareErase = true;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) == Result::RollbackFailed);
    assert(transaction.failureCause() == Result::WifiWriteFailed);
    assert(state.journal.present);
    backend.failHardwareErase = false;
    Transaction retry(backend);
    assert(retry.recover() == RecoveryStatus::Recovered);
    assertInitialState(state);
  }
  {
    DurableState state;
    FakeBackend backend(state);
    backend.failSecurityCall = 1;
    backend.failWifiErase = true;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) == Result::RollbackFailed);
    assert(transaction.failureCause() == Result::SecurityCompletionFailed);
    assert(state.journal.present);
    backend.failWifiErase = false;
    Transaction retry(backend);
    assert(retry.recover() == RecoveryStatus::Recovered);
    assertInitialState(state);
  }
  {
    DurableState state;
    FakeBackend backend(state);
    backend.failJournalWriteCall = 4;
    backend.failSecurityErase = true;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) == Result::RollbackFailed);
    assert(transaction.failureCause() == Result::JournalWriteFailed);
    assert(state.journal.present && state.security.present);
    backend.failSecurityErase = false;
    Transaction retry(backend);
    assert(retry.recover() == RecoveryStatus::Recovered);
    assertInitialState(state);
  }

  // An empty pre-existing Wi-Fi file is a distinct baseline and is restored
  // as an empty file, not silently changed into a missing record.
  {
    DurableState state;
    state.wifi.present = true;
    FakeBackend backend(state);
    backend.failSecurityCall = 1;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) ==
           Result::SecurityCompletionFailed);
    assert(!state.journal.present && state.wifi.present &&
           state.wifi.bytes.empty() && !state.security.present);
  }
  {
    DurableState state;
    state.wifi.present = true;
    FakeBackend backend(state);
    backend.failSecurityCall = 1;
    backend.failWifiWriteCall = 2;
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) == Result::RollbackFailed);
    assert(transaction.failureCause() == Result::SecurityCompletionFailed);
    assert(state.journal.present);
    backend.failWifiWriteCall = -1;
    Transaction retry(backend);
    assert(retry.recover() == RecoveryStatus::Recovered);
    assert(!state.journal.present && state.wifi.present &&
           state.wifi.bytes.empty() && !state.security.present);
  }

  // Power is removed both before and after each durable operation. A reboot
  // reconstructs RAM solely from persistent records, then either rolls back a
  // pending marker or observes the fully committed generation.
  bool sawCutAfterSecurity = false;
  for (bool cutBefore : {true, false}) {
    bool reachedCompletion = false;
    for (int cut = 1; cut <= 40; ++cut) {
      DurableState state;
      FakeBackend backend(state);
      backend.cutBefore = cutBefore;
      backend.cutAt = cut;
      Transaction transaction(backend);
      try {
        const Result result = transaction.commit(candidate(), true);
        assert(result == Result::Applied);
        assertCompletedState(state);
        reachedCompletion = true;
        break;
      } catch (const PowerCut&) {
        if (state.journal.present && state.security.present)
          sawCutAfterSecurity = true;
        if (state.journal.present) {
          recoverAfterCut(state, Record{});
        } else if (state.wifi.present) {
          assertCompletedState(state);
        } else {
          assertInitialState(state);
        }
      }
    }
    assert(reachedCompletion);
  }
  assert(sawCutAfterSecurity);

  // A second power cut during recovery leaves the same journal for another
  // idempotent attempt; RAM from the original attempt is not retained.
  {
    DurableState state;
    FakeBackend interrupted(state);
    interrupted.cutAt = 4;
    try {
      Transaction transaction(interrupted);
      (void)transaction.commit(candidate(), true);
      assert(false);
    } catch (const PowerCut&) {
      assert(state.journal.present);
    }
    FakeBackend firstRecovery(state);
    firstRecovery.cutAt = 1;
    try {
      Transaction recovery(firstRecovery);
      (void)recovery.recover();
      assert(false);
    } catch (const PowerCut&) {
      assert(state.journal.present);
    }
    recoverAfterCut(state, Record{});
  }

  // A failed attempt can be provisioned again after verified rollback.
  {
    DurableState state;
    FakeBackend failed(state);
    failed.failSecurityCall = 1;
    Transaction first(failed);
    assert(first.commit(candidate(), true) ==
           Result::SecurityCompletionFailed);
    assertInitialState(state);
    FakeBackend retryBackend(state);
    Transaction retry(retryBackend);
    assert(retry.commit(candidate(), true) == Result::Applied);
    assertCompletedState(state);
  }

  // Legacy Wi-Fi and a protected installed device have no transaction marker;
  // startup recovery is a strict no-op and never downgrades SECURITY.
  {
    DurableState legacy;
    legacy.wifi.present = true;
    const std::string wifi = "Legacy\tsecret\n";
    legacy.wifi.bytes.assign(wifi.begin(), wifi.end());
    FakeBackend backend(legacy);
    Transaction recovery(backend);
    assert(recovery.recover() == RecoveryStatus::Clean);
    const RuntimeState runtime = bootFromPersistent(legacy);
    assert(runtime.wifiConfigured &&
           runtime.security == RuntimeSecurity::LegacyUnclaimed &&
           !runtime.setupAp);
  }
  {
    DurableState installed;
    installed.wifi.present = true;
    const std::string wifi = "Installed\tsecret\n";
    installed.wifi.bytes.assign(wifi.begin(), wifi.end());
    installed.security.present = true;
    installed.security.bytes = protectedSecurityRecord();
    const DurableState before = installed;
    FakeBackend backend(installed);
    Transaction recovery(backend);
    assert(recovery.recover() == RecoveryStatus::Clean);
    assert(installed.wifi.bytes == before.wifi.bytes);
    assert(installed.security.bytes == before.security.bytes);
    const RuntimeState runtime = bootFromPersistent(installed);
    assert(runtime.security == RuntimeSecurity::Protected && !runtime.setupAp);
  }

  // A malformed journal or a protected state beside a pending FIRST BOOT
  // journal fails closed instead of overwriting an unrelated configuration.
  {
    DurableState corrupt;
    corrupt.journal.present = true;
    corrupt.journal.bytes.assign(kJournalRecordSize, 0x5a);
    FakeBackend backend(corrupt);
    Transaction recovery(backend);
    assert(recovery.recover() == RecoveryStatus::Failed);
    assert(corrupt.journal.present);
  }
  // Unknown journal versions and partial records remain blocked even when an
  // attacker or torn write supplies an otherwise matching CRC.
  {
    DurableState pending;
    FakeBackend interrupted(pending);
    interrupted.failWifiWriteCall = 1;
    interrupted.failHardwareErase = true;
    Transaction transaction(interrupted);
    assert(transaction.commit(candidate(), true) == Result::RollbackFailed);
    assert(pending.journal.bytes.size() == kJournalRecordSize);

    DurableState unknownVersion = pending;
    unknownVersion.journal.bytes[4] = 0x7f;
    const uint32_t crc = wifi_config_atomic::recordCrc32(
        unknownVersion.journal.bytes.data(), 102U);
    writeLe32(unknownVersion.journal.bytes.data() + 102U, crc);
    FakeBackend versionBackend(unknownVersion);
    Transaction versionRecovery(versionBackend);
    assert(versionRecovery.recover() == RecoveryStatus::Failed);
    assert(unknownVersion.journal.present);

    DurableState partial = pending;
    partial.journal.bytes.pop_back();
    FakeBackend partialBackend(partial);
    Transaction partialRecovery(partialBackend);
    assert(partialRecovery.recover() == RecoveryStatus::Failed);
    assert(partial.journal.present);
  }
  {
    DurableState pending;
    FakeBackend interrupted(pending);
    interrupted.failWifiWriteCall = 1;
    interrupted.failHardwareErase = true;
    Transaction transaction(interrupted);
    assert(transaction.commit(candidate(), true) == Result::RollbackFailed);
    pending.security.present = true;
    pending.security.bytes = protectedSecurityRecord();
    const auto protectedBefore = pending.security.bytes;
    FakeBackend recoveryBackend(pending);
    Transaction recovery(recoveryBackend);
    assert(recovery.recover() == RecoveryStatus::Failed);
    assert(pending.journal.present);
    assert(pending.security.bytes == protectedBefore);
  }

  // The production mutation coordinator is non-blocking: FIRST BOOT owns the
  // shared configuration generation, while parallel hardware, config-restore
  // and duplicate provisioning attempts finish as Busy without changing it.
  {
    using namespace configuration_mutation;
    Coordinator coordinator;
    std::atomic<bool> firstBootEntered{false};
    std::atomic<bool> releaseFirstBoot{false};
    std::atomic<int> durableGeneration{0};
    std::atomic<int> rejected{0};

    std::thread firstBoot([&]() {
      Lease lease(coordinator, Owner::FirstBoot);
      assert(lease.acquired());
      durableGeneration.store(1);
      firstBootEntered.store(true);
      while (!releaseFirstBoot.load()) std::this_thread::yield();
      durableGeneration.store(2);
    });
    while (!firstBootEntered.load()) std::this_thread::yield();

    auto competingMutation = [&](Owner owner) {
      Lease lease(coordinator, owner);
      if (!lease.acquired()) {
        rejected.fetch_add(1);
        return;
      }
      durableGeneration.store(99);
    };
    std::thread hardware(competingMutation, Owner::HardwareApi);
    std::thread restore(competingMutation, Owner::ConfigRestore);
    std::thread duplicate(competingMutation, Owner::FirstBoot);
    hardware.join();
    restore.join();
    duplicate.join();
    assert(rejected.load() == 3);
    assert(durableGeneration.load() == 1);

    releaseFirstBoot.store(true);
    firstBoot.join();
    assert(durableGeneration.load() == 2);
    assert(coordinator.owner() == Owner::None);

    Lease afterFailureOrTimeout(coordinator, Owner::ConfigRestore);
    assert(afterFailureOrTimeout.acquired());
  }

  return 0;
}
