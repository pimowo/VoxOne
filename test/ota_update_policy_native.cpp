#include "../src/core/ota_update_policy.h"

#include <assert.h>
#include <string.h>

using namespace voxone::ota;

static FirmwareUpdatePreflightInput validPreflight() {
  return {true, DurableImageState::Valid, true, true, true};
}

int main() {
  // A VALID running app remains the rollback image after the other slot is
  // written, regardless of whether that other slot was valid or empty.
  assert(firmwareUpdateBlock(validPreflight()) == FirmwareUpdateBlock::None);
  assert(firmwareUpdateBlock(validPreflight()) == FirmwareUpdateBlock::None);

  // An unprepared first OTA (legacy/undefined otadata) is blocked before the
  // inactive slot is erased. Service must provision and validate a base image.
  auto input = validPreflight();
  input.runningState = DurableImageState::Undefined;
  assert(firmwareUpdateBlock(input) ==
         FirmwareUpdateBlock::RunningImageNotValid);

  input = validPreflight();
  input.runningState = DurableImageState::PendingVerify;
  assert(firmwareUpdateBlock(input) ==
         FirmwareUpdateBlock::RunningImageNotValid);

  input = validPreflight();
  input.otaStateReadable = false;
  assert(firmwareUpdateBlock(input) ==
         FirmwareUpdateBlock::OtaStateUnreadable);

  input = validPreflight();
  input.bootPartitionMatchesRunning = false;
  assert(firmwareUpdateBlock(input) ==
         FirmwareUpdateBlock::BootPartitionMismatch);

  input = validPreflight();
  input.updatePartitionAvailable = false;
  input.updatePartitionDiffersFromRunning = false;
  assert(firmwareUpdateBlock(input) ==
         FirmwareUpdateBlock::UpdatePartitionUnavailable);

  input = validPreflight();
  input.updatePartitionDiffersFromRunning = false;
  assert(firmwareUpdateBlock(input) ==
         FirmwareUpdateBlock::UpdatePartitionConflict);

  // Validation succeeds only after both the IDF call and durable readback.
  assert(markValidRecovery(true, true, DurableImageState::Valid, true) ==
         MarkValidRecovery::ConfirmedValid);
  assert(markValidRecovery(true, false, DurableImageState::Unknown, true) ==
         MarkValidRecovery::Rollback);
  assert(markValidRecovery(false, true, DurableImageState::Valid, true) ==
         MarkValidRecovery::Rollback);
  assert(markValidRecovery(false, false, DurableImageState::Unknown, false) ==
         MarkValidRecovery::DiagnosticHold);

  assert(strcmp(firmwareUpdateBlockName(FirmwareUpdateBlock::None), "none") ==
         0);
  assert(strcmp(firmwareUpdateBlockName(
                    FirmwareUpdateBlock::RunningImageNotValid),
                "running_image_not_valid") == 0);
}
