#include "ota_boot_health.h"

namespace voxone {
namespace ota {

void HealthGate::begin(bool pending, bool rollbackAvailable, uint32_t nowMs) {
  pending_ = pending;
  rollbackAvailable_ = rollbackAvailable;
  startedAtMs_ = nowMs;
  stableAtMs_ = nowMs;
  stabilityStarted_ = false;
  reason_ = pending && !rollbackAvailable
      ? Failure::RollbackUnavailable : Failure::None;
  phase_ = !pending ? Phase::NotPending
      : (rollbackAvailable ? Phase::Waiting : Phase::RollbackUnavailable);
}

Failure HealthGate::waitingReason(const Evidence& evidence) const {
  if (!evidence.setupComplete) return Failure::RuntimeService;
  if (!evidence.configurationReady) return Failure::Configuration;
  if (!evidence.dataReady) return Failure::DataPartition;
  if (!evidence.runtimeReady) return Failure::RuntimeService;
  if (evidence.networkRequired && !evidence.networkReady)
    return Failure::Network;
  if (evidence.networkRequired && !evidence.httpApiReady)
    return Failure::HttpApi;
  return Failure::None;
}

Action HealthGate::reject(Failure reason) {
  reason_ = reason;
  stabilityStarted_ = false;
  if (!rollbackAvailable_) {
    phase_ = Phase::RollbackUnavailable;
    reason_ = Failure::RollbackUnavailable;
    return Action::None;
  }
  phase_ = Phase::RollbackRequested;
  return Action::Rollback;
}

Action HealthGate::evaluate(const Evidence& evidence, uint32_t nowMs) {
  if (!pending_ || phase_ == Phase::Validated ||
      phase_ == Phase::RollbackRequested ||
      phase_ == Phase::RollbackUnavailable ||
      phase_ == Phase::ValidationFailed)
    return Action::None;

  if (evidence.fatalFailure != Failure::None)
    return reject(evidence.fatalFailure);

  const Failure waiting = waitingReason(evidence);
  if (waiting != Failure::None) {
    phase_ = Phase::Waiting;
    reason_ = waiting;
    stabilityStarted_ = false;
  } else if (!stabilityStarted_) {
    stabilityStarted_ = true;
    stableAtMs_ = nowMs;
    phase_ = Phase::Stabilizing;
    reason_ = Failure::None;
  } else if (static_cast<uint32_t>(nowMs - stableAtMs_) >= kHealthStableMs) {
    phase_ = Phase::Validated;
    reason_ = Failure::None;
    return Action::Validate;
  }

  if (static_cast<uint32_t>(nowMs - startedAtMs_) >= kHealthTimeoutMs)
    return reject(Failure::Timeout);
  return Action::None;
}

void HealthGate::actionFailed(Action action) {
  if (action == Action::Validate) {
    phase_ = Phase::ValidationFailed;
    reason_ = Failure::MarkValid;
  } else if (action == Action::Rollback) {
    phase_ = Phase::RollbackUnavailable;
    reason_ = Failure::RollbackFailed;
  }
}

Snapshot HealthGate::snapshot(uint32_t nowMs) const {
  return {phase_, reason_, pending_, rollbackAvailable_,
          static_cast<uint32_t>(nowMs - startedAtMs_)};
}

const char* phaseName(Phase phase) {
  switch (phase) {
    case Phase::NotPending: return "not_pending";
    case Phase::Waiting: return "waiting";
    case Phase::Stabilizing: return "stabilizing";
    case Phase::Validated: return "validated";
    case Phase::RollbackRequested: return "rollback_requested";
    case Phase::RollbackUnavailable: return "rollback_unavailable";
    case Phase::ValidationFailed: return "validation_failed";
  }
  return "unknown";
}

const char* failureName(Failure failure) {
  switch (failure) {
    case Failure::None: return "none";
    case Failure::OtaStateRead: return "ota_state_read";
    case Failure::ConfigRestore: return "config_restore";
    case Failure::Configuration: return "configuration";
    case Failure::DataPartition: return "data_partition";
    case Failure::RuntimeService: return "runtime_service";
    case Failure::Network: return "network";
    case Failure::HttpApi: return "http_api";
    case Failure::Timeout: return "timeout";
    case Failure::MarkValid: return "mark_valid";
    case Failure::RollbackUnavailable: return "rollback_unavailable";
    case Failure::RollbackFailed: return "rollback_failed";
  }
  return "unknown";
}

}  // namespace ota
}  // namespace voxone
