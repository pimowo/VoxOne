#ifndef VOXONE_SECURITY_RECOVERY_SESSION_H
#define VOXONE_SECURITY_RECOVERY_SESSION_H

#include "security_recovery_state.h"

namespace voxone {
namespace security_recovery {

class RecoveryGateSource {
 public:
  virtual ~RecoveryGateSource() {}
  virtual GateConditions read() = 0;
};

// RAM-only lifecycle around RecoveryStateMachine. Network and storage adapters
// are deliberately outside this class. The source is read immediately before
// every safety-sensitive transition, so callers cannot reuse a stale snapshot.
class RecoverySession {
 public:
  explicit RecoverySession(
      RecoveryGateSource& gateSource,
      RecoveryTimings timings = kDefaultRecoveryTimings);

  RecoveryTransition begin(KeyState keyState, uint32_t nowMs);
  RecoveryTransition activate(uint32_t nowMs);
  RecoveryTransition awaitConfirmation(uint32_t nowMs);
  RecoveryTransition confirm(uint32_t nowMs);
  RecoveryTransition complete(bool success, uint32_t nowMs);
  RecoveryTransition cancel(uint32_t nowMs);
  RecoveryTransition fail(uint32_t nowMs);
  RecoveryTransition service(uint32_t nowMs);

  RecoveryState state() const { return machine_.state(); }
  RecoveryDiagnostic diagnostic() const { return machine_.diagnostic(); }
  bool requestedThisBoot() const { return machine_.requestedThisBoot(); }
  bool live() const;
  bool activated() const;

 private:
  RecoveryTransition current() const;
  RecoveryGateSource& gateSource_;
  RecoveryStateMachine machine_;
};

}  // namespace security_recovery
}  // namespace voxone

#endif
