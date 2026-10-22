#include "security_recovery_session.h"
namespace voxone { namespace security_recovery {
RecoveryTransition RecoverySession::current() const { return {machine_.state(), machine_.state(), machine_.diagnostic(), false}; }
bool RecoverySession::live() const { return machine_.state() == RecoveryState::Requested || machine_.state() == RecoveryState::Active || machine_.state() == RecoveryState::Committing; }
bool RecoverySession::activated() const { return machine_.state() == RecoveryState::Active || machine_.state() == RecoveryState::Committing; }
RecoveryTransition RecoverySession::begin(KeyState keyState) { return machine_.observeBootKey(keyState); }
RecoveryTransition RecoverySession::activate() { return machine_.dispatch(RecoveryEvent::Activate); }
RecoveryTransition RecoverySession::beginCommit() { return machine_.dispatch(RecoveryEvent::BeginCommit); }
RecoveryTransition RecoverySession::complete(bool success) { return machine_.dispatch(success ? RecoveryEvent::CommitSucceeded : RecoveryEvent::CommitFailed); }
RecoveryTransition RecoverySession::fail() { return machine_.dispatch(RecoveryEvent::InternalFailure); }
} }
