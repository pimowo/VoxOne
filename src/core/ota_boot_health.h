#ifndef VOXONE_OTA_BOOT_HEALTH_H
#define VOXONE_OTA_BOOT_HEALTH_H

#include <stdint.h>

namespace voxone {
namespace ota {

// MAIN OTA is the only path which produces PENDING_VERIFY in VoxOne. A
// pending image therefore has to restore its internal services and one legal
// local control plane (validated STA or Recovery AP) before it may replace the
// last known-good slot. Internet, radio-station and browser availability are
// deliberately not health conditions. USB/recovery flashes do not enter this
// state and are outside this policy.
constexpr uint32_t kHealthTimeoutMs = 180000;
constexpr uint32_t kHealthStableMs = 5000;

enum class Phase : uint8_t {
  NotPending,
  Waiting,
  Stabilizing,
  Validated,
  RollbackRequested,
  RollbackUnavailable,
  ValidationFailed,
};

enum class Failure : uint8_t {
  None,
  OtaStateRead,
  ConfigRestore,
  Configuration,
  DataPartition,
  RuntimeService,
  Network,
  HttpApi,
  Timeout,
  MarkValid,
  RollbackUnavailable,
  RollbackFailed,
};

enum class Action : uint8_t { None, Validate, Rollback };

struct Evidence {
  bool setupComplete = false;
  bool configurationReady = false;
  bool dataReady = false;
  bool runtimeReady = false;
  bool controlPlaneReady = false;
  Failure fatalFailure = Failure::None;
};

struct Snapshot {
  Phase phase;
  Failure reason;
  bool pending;
  bool rollbackAvailable;
  uint32_t elapsedMs;
};

inline bool bootControlPlaneReady(bool httpServerReady,
                                  bool stationNetworkReady,
                                  bool recoveryApReady) {
  return httpServerReady && (stationNetworkReady || recoveryApReady);
}

inline bool bootHealthAllowsUpdate(bool stateReadOk, bool pending,
                                   Phase phase) {
  return stateReadOk && !pending &&
      (phase == Phase::NotPending || phase == Phase::Validated);
}

class HealthGate {
 public:
  void begin(bool pending, bool rollbackAvailable, uint32_t nowMs);
  Action evaluate(const Evidence& evidence, uint32_t nowMs);
  Action validationFailed(bool rollbackAvailable);
  void rollbackBecameUnavailable();
  void actionFailed(Action action);
  Snapshot snapshot(uint32_t nowMs) const;

 private:
  Failure waitingReason(const Evidence& evidence) const;
  Action reject(Failure reason);

  Phase phase_ = Phase::NotPending;
  Failure reason_ = Failure::None;
  bool pending_ = false;
  bool rollbackAvailable_ = false;
  bool stabilityStarted_ = false;
  uint32_t startedAtMs_ = 0;
  uint32_t stableAtMs_ = 0;
};

const char* phaseName(Phase phase);
const char* failureName(Failure failure);

}  // namespace ota
}  // namespace voxone

#endif
