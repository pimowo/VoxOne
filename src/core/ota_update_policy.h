#ifndef VOXONE_OTA_UPDATE_POLICY_H
#define VOXONE_OTA_UPDATE_POLICY_H

#include <stdint.h>

namespace voxone {
namespace ota {

// A firmware upload is safe only when the image which is running now is a
// durable rollback candidate. The inactive slot may be empty: after the new
// image is selected, the current VALID slot becomes the previous application.
enum class DurableImageState : uint8_t {
  New,
  PendingVerify,
  Valid,
  Invalid,
  Aborted,
  Undefined,
  Unknown,
};

enum class OtaStateReadStatus : uint8_t {
  Readable,
  Missing,
  Unreadable,
};

// Numeric values are the durable ESP-IDF otadata ABI. Keeping this mapping in
// the host-testable policy layer prevents ESP_OTA_IMG_UNDEFINED (0xFFFFFFFF)
// from ever being narrowed to a byte again.
const char* otaImageStateName(OtaStateReadStatus status, uint32_t rawState);
DurableImageState durableImageState(uint32_t rawState);

enum class InitialImageValidationDecision : uint8_t {
  NotApplicable,
  Validate,
  Missing,
  Unreadable,
  UnknownState,
  BootPartitionMismatch,
};

enum class FirmwareUpdateBlock : uint8_t {
  None,
  OtaStateUnreadable,
  OtaStateMissing,
  OtaStateUnknown,
  RunningImageNotValid,
  BootPartitionMismatch,
  UpdatePartitionUnavailable,
  UpdatePartitionConflict,
};

struct FirmwareUpdatePreflightInput {
  OtaStateReadStatus otaStateStatus;
  DurableImageState runningState;
  bool bootPartitionMatchesRunning;
  bool updatePartitionAvailable;
  bool updatePartitionDiffersFromRunning;
};

inline FirmwareUpdateBlock firmwareUpdateBlock(
    const FirmwareUpdatePreflightInput& input) {
  if (input.otaStateStatus == OtaStateReadStatus::Unreadable)
    return FirmwareUpdateBlock::OtaStateUnreadable;
  if (input.otaStateStatus == OtaStateReadStatus::Missing)
    return FirmwareUpdateBlock::OtaStateMissing;
  if (input.runningState == DurableImageState::Unknown)
    return FirmwareUpdateBlock::OtaStateUnknown;
  if (input.runningState != DurableImageState::Valid)
    return FirmwareUpdateBlock::RunningImageNotValid;
  if (!input.bootPartitionMatchesRunning)
    return FirmwareUpdateBlock::BootPartitionMismatch;
  if (!input.updatePartitionAvailable)
    return FirmwareUpdateBlock::UpdatePartitionUnavailable;
  if (!input.updatePartitionDiffersFromRunning)
    return FirmwareUpdateBlock::UpdatePartitionConflict;
  return FirmwareUpdateBlock::None;
}

inline InitialImageValidationDecision initialImageValidationDecision(
    OtaStateReadStatus status, DurableImageState state,
    bool bootPartitionMatchesRunning) {
  if (status == OtaStateReadStatus::Unreadable)
    return InitialImageValidationDecision::Unreadable;
  if (status == OtaStateReadStatus::Missing)
    return InitialImageValidationDecision::Missing;
  if (state == DurableImageState::Unknown)
    return InitialImageValidationDecision::UnknownState;
  if (state != DurableImageState::Undefined)
    return InitialImageValidationDecision::NotApplicable;
  if (!bootPartitionMatchesRunning)
    return InitialImageValidationDecision::BootPartitionMismatch;
  return InitialImageValidationDecision::Validate;
}

enum class MarkValidRecovery : uint8_t {
  ConfirmedValid,
  Rollback,
  DiagnosticHold,
};

// ESP_OK alone is not success: the VALID state must also be read back from
// otadata. Any unconfirmed result rolls back only when IDF has just confirmed
// that another bootable application exists. Otherwise the running image is
// kept alive for diagnostics and USB recovery without an automatic reboot.
inline MarkValidRecovery markValidRecovery(bool callSucceeded,
                                           bool stateReadSucceeded,
                                           DurableImageState stateAfterCall,
                                           bool rollbackAvailable) {
  if (callSucceeded && stateReadSucceeded &&
      stateAfterCall == DurableImageState::Valid)
    return MarkValidRecovery::ConfirmedValid;
  return rollbackAvailable ? MarkValidRecovery::Rollback
                           : MarkValidRecovery::DiagnosticHold;
}

const char* firmwareUpdateBlockName(FirmwareUpdateBlock block);

}  // namespace ota
}  // namespace voxone

#endif
