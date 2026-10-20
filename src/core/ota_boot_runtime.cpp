#include "ota_boot_runtime.h"

#include "version.h"

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <nvs.h>
#include <atomic>
#include <cstdio>
#include <cstring>

#ifndef CONFIG_APP_ROLLBACK_ENABLE
#error "VoxOne OTA health gate requires ESP-IDF application rollback"
#endif

// Arduino-ESP32 declares this weakly in esp32-hal-misc.c. The strong project
// symbol prevents initArduino() from accepting PENDING_VERIFY before setup().
extern "C" bool verifyRollbackLater() { return true; }

namespace voxone {
namespace ota {
namespace {

static_assert(static_cast<uint32_t>(ESP_OTA_IMG_NEW) == 0U,
              "ESP-IDF OTA state ABI changed");
static_assert(static_cast<uint32_t>(ESP_OTA_IMG_PENDING_VERIFY) == 1U,
              "ESP-IDF OTA state ABI changed");
static_assert(static_cast<uint32_t>(ESP_OTA_IMG_VALID) == 2U,
              "ESP-IDF OTA state ABI changed");
static_assert(static_cast<uint32_t>(ESP_OTA_IMG_INVALID) == 3U,
              "ESP-IDF OTA state ABI changed");
static_assert(static_cast<uint32_t>(ESP_OTA_IMG_ABORTED) == 4U,
              "ESP-IDF OTA state ABI changed");
static_assert(static_cast<uint32_t>(ESP_OTA_IMG_UNDEFINED) == UINT32_MAX,
              "ESP-IDF OTA undefined sentinel changed");

constexpr char kNvsNamespace[] = "vox_ota";
constexpr char kNvsLastFailure[] = "last_fail";
constexpr uint32_t kMonitorPeriodMs = 200;
constexpr uint32_t kMonitorStack = 4096;

enum EvidenceBit : uint32_t {
  SetupComplete = 1u << 0,
  ConfigurationReady = 1u << 1,
  DataReady = 1u << 2,
  RuntimeReady = 1u << 3,
  ControlPlaneReady = 1u << 4,
};

HealthGate gate;
std::atomic<uint32_t> evidenceBits{0};
std::atomic<uint8_t> fatalFailure{static_cast<uint8_t>(Failure::None)};
std::atomic<uint8_t> publishedPhase{static_cast<uint8_t>(Phase::NotPending)};
std::atomic<uint8_t> publishedReason{static_cast<uint8_t>(Failure::None)};
std::atomic<uint32_t> publishedElapsed{0};
std::atomic<bool> pendingImage{false};
std::atomic<bool> validationRequiredImage{false};
std::atomic<bool> rollbackAvailable{false};
std::atomic<bool> stateReadOk{false};
std::atomic<uint8_t> imageStateReadStatus{
    static_cast<uint8_t>(OtaStateReadStatus::Unreadable)};
std::atomic<uint32_t> imageState{
    static_cast<uint32_t>(ESP_OTA_IMG_UNDEFINED)};
std::atomic<uint8_t> lastFailure{static_cast<uint8_t>(Failure::None)};
std::atomic<uint32_t> lastServiceMs{0};
std::atomic<bool> httpApiProbed{false};
char runningSlot[17] = "unknown";
char bootSlot[17] = "unknown";

void copyLabel(char* output, size_t capacity, const esp_partition_t* partition) {
  if (!output || !capacity) return;
  if (!partition) {
    std::snprintf(output, capacity, "unknown");
    return;
  }
  std::snprintf(output, capacity, "%s", partition->label);
}

OtaStateReadStatus otaStateReadStatus(esp_err_t result) {
  if (result == ESP_OK) return OtaStateReadStatus::Readable;
  if (result == ESP_ERR_NOT_FOUND) return OtaStateReadStatus::Missing;
  return OtaStateReadStatus::Unreadable;
}

OtaStateReadStatus currentImageStateReadStatus() {
  return static_cast<OtaStateReadStatus>(imageStateReadStatus.load());
}

void loadLastFailure() {
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return;
  uint8_t value = static_cast<uint8_t>(Failure::None);
  if (nvs_get_u8(handle, kNvsLastFailure, &value) == ESP_OK &&
      value <= static_cast<uint8_t>(Failure::BootPartitionMismatch))
    lastFailure.store(value);
  nvs_close(handle);
}

bool saveLastFailure(Failure failure) {
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  const esp_err_t set = nvs_set_u8(
      handle, kNvsLastFailure, static_cast<uint8_t>(failure));
  const esp_err_t commit = set == ESP_OK ? nvs_commit(handle) : set;
  nvs_close(handle);
  if (commit == ESP_OK) lastFailure.store(static_cast<uint8_t>(failure));
  return commit == ESP_OK;
}

Evidence currentEvidence(uint32_t nowMs) {
  const uint32_t bits = evidenceBits.load();
  Evidence evidence;
  evidence.setupComplete = (bits & SetupComplete) != 0;
  evidence.configurationReady = (bits & ConfigurationReady) != 0;
  evidence.dataReady = (bits & DataReady) != 0;
  evidence.runtimeReady = (bits & RuntimeReady) != 0 &&
      static_cast<uint32_t>(nowMs - lastServiceMs.load()) <= 1000;
  evidence.controlPlaneReady = (bits & ControlPlaneReady) != 0;
#if defined(VOXONE_OTA_HIL_ROLLBACK_AFTER_PROBE)
  // The dedicated destructive HIL image deliberately remains pending until
  // its HTTP probe arrives and injects the test failure below. Production
  // profiles never take this branch.
  if (!httpApiProbed.load()) evidence.controlPlaneReady = false;
#endif
  evidence.fatalFailure = static_cast<Failure>(fatalFailure.load());
  return evidence;
}

void publish(uint32_t nowMs) {
  const Snapshot snapshot = gate.snapshot(nowMs);
  publishedPhase.store(static_cast<uint8_t>(snapshot.phase));
  publishedReason.store(static_cast<uint8_t>(snapshot.reason));
  publishedElapsed.store(snapshot.elapsedMs);
}

void logSnapshot(const char* event, uint32_t nowMs) {
  const Snapshot snapshot = gate.snapshot(nowMs);
  Serial.printf(
      "##[OTA-HEALTH]# event=%s running=%s boot=%s image=%s phase=%s "
      "reason=%s rollback=%u elapsedMs=%lu\n",
      event, runningSlot, bootSlot,
      otaImageStateName(currentImageStateReadStatus(), imageState.load()),
      phaseName(snapshot.phase), failureName(snapshot.reason),
      snapshot.rollbackAvailable ? 1U : 0U,
      static_cast<unsigned long>(snapshot.elapsedMs));
}

void monitorTask(void*) {
  for (;;) {
    const uint32_t nowMs = millis();
    const Action action = gate.evaluate(currentEvidence(nowMs), nowMs);
    publish(nowMs);
    if (action == Action::DiagnosticHold) {
      const Failure reason = gate.snapshot(nowMs).reason;
      saveLastFailure(reason);
      logSnapshot("initial-validation-hold", nowMs);
      Serial.println(
          "##[OTA-HEALTH]# BLOCKED: initial image validation failed; "
          "keep running for diagnostics and USB recovery");
      vTaskDelete(nullptr);
      return;
    }
    if (action == Action::Validate) {
      const esp_partition_t* running = esp_ota_get_running_partition();
      const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
      esp_ota_img_states_t confirmedState = ESP_OTA_IMG_UNDEFINED;
      const esp_err_t confirmResult = running
          ? esp_ota_get_state_partition(running, &confirmedState)
          : ESP_ERR_INVALID_ARG;
      const bool canRollback = esp_ota_check_rollback_is_possible();
      rollbackAvailable.store(canRollback);
      const MarkValidRecovery recovery = markValidRecovery(
          result == ESP_OK, confirmResult == ESP_OK,
          durableImageState(static_cast<uint32_t>(confirmedState)), canRollback);
      if (recovery == MarkValidRecovery::ConfirmedValid) {
        imageStateReadStatus.store(
            static_cast<uint8_t>(OtaStateReadStatus::Readable));
        imageState.store(static_cast<uint32_t>(ESP_OTA_IMG_VALID));
        pendingImage.store(false);
        validationRequiredImage.store(false);
        logSnapshot("validated", nowMs);
        vTaskDelete(nullptr);
        return;
      }
      const Action failureAction = gate.validationFailed(canRollback);
      publish(nowMs);
      saveLastFailure(Failure::MarkValid);
      Serial.printf(
          "##[OTA-HEALTH]# mark-valid not confirmed: call=%d read=%d "
          "state=%s rollback=%u\n",
          static_cast<int>(result), static_cast<int>(confirmResult),
          otaImageStateName(otaStateReadStatus(confirmResult),
                            static_cast<uint32_t>(confirmedState)),
          canRollback ? 1U : 0U);
      logSnapshot("mark-valid-failed", nowMs);
      if (failureAction == Action::Rollback) {
        Serial.println(
            "##[OTA-HEALTH]# rolling back after unconfirmed mark-valid");
        Serial.flush();
        const esp_err_t rollbackResult =
            esp_ota_mark_app_invalid_rollback_and_reboot();
        (void)rollbackResult;  // Success does not return.
        gate.actionFailed(Action::Rollback);
        publish(nowMs);
        saveLastFailure(Failure::RollbackFailed);
        logSnapshot("mark-valid-rollback-failed", nowMs);
      } else {
        Serial.println(
            "##[OTA-HEALTH]# BLOCKED: mark-valid failed without a safe "
            "rollback image; keep running for diagnostics and USB recovery");
      }
      vTaskDelete(nullptr);
      return;
    }
    if (action == Action::Rollback) {
      const Failure reason = gate.snapshot(nowMs).reason;
      if (!saveLastFailure(reason))
        Serial.println("##[OTA-HEALTH]# could not persist rollback reason");
      const bool canRollback = esp_ota_check_rollback_is_possible();
      rollbackAvailable.store(canRollback);
      if (!canRollback) {
        gate.rollbackBecameUnavailable();
        publish(nowMs);
        logSnapshot("rollback-candidate-lost", nowMs);
        Serial.println(
            "##[OTA-HEALTH]# BLOCKED: rollback candidate is no longer "
            "bootable; keep running for diagnostics and USB recovery");
        vTaskDelete(nullptr);
        return;
      }
      logSnapshot("rollback", nowMs);
      Serial.flush();
      const esp_err_t result = esp_ota_mark_app_invalid_rollback_and_reboot();
      (void)result;  // Success does not return.
      gate.actionFailed(action);
      publish(nowMs);
      saveLastFailure(Failure::RollbackFailed);
      logSnapshot("rollback-failed", nowMs);
      vTaskDelete(nullptr);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(kMonitorPeriodMs));
  }
}

}  // namespace

void beginBootHealthMonitor() {
  loadLastFailure();
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  copyLabel(runningSlot, sizeof(runningSlot), running);
  copyLabel(bootSlot, sizeof(bootSlot), boot);

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t stateResult = running
      ? esp_ota_get_state_partition(running, &state) : ESP_ERR_INVALID_ARG;
  const OtaStateReadStatus readStatus = otaStateReadStatus(stateResult);
  imageStateReadStatus.store(static_cast<uint8_t>(readStatus));
  imageState.store(static_cast<uint32_t>(state));
  const bool readable = readStatus == OtaStateReadStatus::Readable;
  stateReadOk.store(readable);
  if (!readable) {
    validationRequiredImage.store(true);
    publishedPhase.store(static_cast<uint8_t>(Phase::ValidationFailed));
    publishedReason.store(static_cast<uint8_t>(Failure::OtaStateRead));
    Serial.printf("##[OTA-HEALTH]# OTA state %s: %d\n",
                  readStatus == OtaStateReadStatus::Missing
                      ? "record missing" : "read failed",
                  static_cast<int>(stateResult));
    return;
  }

  const bool pending = state == ESP_OTA_IMG_PENDING_VERIFY;
  const bool canRollback = esp_ota_check_rollback_is_possible();
  const bool bootMatchesRunning = running && boot &&
      running->address == boot->address;
  const InitialImageValidationDecision initialDecision =
      initialImageValidationDecision(
          readStatus, durableImageState(static_cast<uint32_t>(state)),
          bootMatchesRunning);
  ValidationMode mode = pending ? ValidationMode::PendingVerify
                                : ValidationMode::None;
  if (initialDecision == InitialImageValidationDecision::Validate)
    mode = ValidationMode::InitialUndefined;

  pendingImage.store(pending);
  validationRequiredImage.store(mode != ValidationMode::None);
  rollbackAvailable.store(canRollback);
  gate.begin(mode, canRollback, millis());

  if (initialDecision ==
      InitialImageValidationDecision::BootPartitionMismatch) {
    validationRequiredImage.store(true);
    gate.begin(ValidationMode::InitialUndefined, canRollback, millis());
    (void)gate.fail(Failure::BootPartitionMismatch);
    publish(millis());
    logSnapshot("initial-boot-mismatch", millis());
    Serial.println(
        "##[OTA-HEALTH]# BLOCKED: initial running and boot slots differ");
    return;
  }
  if (initialDecision == InitialImageValidationDecision::UnknownState) {
    validationRequiredImage.store(true);
    publishedPhase.store(static_cast<uint8_t>(Phase::ValidationFailed));
    publishedReason.store(static_cast<uint8_t>(Failure::OtaStateRead));
    logSnapshot("unknown-image-state", millis());
    return;
  }
  if (mode == ValidationMode::InitialUndefined &&
      static_cast<Failure>(lastFailure.load()) == Failure::MarkValid) {
    (void)gate.validationFailed(false);
    publish(millis());
    logSnapshot("initial-mark-valid-retry-blocked", millis());
    Serial.println(
        "##[OTA-HEALTH]# BLOCKED: previous initial mark-valid failed; "
        "automatic retry disabled");
    return;
  }

  publish(millis());
  logSnapshot("begin", millis());
  if (mode == ValidationMode::None) return;
  if (mode == ValidationMode::PendingVerify && !canRollback) {
    Serial.println(
        "##[OTA-HEALTH]# BLOCKED: PENDING_VERIFY has no bootable rollback slot");
    return;
  }
  TaskHandle_t task = nullptr;
  if (xTaskCreate(monitorTask, "ota-health", kMonitorStack, nullptr, 2,
                  &task) != pdPASS) {
    reportBootFatal(Failure::RuntimeService);
    saveLastFailure(Failure::RuntimeService);
    const bool stillCanRollback = esp_ota_check_rollback_is_possible();
    rollbackAvailable.store(stillCanRollback);
    const Action failureAction = gate.fail(Failure::RuntimeService);
    publish(millis());
    if (failureAction == Action::Rollback && stillCanRollback) {
      Serial.println("##[OTA-HEALTH]# monitor task unavailable; rolling back");
      Serial.flush();
      esp_ota_mark_app_invalid_rollback_and_reboot();
    } else {
      Serial.println(
          "##[OTA-HEALTH]# BLOCKED: monitor task unavailable; "
          "use USB recovery");
    }
  }
}

void reportBootFatal(Failure failure) {
  if (failure != Failure::None)
    fatalFailure.store(static_cast<uint8_t>(failure));
}

void reportBootSetup(bool configurationReady, bool dataReady,
                     bool runtimeReady, bool controlPlaneReady) {
  uint32_t bits = SetupComplete;
  if (configurationReady) bits |= ConfigurationReady;
  if (dataReady) bits |= DataReady;
  if (runtimeReady) bits |= RuntimeReady;
  if (controlPlaneReady) bits |= ControlPlaneReady;
  evidenceBits.store(bits);
  lastServiceMs.store(millis());
}

void serviceBootHealth(bool controlPlaneReady) {
  lastServiceMs.store(millis());
  if (controlPlaneReady) evidenceBits.fetch_or(ControlPlaneReady);
  else evidenceBits.fetch_and(~static_cast<uint32_t>(ControlPlaneReady));
}

void reportHttpApiProbe() {
  httpApiProbed.store(true);
#if defined(VOXONE_OTA_HIL_ROLLBACK_AFTER_PROBE)
  // Dedicated HIL image only: once remote HTTP reachability is proven, force
  // one controlled late-startup failure without changing config or SPIFFS.
  reportBootFatal(Failure::RuntimeService);
#endif
}

bool bootHealthReady() {
  return bootHealthAllowsUpdate(
      stateReadOk.load(), validationRequiredImage.load(), pendingImage.load(),
      static_cast<Phase>(publishedPhase.load()));
}

FirmwareUpdatePreflight firmwareUpdatePreflight() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  const esp_partition_t* update = esp_ota_get_next_update_partition(nullptr);
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t stateResult = running
      ? esp_ota_get_state_partition(running, &state) : ESP_ERR_INVALID_ARG;
  const OtaStateReadStatus readStatus = otaStateReadStatus(stateResult);
  const FirmwareUpdatePreflightInput input{
      readStatus,
      readStatus == OtaStateReadStatus::Readable
          ? durableImageState(static_cast<uint32_t>(state))
          : DurableImageState::Unknown,
      running && boot && running->address == boot->address,
      update != nullptr,
      running && update && running->address != update->address,
  };
  return {
      firmwareUpdateBlock(input),
      running ? running->label : "unknown",
      boot ? boot->label : "unknown",
      update ? update->label : "unknown",
  };
}

RunningImageSnapshot runningImageSnapshot() {
  RunningImageState state = RunningImageState::Unknown;
  switch (durableImageState(imageState.load())) {
    case DurableImageState::Valid:
      state = RunningImageState::Valid;
      break;
    case DurableImageState::PendingVerify:
      state = RunningImageState::PendingVerify;
      break;
    case DurableImageState::Invalid:
      state = RunningImageState::Invalid;
      break;
    case DurableImageState::Aborted:
      state = RunningImageState::Aborted;
      break;
    case DurableImageState::New:
    case DurableImageState::Undefined:
    case DurableImageState::Unknown:
      break;
  }
  return {stateReadOk.load(), state, pendingImage.load()};
}

bool formatBootHealthJson(char* output, size_t capacity) {
  if (!output || !capacity) return false;
  const Phase phase = static_cast<Phase>(publishedPhase.load());
  const Failure reason = static_cast<Failure>(publishedReason.load());
  const Failure previous = static_cast<Failure>(lastFailure.load());
  const FirmwareUpdatePreflight preflight = firmwareUpdatePreflight();
  const int written = std::snprintf(
      output, capacity,
      "{\"runningSlot\":\"%s\",\"bootSlot\":\"%s\","
      "\"imageState\":\"%s\",\"pending\":%s,"
      "\"rollbackAvailable\":%s,\"health\":\"%s\","
      "\"reason\":\"%s\",\"lastFailure\":\"%s\","
      "\"firmwareUpdateReady\":%s,\"firmwareUpdateReason\":\"%s\","
      "\"elapsedMs\":%lu}",
      runningSlot, bootSlot,
      otaImageStateName(currentImageStateReadStatus(), imageState.load()),
      pendingImage.load() ? "true" : "false",
      rollbackAvailable.load() ? "true" : "false", phaseName(phase),
      failureName(reason), failureName(previous),
      preflight.block == FirmwareUpdateBlock::None ? "true" : "false",
      firmwareUpdateBlockName(preflight.block),
      static_cast<unsigned long>(publishedElapsed.load()));
  return written >= 0 && static_cast<size_t>(written) < capacity;
}

}  // namespace ota
}  // namespace voxone
