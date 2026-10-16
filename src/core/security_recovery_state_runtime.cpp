#include "security_recovery_state_runtime.h"

#include "config_restore_adapter.h"
#include "ota_boot_runtime.h"
#include "update_progress.h"

namespace voxone {
namespace security_recovery {
namespace {
RecoveryImageState mapImageState(ota::RunningImageState state) {
  switch (state) {
    case ota::RunningImageState::Valid:
      return RecoveryImageState::Valid;
    case ota::RunningImageState::PendingVerify:
      return RecoveryImageState::PendingVerify;
    case ota::RunningImageState::Invalid:
      return RecoveryImageState::Invalid;
    case ota::RunningImageState::Aborted:
      return RecoveryImageState::Aborted;
    case ota::RunningImageState::Unknown:
      return RecoveryImageState::Unknown;
  }
  return RecoveryImageState::Unknown;
}
}

BootRecoveryEvaluation evaluateBootRecoveryRequest(KeyState keyState,
                                                    uint32_t nowMs) {
  RecoveryStateMachine machine;
  machine.observeBootKey(keyState, nowMs);
  if (machine.state() != RecoveryState::Requested)
    return {machine.state(), machine.diagnostic(), false};

  machine.dispatch(RecoveryEvent::BeginOtaCheck, nowMs);
  const ota::RunningImageSnapshot otaStatus = ota::runningImageSnapshot();
  const config_restore::BootRecoveryInspection configStatus =
      config_restore::inspectBootRecovery();
  const GateConditions conditions = {
      otaStatus.readable,
      mapImageState(otaStatus.state),
      otaStatus.pending,
      updateLockActive(),
      configStatus != config_restore::BootRecoveryInspection::Error,
      configStatus == config_restore::BootRecoveryInspection::Pending,
  };
  machine.evaluateGate(conditions, nowMs);
  return {machine.state(), machine.diagnostic(), true};
}

}  // namespace security_recovery
}  // namespace voxone
