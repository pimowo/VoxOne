#include "../src/core/ota_boot_health.h"

#include <assert.h>
#include <string.h>

using namespace voxone::ota;

static Evidence healthy(bool networkRequired = true) {
  Evidence evidence;
  evidence.setupComplete = true;
  evidence.configurationReady = true;
  evidence.dataReady = true;
  evidence.runtimeReady = true;
  evidence.networkRequired = networkRequired;
  evidence.networkReady = networkRequired;
  evidence.httpApiReady = networkRequired;
  return evidence;
}

int main() {
  HealthGate normal;
  normal.begin(false, false, 10);
  assert(normal.evaluate(healthy(), 200000) == Action::None);
  assert(normal.snapshot(200000).phase == Phase::NotPending);

  HealthGate pending;
  pending.begin(true, true, 100);
  Evidence evidence;
  assert(pending.evaluate(evidence, 100) == Action::None);
  assert(pending.snapshot(100).reason == Failure::RuntimeService);
  evidence = healthy();
  assert(pending.evaluate(evidence, 1000) == Action::None);
  assert(pending.snapshot(1000).phase == Phase::Stabilizing);
  assert(pending.evaluate(evidence, 5999) == Action::None);
  assert(pending.evaluate(evidence, 6000) == Action::Validate);
  assert(pending.snapshot(6000).phase == Phase::Validated);
  assert(pending.evaluate(evidence, 7000) == Action::None);

  // A transient loss resets the entire stability interval.
  HealthGate transient;
  transient.begin(true, true, 0);
  assert(transient.evaluate(healthy(), 100) == Action::None);
  Evidence offline = healthy();
  offline.networkReady = false;
  assert(transient.evaluate(offline, 3000) == Action::None);
  assert(transient.snapshot(3000).reason == Failure::Network);
  assert(transient.evaluate(healthy(), 4000) == Action::None);
  assert(transient.evaluate(healthy(), 8999) == Action::None);
  assert(transient.evaluate(healthy(), 9000) == Action::Validate);

  HealthGate timeout;
  timeout.begin(true, true, 50);
  assert(timeout.evaluate(offline, 50 + kHealthTimeoutMs - 1) == Action::None);
  assert(timeout.evaluate(offline, 50 + kHealthTimeoutMs) == Action::Rollback);
  Snapshot timedOut = timeout.snapshot(50 + kHealthTimeoutMs);
  assert(timedOut.phase == Phase::RollbackRequested);
  assert(timedOut.reason == Failure::Timeout);

  HealthGate corrupt;
  corrupt.begin(true, true, 0);
  Evidence fatal = healthy();
  fatal.fatalFailure = Failure::ConfigRestore;
  assert(corrupt.evaluate(fatal, 1) == Action::Rollback);
  assert(corrupt.snapshot(1).reason == Failure::ConfigRestore);

  // Never invalidate the only bootable slot. It stays running and exposes a
  // blocker for diagnostics/preflight instead of entering a reboot loop.
  HealthGate noFallback;
  noFallback.begin(true, false, 0);
  assert(noFallback.evaluate(healthy(), kHealthStableMs) == Action::None);
  assert(noFallback.snapshot(kHealthStableMs).phase == Phase::RollbackUnavailable);
  assert(noFallback.evaluate(fatal, 1) == Action::None);
  assert(noFallback.snapshot(1).phase == Phase::RollbackUnavailable);
  assert(noFallback.snapshot(1).reason == Failure::RollbackUnavailable);

  HealthGate offlineProfile;
  offlineProfile.begin(true, true, 0);
  Evidence local = healthy(false);
  local.networkReady = false;
  local.httpApiReady = false;
  assert(offlineProfile.evaluate(local, 0) == Action::None);
  assert(offlineProfile.evaluate(local, kHealthStableMs) == Action::Validate);

  HealthGate markFailure;
  markFailure.begin(true, true, 0);
  assert(markFailure.evaluate(healthy(), 0) == Action::None);
  assert(markFailure.evaluate(healthy(), kHealthStableMs) == Action::Validate);
  markFailure.actionFailed(Action::Validate);
  assert(markFailure.snapshot(kHealthStableMs).phase == Phase::ValidationFailed);
  assert(markFailure.snapshot(kHealthStableMs).reason == Failure::MarkValid);

  HealthGate rollbackFailure;
  rollbackFailure.begin(true, true, 0xfffffff0u);
  assert(rollbackFailure.evaluate(offline, 0xfffffff0u + kHealthTimeoutMs) ==
         Action::Rollback);  // uint32_t wrap is intentional.
  rollbackFailure.actionFailed(Action::Rollback);
  assert(rollbackFailure.snapshot(1).reason == Failure::RollbackFailed);

  assert(strcmp(phaseName(Phase::Stabilizing), "stabilizing") == 0);
  assert(strcmp(failureName(Failure::DataPartition), "data_partition") == 0);
}
