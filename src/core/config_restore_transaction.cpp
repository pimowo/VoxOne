#include "config_restore_transaction.h"

namespace voxone {
namespace config_restore {

bool TransactionStorage::begin() {
  Phase phase = Phase::Prepared;
  const MarkerStatus marker = backend_.readMarker(phase);
  if (marker != MarkerStatus::None) return false;
  return backend_.cleanupStale();
}

bool TransactionStorage::stage(const config_backup::Data& candidate) {
  // Neither operation changes active configuration. The marker is made
  // durable only after both candidate staging and rollback snapshot verify.
  return backend_.stageCandidate(candidate) &&
         backend_.createRollbackSnapshot() &&
         backend_.writeMarker(Phase::Prepared);
}

Step TransactionStorage::commitStep() {
  Phase phase = Phase::Prepared;
  if (backend_.readMarker(phase) != MarkerStatus::Pending) return Step::Failed;
  switch (phase) {
    case Phase::Prepared:
      return backend_.apply(Component::Config) &&
                     backend_.writeMarker(Phase::ConfigWritten)
                 ? Step::InProgress : Step::Failed;
    case Phase::ConfigWritten:
      return backend_.apply(Component::Hardware) &&
                     backend_.writeMarker(Phase::HardwareWritten)
                 ? Step::InProgress : Step::Failed;
    case Phase::HardwareWritten:
      return backend_.apply(Component::Mqtt) &&
                     backend_.writeMarker(Phase::MqttWritten)
                 ? Step::InProgress : Step::Failed;
    case Phase::MqttWritten:
      return backend_.apply(Component::Ui) &&
                     backend_.writeMarker(Phase::UiWritten)
                 ? Step::InProgress : Step::Failed;
    case Phase::UiWritten:
      return backend_.apply(Component::Scroll) &&
                     backend_.writeMarker(Phase::ScrollWritten)
                 ? Step::InProgress : Step::Failed;
    case Phase::ScrollWritten:
      return backend_.apply(Component::Wifi) &&
                     backend_.writeMarker(Phase::WifiWritten)
                 ? Step::InProgress : Step::Failed;
    case Phase::WifiWritten:
      return backend_.apply(Component::Stations) &&
                     backend_.writeMarker(Phase::StationsWritten)
                 ? Step::InProgress : Step::Failed;
    case Phase::StationsWritten:
      return backend_.rebuildStationsIndex() &&
                     backend_.writeMarker(Phase::IndexRebuilt)
                 ? Step::InProgress : Step::Failed;
    case Phase::IndexRebuilt:
      return backend_.verifyCandidate() &&
                     backend_.writeMarker(Phase::Verified)
                 ? Step::InProgress : Step::Failed;
    case Phase::Verified:
      if (!backend_.clearMarker()) return Step::Failed;
      // Once the marker is durably absent, the new complete configuration is
      // authoritative. Stale cleanup can safely be retried on a later boot.
      backend_.cleanupStale();
      return Step::Complete;
  }
  return Step::Failed;
}

bool TransactionStorage::commit() {
  for (;;) {
    const Step step = commitStep();
    if (step == Step::Complete) return true;
    if (step == Step::Failed) return false;
  }
}

bool TransactionStorage::rollback() {
  return rollbackSucceeded_ = recover();
}

bool TransactionStorage::recover() {
  Phase phase = Phase::Prepared;
  const MarkerStatus marker = backend_.readMarker(phase);
  if (marker == MarkerStatus::None) return backend_.cleanupStale();
  if (marker == MarkerStatus::Error) return false;
  if (!backend_.restoreRollbackSnapshot() ||
      !backend_.verifyRollbackSnapshot() || !backend_.clearMarker()) return false;
  backend_.cleanupStale();
  return true;
}

}  // namespace config_restore
}  // namespace voxone
