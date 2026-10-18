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

enum class FirmwareUpdateBlock : uint8_t {
  None,
  OtaStateUnreadable,
  RunningImageNotValid,
  BootPartitionMismatch,
  UpdatePartitionUnavailable,
  UpdatePartitionConflict,
};

struct FirmwareUpdatePreflightInput {
  bool otaStateReadable;
  DurableImageState runningState;
  bool bootPartitionMatchesRunning;
  bool updatePartitionAvailable;
  bool updatePartitionDiffersFromRunning;
};

inline FirmwareUpdateBlock firmwareUpdateBlock(
    const FirmwareUpdatePreflightInput& input) {
  if (!input.otaStateReadable)
    return FirmwareUpdateBlock::OtaStateUnreadable;
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
