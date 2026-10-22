#ifndef VOXONE_SECURITY_RECOVERY_SESSION_H
#define VOXONE_SECURITY_RECOVERY_SESSION_H
#include "security_recovery_state.h"
namespace voxone { namespace security_recovery {
class RecoverySession {
 public:
  RecoveryTransition begin(KeyState keyState);
  RecoveryTransition activate();
  RecoveryTransition beginCommit();
  RecoveryTransition complete(bool success);
  RecoveryTransition fail();
  RecoveryTransition current() const;
  RecoveryState state() const { return machine_.state(); }
  RecoveryDiagnostic diagnostic() const { return machine_.diagnostic(); }
  bool requestedThisBoot() const { return machine_.requestedThisBoot(); }
  bool live() const;
  bool activated() const;
 private:
  RecoveryStateMachine machine_;
};
} }
#endif
