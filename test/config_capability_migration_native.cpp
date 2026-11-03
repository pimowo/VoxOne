#include "../src/core/config_capability_migration.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>

using namespace voxone::config_capability_migration;
using S = voxone::config_format::ConfigRecordStatus;

int main() {
  // The ambiguous pre-capability S0 false is enabled exactly once.
  auto plan = planS0BtIntroduction(
      true, true, S::LOADED_V7, false, MarkerStatus::Missing);
  assert(plan.enableForThisBoot && plan.persistEnabled &&
         plan.markAppliedAfterPersistence);

  // An already-true/default or legacy record only needs the durable marker.
  plan = planS0BtIntroduction(
      true, true, S::LOADED_V7, true, MarkerStatus::Missing);
  assert(plan.enableForThisBoot && !plan.persistEnabled &&
         plan.markAppliedAfterPersistence);
  for (S status : {S::MIGRATED_V5, S::MIGRATED_V6,
                   S::DEFAULTS_REQUIRED, S::INVALID_CRC}) {
    plan = planS0BtIntroduction(
        true, true, status, true, MarkerStatus::Missing);
    assert(plan.enableForThisBoot && !plan.persistEnabled &&
           plan.markAppliedAfterPersistence);
  }

  // Once marked, a later explicit false or a restored VXCF false is final.
  plan = planS0BtIntroduction(
      true, true, S::LOADED_V7, false, MarkerStatus::Applied);
  assert(!plan.enableForThisBoot && !plan.persistEnabled &&
         !plan.markAppliedAfterPersistence);

  // No guessing if the marker cannot be trusted or persistence is read-only.
  for (MarkerStatus marker : {MarkerStatus::Unavailable,
                              MarkerStatus::Invalid}) {
    plan = planS0BtIntroduction(true, true, S::LOADED_V7, false, marker);
    assert(!plan.enableForThisBoot && !plan.persistEnabled &&
           !plan.markAppliedAfterPersistence);
  }
  for (S status : {S::UNSUPPORTED_NEWER, S::INVALID_ARGUMENT}) {
    plan = planS0BtIntroduction(
        true, true, status, false, MarkerStatus::Missing);
    assert(!plan.enableForThisBoot && !plan.persistEnabled &&
           !plan.markAppliedAfterPersistence);
  }

  // L0 and non-BT profiles never participate in the S0 introduction marker.
  plan = planS0BtIntroduction(
      false, true, S::LOADED_V7, false, MarkerStatus::Missing);
  assert(!plan.enableForThisBoot && !plan.persistEnabled &&
         !plan.markAppliedAfterPersistence);
  plan = planS0BtIntroduction(
      true, false, S::LOADED_V7, false, MarkerStatus::Missing);
  assert(!plan.enableForThisBoot && !plan.persistEnabled &&
         !plan.markAppliedAfterPersistence);

  std::puts("PASS config_capability_migration_native");
}
