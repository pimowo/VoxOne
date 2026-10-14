#include "../src/core/config_restore_transaction.h"

#include <cassert>
#include <initializer_list>
#include <stdexcept>

using namespace voxone::config_backup;
using namespace voxone::config_restore;

namespace {

struct State {
  int values[7];
};

bool same(const State& a, const State& b) {
  for (unsigned i = 0; i < 7; ++i)
    if (a.values[i] != b.values[i]) return false;
  return true;
}

struct DurableDevice {
  State active{{1, 1, 1, 1, 1, 1, 1}};
  State rollback{};
  State candidate{{2, 2, 2, 2, 2, 2, 2}};
  bool snapshot = false;
  bool staged = false;
  bool marker = false;
  Phase phase = Phase::Prepared;
};

struct PowerCut {};

class FakeBackend : public Backend {
 public:
  explicit FakeBackend(DurableDevice& device) : device_(device) {}

  int cutAt = -1;
  int operation = 0;
  int failAt = -1;
  bool capacityAvailable = true;

  MarkerStatus readMarker(Phase& phase) override {
    if (!device_.marker) return MarkerStatus::None;
    phase = device_.phase;
    return MarkerStatus::Pending;
  }
  bool cleanupStale() override {
    tick();
    if (failed()) return false;
    device_.snapshot = false;
    device_.staged = false;
    return true;
  }
  bool stageCandidate(const Data&) override {
    tick();
    if (failed() || !capacityAvailable) return false;
    device_.staged = true;  // Wi-Fi staged; stations may still fail or power-cut.
    tick();
    if (failed()) return false;
    device_.staged = true;
    return true;
  }
  bool createRollbackSnapshot() override {
    tick();
    if (failed()) return false;
    device_.rollback = device_.active;
    device_.snapshot = true;
    return true;
  }
  bool writeMarker(Phase phase) override {
    tick();
    if (failed()) return false;
    device_.phase = phase;
    device_.marker = true;
    return true;
  }
  bool apply(Component component) override {
    tick();
    if (failed()) return false;
    device_.active.values[static_cast<unsigned>(component)] = 2;
    return true;
  }
  bool rebuildStationsIndex() override {
    tick();
    return !failed();
  }
  bool verifyCandidate() override {
    tick();
    return !failed() && same(device_.active, device_.candidate);
  }
  bool restoreRollbackSnapshot() override {
    tick();
    if (failed() || !device_.snapshot) return false;
    device_.active = device_.rollback;
    return true;
  }
  bool verifyRollbackSnapshot() override {
    tick();
    return !failed() && same(device_.active, device_.rollback);
  }
  bool clearMarker() override {
    tick();
    if (failed()) return false;
    device_.marker = false;
    return true;
  }

 private:
  DurableDevice& device_;
  bool failed() const { return failAt >= 0 && operation == failAt; }
  void tick() {
    ++operation;
    if (cutAt == operation) throw PowerCut{};
  }
};

Data dummyCandidate() { return Data{}; }

void recoverAfterCut(DurableDevice& device, const State& oldState) {
  FakeBackend rebooted(device);
  TransactionStorage recovery(rebooted);
  assert(recovery.recover());
  assert(same(device.active, oldState));
  assert(!device.marker);
}

}  // namespace

int main() {
  // Successful transaction reaches one complete NEW generation.
  {
    DurableDevice device;
    FakeBackend backend(device);
    TransactionStorage transaction(backend);
    assert(transaction.begin());
    assert(transaction.stage(dummyCandidate()));
    assert(transaction.commit());
    assert(same(device.active, device.candidate));
    assert(!device.marker);
  }

  // A valid candidate that cannot be staged is rejected before the durable
  // marker and before any active component changes.
  {
    DurableDevice device;
    FakeBackend backend(device);
    backend.capacityAvailable = false;
    TransactionStorage transaction(backend);
    assert(transaction.begin());
    assert(!transaction.stage(dummyCandidate()));
    assert(transaction.rollback());
    const State oldState{{1, 1, 1, 1, 1, 1, 1}};
    assert(same(device.active, oldState));
    assert(!device.marker && !device.staged && !device.snapshot);
  }

  // Simulated process death at every durable operation. Before the marker the
  // old generation is untouched; with a marker, a new instance rolls it back.
  for (int oldScroll : {0, 1}) {
    for (int cut = 1; cut <= 25; ++cut) {
      DurableDevice device;
      device.active.values[static_cast<unsigned>(Component::Scroll)] = oldScroll;
      const State oldState = device.active;
      FakeBackend backend(device);
      backend.cutAt = cut;
      TransactionStorage transaction(backend);
      try {
        if (transaction.begin() && transaction.stage(dummyCandidate()))
          transaction.commit();
      } catch (const PowerCut&) {
        if (device.marker) recoverAfterCut(device, oldState);
        else {
          FakeBackend rebooted(device);
          TransactionStorage recovery(rebooted);
          const State beforeRecovery = device.active;
          assert(recovery.recover());
          assert(same(device.active, beforeRecovery));
          assert(same(device.active, oldState) ||
                 same(device.active, device.candidate));
          assert(!device.staged && !device.snapshot);
        }
      }
    }
  }

  // Every explicit failure after PREPARE is rolled back; failed recovery keeps
  // the marker so the next boot retries instead of accepting a mixed state.
  for (int failure = 1; failure <= 25; ++failure) {
    DurableDevice device;
    FakeBackend backend(device);
    backend.failAt = failure;
    TransactionStorage transaction(backend);
    const bool began = transaction.begin();
    const bool staged = began && transaction.stage(dummyCandidate());
    if (staged) {
      const bool committed = transaction.commit();
      if (!committed) transaction.rollback();
    } else if (began) transaction.rollback();
    const State oldState{{1, 1, 1, 1, 1, 1, 1}};
    if (device.marker) recoverAfterCut(device, oldState);
    if (failure == 25)
      assert(same(device.active, device.candidate) && !device.marker);
    else
      assert(same(device.active, oldState));
  }

  // A failed recovery never clears the durable marker. A later boot can retry
  // and restore the complete previous generation, including scroll.
  {
    DurableDevice device;
    FakeBackend interrupted(device);
    interrupted.failAt = 10;
    TransactionStorage transaction(interrupted);
    assert(transaction.begin());
    assert(transaction.stage(dummyCandidate()));
    assert(!transaction.commit());
    assert(device.marker);

    FakeBackend failedRecovery(device);
    failedRecovery.failAt = 1;
    TransactionStorage firstBoot(failedRecovery);
    assert(!firstBoot.recover());
    assert(device.marker);

    const State oldState{{1, 1, 1, 1, 1, 1, 1}};
    recoverAfterCut(device, oldState);
  }

  return 0;
}
