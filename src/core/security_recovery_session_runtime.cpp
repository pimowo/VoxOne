#include "security_recovery_session_runtime.h"

#include "security_recovery_state_runtime.h"

namespace voxone {
namespace security_recovery {
namespace {

class ProductionGateSource : public RecoveryGateSource {
 public:
  GateConditions read() override { return readRecoveryGateConditions(); }
};

ProductionGateSource gateSource;
RecoverySession session(gateSource);

RecoverySessionSnapshot snapshot() {
  return {session.state(), session.diagnostic(), session.requestedThisBoot(),
          session.live(), session.activated()};
}

}  // namespace

RecoverySessionSnapshot beginRecoverySession(KeyState keyState,
                                             uint32_t nowMs) {
  // RecoverySession reads the production gate only after an accepted physical
  // boot gesture. Other outcomes do not inspect NVS/OTA status.
  session.begin(keyState, nowMs);
  return snapshot();
}

void serviceRecoverySession(uint32_t nowMs) {
  session.service(nowMs);
}

RecoverySessionSnapshot recoverySessionSnapshot() { return snapshot(); }

RecoveryTransition activateRecoverySession(uint32_t nowMs) {
  return session.activate(nowMs);
}

RecoveryTransition awaitRecoveryConfirmation(uint32_t nowMs) {
  return session.awaitConfirmation(nowMs);
}

RecoveryTransition confirmRecoveryKey(uint32_t nowMs) {
  return session.confirm(nowMs);
}

RecoveryTransition beginRecoveryCommit(uint32_t nowMs) {
  return session.beginCommit(nowMs);
}

RecoveryTransition cancelRecoverySession(uint32_t nowMs) {
  return session.cancel(nowMs);
}

RecoveryTransition failRecoverySession(uint32_t nowMs) {
  return session.fail(nowMs);
}

RecoveryTransition completeRecoverySession(bool success, uint32_t nowMs) {
  return session.complete(success, nowMs);
}

}  // namespace security_recovery
}  // namespace voxone
