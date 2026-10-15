#include "../src/core/ota_boot_health.h"

#include <assert.h>
#include <string.h>

using namespace voxone::ota;

static Evidence healthy() {
  Evidence evidence;
  evidence.setupComplete = true;
  evidence.configurationReady = true;
  evidence.dataReady = true;
  evidence.runtimeReady = true;
  evidence.controlPlaneReady = true;
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
  assert(!bootHealthAllowsUpdate(true, true, pending.snapshot(100).phase));
  evidence = healthy();
  assert(pending.evaluate(evidence, 1000) == Action::None);
  assert(pending.snapshot(1000).phase == Phase::Stabilizing);
  assert(pending.evaluate(evidence, 5999) == Action::None);
  assert(pending.evaluate(evidence, 6000) == Action::Validate);
  assert(pending.snapshot(6000).phase == Phase::Validated);
  // Gate phase changes before the IDF mark-valid call returns. Mutating
  // updates stay blocked until runtime also clears the durable pending flag.
  assert(!bootHealthAllowsUpdate(true, true, pending.snapshot(6000).phase));
  assert(bootHealthAllowsUpdate(true, false, pending.snapshot(6000).phase));
  assert(pending.evaluate(evidence, 7000) == Action::None);

  // A control-plane startup failure resets the entire stability interval.
  // Internet and remote browser availability are intentionally absent from
  // Evidence and therefore cannot block an otherwise healthy legacy boot.
  HealthGate transient;
  transient.begin(true, true, 0);
  assert(transient.evaluate(healthy(), 100) == Action::None);
  Evidence offline = healthy();
  offline.controlPlaneReady = false;
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

  // Both a validated local STA and Recovery AP are legal autonomous control
  // planes. A failed AP/STA startup is not.
  assert(bootControlPlaneReady(true, true, false));
  assert(bootControlPlaneReady(true, false, true));
  assert(!bootControlPlaneReady(true, false, false));
  assert(!bootControlPlaneReady(false, true, true));

  HealthGate recoveryAp;
  recoveryAp.begin(true, true, 0);
  Evidence local = healthy();
  local.controlPlaneReady = bootControlPlaneReady(true, false, true);
  assert(recoveryAp.evaluate(local, 0) == Action::None);
  assert(recoveryAp.evaluate(local, kHealthStableMs) == Action::Validate);

  // A0 -> L0 starts with legacy SPIFFS and no /api/ready caller. These
  // external conditions deliberately are not Evidence inputs; every context
  // below must follow the same internal transition to VALID.
  struct ExternalContext {
    const char* name;
    bool currentWww;
    bool browserPresent;
    bool internetAvailable;
    bool radioStationAvailable;
  };
  const ExternalContext migrationContexts[] = {
      {"legacy-spiffs-no-browser", false, false, true, true},
      {"current-spiffs-with-probe", true, true, true, true},
      {"offline-recovery-ap", true, false, false, false},
      {"internet-without-radio-station", true, false, true, false},
  };
  for (const auto& context : migrationContexts) {
    assert(context.name[0] != '\0');
    (void)context.currentWww;
    (void)context.browserPresent;
    (void)context.internetAvailable;
    (void)context.radioStationAvailable;
    HealthGate migration;
    migration.begin(true, true, 100);
    Evidence boot = healthy();
    assert(migration.evaluate(boot, 100) == Action::None);
    assert(migration.snapshot(100).phase == Phase::Stabilizing);
    assert(migration.evaluate(boot, 100 + kHealthStableMs) == Action::Validate);
    assert(!bootHealthAllowsUpdate(
        true, true, migration.snapshot(100 + kHealthStableMs).phase));
    assert(bootHealthAllowsUpdate(
        true, false, migration.snapshot(100 + kHealthStableMs).phase));
  }

  // Main-loop loss during the stability window is a real runtime failure and
  // must restart the complete stable interval when service resumes.
  HealthGate heartbeat;
  heartbeat.begin(true, true, 0);
  assert(heartbeat.evaluate(healthy(), 0) == Action::None);
  Evidence stalled = healthy();
  stalled.runtimeReady = false;
  assert(heartbeat.evaluate(stalled, 4000) == Action::None);
  assert(heartbeat.snapshot(4000).reason == Failure::RuntimeService);
  assert(heartbeat.evaluate(healthy(), 5000) == Action::None);
  assert(heartbeat.evaluate(healthy(), 9999) == Action::None);
  assert(heartbeat.evaluate(healthy(), 10000) == Action::Validate);

  // Corrupt SPIFFS/data recovery is reported as fatal by setup and rolls back
  // immediately instead of being approved by elapsed time alone.
  HealthGate damagedData;
  damagedData.begin(true, true, 0);
  Evidence damaged = healthy();
  damaged.dataReady = false;
  damaged.fatalFailure = Failure::DataPartition;
  assert(damagedData.evaluate(damaged, 1) == Action::Rollback);
  assert(damagedData.snapshot(1).reason == Failure::DataPartition);

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

  // Once VALID, the next restart is a normal non-pending boot. Conversely a
  // watchdog/reset before the stable interval never unlocks a second update.
  HealthGate preValidationReset;
  preValidationReset.begin(true, true, 0);
  assert(preValidationReset.evaluate(healthy(), kHealthStableMs - 1) ==
         Action::None);
  assert(!bootHealthAllowsUpdate(
      true, true, preValidationReset.snapshot(kHealthStableMs - 1).phase));
  HealthGate secondBoot;
  secondBoot.begin(false, true, 0);
  assert(secondBoot.evaluate(healthy(), 0) == Action::None);
  assert(secondBoot.snapshot(0).phase == Phase::NotPending);
  assert(bootHealthAllowsUpdate(true, false, secondBoot.snapshot(0).phase));
  assert(!bootHealthAllowsUpdate(false, false, Phase::NotPending));

  assert(strcmp(phaseName(Phase::Stabilizing), "stabilizing") == 0);
  assert(strcmp(failureName(Failure::DataPartition), "data_partition") == 0);
  assert(strcmp(failureName(Failure::HttpApi), "http_api") == 0);
}
