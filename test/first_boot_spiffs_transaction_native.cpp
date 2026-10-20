#include "../src/core/first_boot_transaction.h"
#include "../src/core/first_boot_core.h"
#include "../src/core/wifi_config_atomic.h"

#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace voxone;
using namespace voxone::first_boot_transaction;

namespace {

struct PowerCut {};

enum class FsOperation { Open, Write, Flush, Rename, Remove };

struct FaultPlan {
  int cutAt = -1;
  int failAt = -1;
  bool cutBefore = true;
  int operations = 0;
  std::vector<FsOperation> trace;

  bool begin(FsOperation operation) {
    trace.push_back(operation);
    ++operations;
    if (cutAt == operations && cutBefore) throw PowerCut{};
    return failAt == operations;
  }

  void end() {
    if (cutAt == operations && !cutBefore) throw PowerCut{};
  }
};

struct Record {
  bool present = false;
  std::vector<uint8_t> bytes;
};

struct PersistentState {
  Record journal;
  Record hardware;
  Record security;
  std::map<std::string, std::string> files;
};

class ModeledSpiffsStorage {
 public:
  ModeledSpiffsStorage(std::map<std::string, std::string>& files,
                       FaultPlan* faults)
      : files_(files), faults_(faults) {}

  bool exists(const char* path) const { return files_.count(path) != 0; }

  bool remove(const char* path) {
    const bool fail = stepBegin(FsOperation::Remove);
    if (fail) return false;
    if (!exists(path)) return false;
    files_.erase(path);
    stepEnd();
    return true;
  }

  bool rename(const char* from, const char* to) {
    const bool fail = stepBegin(FsOperation::Rename);
    if (fail) return false;
    if (!exists(from) || exists(to)) return false;
    files_[to] = files_[from];
    files_.erase(from);
    stepEnd();
    return true;
  }

  bool write(const char* path, const uint8_t* bytes, size_t size) {
    if (stepBegin(FsOperation::Open)) return false;
    files_[path] = std::string();
    stepEnd();

    if (size) {
      if (stepBegin(FsOperation::Write)) {
        files_[path] = std::string(
            reinterpret_cast<const char*>(bytes), size > 1U ? size - 1U : 0U);
        return false;
      }
      files_[path] = std::string(reinterpret_cast<const char*>(bytes), size);
      stepEnd();
    }

    if (stepBegin(FsOperation::Flush)) return false;
    stepEnd();
    return true;
  }

  bool matches(const char* path, const uint8_t* bytes, size_t size) const {
    const auto found = files_.find(path);
    if (found == files_.end() || found->second.size() != size) return false;
    return size == 0U ||
           std::memcmp(found->second.data(), bytes, size) == 0;
  }

  bool validBytes(const uint8_t* bytes, size_t size) const {
    return wifi_config::validSerializedConfig(bytes, size);
  }

  bool valid(const char* path) const {
    const auto found = files_.find(path);
    return found != files_.end() &&
           validBytes(reinterpret_cast<const uint8_t*>(found->second.data()),
                      found->second.size());
  }

  bool matchesFingerprint(const char* path, size_t expectedSize,
                          uint32_t expectedCrc) const {
    const auto found = files_.find(path);
    return found != files_.end() && found->second.size() == expectedSize &&
           valid(path) &&
           wifi_config_atomic::recordCrc32(
               reinterpret_cast<const uint8_t*>(found->second.data()),
               found->second.size()) == expectedCrc;
  }

 private:
  bool stepBegin(FsOperation operation) {
    return faults_ && faults_->begin(operation);
  }
  void stepEnd() {
    if (faults_) faults_->end();
  }

  std::map<std::string, std::string>& files_;
  FaultPlan* faults_;
};

class SpiffsTransactionBackend : public Backend {
 public:
  SpiffsTransactionBackend(PersistentState& state, FaultPlan* faults = nullptr)
      : state_(state), faults_(faults) {}

  RecordStatus readJournal(uint8_t* output, size_t capacity,
                           size_t& size) override {
    return read(state_.journal, output, capacity, size);
  }
  bool writeJournal(const uint8_t* input, size_t size) override {
    return write(state_.journal, input, size);
  }
  bool eraseJournal() override { return erase(state_.journal); }

  RecordStatus readHardware(uint8_t* output, size_t capacity,
                            size_t& size) override {
    return read(state_.hardware, output, capacity, size);
  }
  bool writeHardware(const uint8_t* input, size_t size) override {
    return write(state_.hardware, input, size);
  }
  bool eraseHardware() override { return erase(state_.hardware); }

  RecordStatus readWifi(uint8_t* output, size_t capacity,
                        size_t& size) override {
    size = 0;
    const auto found = state_.files.find(wifi_config_atomic::kActivePath);
    if (found == state_.files.end()) return RecordStatus::Missing;
    size = found->second.size();
    if (size > capacity) return RecordStatus::Found;
    if (size) std::memcpy(output, found->second.data(), size);
    return wifi_config::validSerializedConfig(output, size)
               ? RecordStatus::Found
               : RecordStatus::Error;
  }
  bool writeWifi(const uint8_t* input, size_t size) override {
    ModeledSpiffsStorage storage(state_.files, faults_);
    return wifi_config_atomic::replace(storage, input, size);
  }
  bool eraseWifi() override {
    ModeledSpiffsStorage storage(state_.files, faults_);
    return wifi_config_atomic::removeIfPresent(
               storage, wifi_config_atomic::kTemporaryPath) &&
           wifi_config_atomic::removeIfPresent(
               storage, wifi_config_atomic::kRollbackPath) &&
           wifi_config_atomic::removeIfPresent(
               storage, wifi_config_atomic::kActivePath);
  }
  bool rollbackWifi(bool priorEmpty, size_t candidateSize,
                    uint32_t candidateCrc) override {
    ModeledSpiffsStorage storage(state_.files, faults_);
    return wifi_config_atomic::rollbackFirstBoot(
        storage, priorEmpty, candidateSize, candidateCrc);
  }

  RecordStatus readSecurity(uint8_t* output, size_t capacity,
                            size_t& size) override {
    return read(state_.security, output, capacity, size);
  }
  bool completeSecurityOpen(const uint8_t* expected, size_t size) override {
    return write(state_.security, expected, size);
  }
  bool eraseSecurity() override { return erase(state_.security); }

 private:
  static RecordStatus read(const Record& record, uint8_t* output,
                           size_t capacity, size_t& size) {
    size = 0;
    if (!record.present) return RecordStatus::Missing;
    size = record.bytes.size();
    if (size <= capacity && size) std::memcpy(output, record.bytes.data(), size);
    return RecordStatus::Found;
  }
  static bool write(Record& record, const uint8_t* input, size_t size) {
    record.present = true;
    record.bytes.assign(input, input + size);
    return true;
  }
  static bool erase(Record& record) {
    record.present = false;
    record.bytes.clear();
    return true;
  }

  PersistentState& state_;
  FaultPlan* faults_;
};

std::vector<uint8_t> hardwareRecord() {
  const hardware::RuntimeHardwareConfig config{
      hardware::kRuntimeHardwareConfigVersion,
      hardware::RuntimeDisplay::None,
      hardware::RuntimeInput::Buttons3,
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

Candidate candidate() {
  Candidate result{};
  result.writeHardware = true;
  const auto hardware = hardwareRecord();
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

PersistentState initialState(bool priorEmpty) {
  PersistentState state;
  if (priorEmpty) state.files[wifi_config_atomic::kActivePath] = "";
  return state;
}

void assertRolledBack(const PersistentState& state, bool priorEmpty) {
  assert(!state.journal.present);
  assert(!state.hardware.present);
  assert(!state.security.present);
  assert(state.files.count(wifi_config_atomic::kTemporaryPath) == 0U);
  assert(state.files.count(wifi_config_atomic::kRollbackPath) == 0U);
  const auto active = state.files.find(wifi_config_atomic::kActivePath);
  assert((active != state.files.end()) == priorEmpty);
  if (priorEmpty) assert(active->second.empty());
  assert(first_boot::shouldStartOpenSetupAp(0U));
}

void recoverAfterRestart(PersistentState& state, bool priorEmpty) {
  SpiffsTransactionBackend rebooted(state);
  Transaction recovery(rebooted);
  assert(recovery.recover() == RecoveryStatus::Recovered);
  assertRolledBack(state, priorEmpty);
}

std::vector<FsOperation> successfulTrace(bool priorEmpty) {
  PersistentState state = initialState(priorEmpty);
  FaultPlan trace;
  SpiffsTransactionBackend backend(state, &trace);
  Transaction transaction(backend);
  assert(transaction.commit(candidate(), true) == Result::Applied);
  assert(!state.journal.present && state.hardware.present &&
         state.security.present);
  assert(state.files.count(wifi_config_atomic::kTemporaryPath) == 0U);
  assert(state.files.count(wifi_config_atomic::kRollbackPath) == 0U);
  ModeledSpiffsStorage storage(state.files, nullptr);
  assert(wifi_config_atomic::recover(storage));
  const auto active = state.files.find(wifi_config_atomic::kActivePath);
  assert(active != state.files.end() && active->second == "Home\tsecret\n");
  assert(!trace.trace.empty());
  return trace.trace;
}

void testPowerCuts(bool priorEmpty) {
  const auto trace = successfulTrace(priorEmpty);
  for (bool before : {true, false}) {
    for (size_t operation = 1; operation <= trace.size(); ++operation) {
      PersistentState state = initialState(priorEmpty);
      FaultPlan fault;
      fault.cutAt = static_cast<int>(operation);
      fault.cutBefore = before;
      SpiffsTransactionBackend backend(state, &fault);
      try {
        Transaction transaction(backend);
        (void)transaction.commit(candidate(), true);
        assert(false);
      } catch (const PowerCut&) {
        assert(state.journal.present);
      }
      recoverAfterRestart(state, priorEmpty);
    }
  }
}

void testOperationFailures(bool priorEmpty) {
  const auto trace = successfulTrace(priorEmpty);
  for (size_t operation = 1; operation <= trace.size(); ++operation) {
    PersistentState state = initialState(priorEmpty);
    FaultPlan fault;
    fault.failAt = static_cast<int>(operation);
    SpiffsTransactionBackend backend(state, &fault);
    Transaction transaction(backend);
    const Result result = transaction.commit(candidate(), true);
    assert(result != Result::Applied);
    if (state.journal.present) recoverAfterRestart(state, priorEmpty);
    else assertRolledBack(state, priorEmpty);
  }
}

void testRepeatedRecoveryCut() {
  PersistentState pending = initialState(true);
  const auto commitTrace = successfulTrace(true);
  size_t removeOperation = 0;
  for (size_t index = 0; index < commitTrace.size(); ++index) {
    if (commitTrace[index] == FsOperation::Remove) {
      removeOperation = index + 1U;
      break;
    }
  }
  assert(removeOperation != 0U);
  FaultPlan commitCut;
  commitCut.cutAt = static_cast<int>(removeOperation);
  commitCut.cutBefore = true;
  try {
    SpiffsTransactionBackend backend(pending, &commitCut);
    Transaction transaction(backend);
    (void)transaction.commit(candidate(), true);
    assert(false);
  } catch (const PowerCut&) {
    assert(pending.journal.present);
  }

  PersistentState traced = pending;
  FaultPlan recoveryTrace;
  {
    SpiffsTransactionBackend backend(traced, &recoveryTrace);
    Transaction recovery(backend);
    assert(recovery.recover() == RecoveryStatus::Recovered);
  }
  assert(!recoveryTrace.trace.empty());

  for (bool before : {true, false}) {
    for (size_t operation = 1; operation <= recoveryTrace.trace.size();
         ++operation) {
      PersistentState state = pending;
      FaultPlan recoveryCut;
      recoveryCut.cutAt = static_cast<int>(operation);
      recoveryCut.cutBefore = before;
      try {
        SpiffsTransactionBackend backend(state, &recoveryCut);
        Transaction recovery(backend);
        (void)recovery.recover();
        assert(false);
      } catch (const PowerCut&) {
        assert(state.journal.present);
      }
      recoverAfterRestart(state, true);
    }
  }
}

}  // namespace

int main() {
  const auto missingTrace = successfulTrace(false);
  const auto emptyTrace = successfulTrace(true);
  auto has = [](const std::vector<FsOperation>& trace, FsOperation operation) {
    for (FsOperation candidate : trace)
      if (candidate == operation) return true;
    return false;
  };
  assert(has(missingTrace, FsOperation::Open));
  assert(has(missingTrace, FsOperation::Write));
  assert(has(missingTrace, FsOperation::Flush));
  assert(has(missingTrace, FsOperation::Rename));
  assert(has(emptyTrace, FsOperation::Remove));

  testPowerCuts(false);
  testPowerCuts(true);
  testOperationFailures(false);
  testOperationFailures(true);
  testRepeatedRecoveryCut();

  // A non-empty baseline remains outside FIRST BOOT and is never mutated.
  {
    PersistentState state;
    state.files[wifi_config_atomic::kActivePath] = "Legacy\tsecret\n";
    const PersistentState before = state;
    SpiffsTransactionBackend backend(state);
    Transaction transaction(backend);
    assert(transaction.commit(candidate(), true) == Result::BaselineConflict);
    assert(state.files == before.files);
    assert(!state.journal.present && !state.hardware.present &&
           !state.security.present);
  }

  return 0;
}
