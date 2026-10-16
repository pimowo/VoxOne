#include "security_recovery_ap.h"

namespace voxone {
namespace security_recovery {

RecoveryApStartResult RecoveryApController::start(uint32_t nowMs) {
  if (session_.state() != RecoveryState::Ready)
    return RecoveryApStartResult::NotRequested;
  if (!platform_.credentialConfigured()) {
    session_.cancel(nowMs);
    return RecoveryApStartResult::CredentialUnavailable;
  }

  const RecoveryTransition activated = session_.activate(nowMs);
  if (activated.to != RecoveryState::Active)
    return RecoveryApStartResult::GateDenied;
  if (!platform_.securityBaselineEligible()) {
    session_.fail(nowMs);
    return RecoveryApStartResult::BaselineRejected;
  }

  // Once ACTIVE is reached, normal services must never start during this boot,
  // even if AP or HTTP startup subsequently fails.
  ownsRuntime_ = true;
  cleaned_ = false;
  restartIssued_ = false;
  if (!platform_.startWifi()) {
    session_.fail(nowMs);
    cleanup();
    return RecoveryApStartResult::WifiFailed;
  }
  if (!platform_.startHttp()) {
    session_.fail(nowMs);
    cleanup();
    return RecoveryApStartResult::HttpFailed;
  }
  running_ = true;
  return RecoveryApStartResult::Active;
}

bool RecoveryApController::service(uint32_t nowMs) {
  if (!ownsRuntime_) return false;
  session_.service(nowMs);
  if (!session_.live()) {
    const bool completed = session_.state() == RecoveryState::Completed;
    cleanup();
    if (completed && !restartIssued_) {
      restartIssued_ = true;
      platform_.restart();
    }
    return true;
  }
  if (running_ && !platform_.serviceHttp(nowMs)) {
    session_.fail(nowMs);
    cleanup();
  }
  return true;
}

void RecoveryApController::cancel(uint32_t nowMs) {
  if (session_.live()) session_.cancel(nowMs);
  if (session_.live()) session_.fail(nowMs);
  if (ownsRuntime_) cleanup();
}

void RecoveryApController::complete(bool success, uint32_t nowMs) {
  if (session_.live()) session_.complete(success, nowMs);
  if (session_.live()) session_.fail(nowMs);
  if (ownsRuntime_) {
    const bool completed = session_.state() == RecoveryState::Completed;
    cleanup();
    if (completed && !restartIssued_) {
      restartIssued_ = true;
      platform_.restart();
    }
  }
}

void RecoveryApController::cleanup() {
  if (cleaned_) return;
  platform_.stopHttp();
  platform_.disconnectClient();
  platform_.stopWifi();
  platform_.clearTemporary();
  running_ = false;
  cleaned_ = true;
}

const char* recoveryApStartResultName(RecoveryApStartResult result) {
  switch (result) {
    case RecoveryApStartResult::NotRequested: return "not_requested";
    case RecoveryApStartResult::CredentialUnavailable:
      return "credential_unavailable";
    case RecoveryApStartResult::GateDenied: return "gate_denied";
    case RecoveryApStartResult::BaselineRejected: return "baseline_rejected";
    case RecoveryApStartResult::WifiFailed: return "wifi_failed";
    case RecoveryApStartResult::HttpFailed: return "http_failed";
    case RecoveryApStartResult::Active: return "active";
  }
  return "unknown";
}

}  // namespace security_recovery
}  // namespace voxone
