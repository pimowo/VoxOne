#include "../src/core/ota_update_policy.h"

#include <assert.h>
#include <string.h>

using namespace voxone::ota;

static FirmwareUpdatePreflightInput validPreflight() {
  return {OtaStateReadStatus::Readable, DurableImageState::Valid,
          true, true, true};
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
  input.otaStateStatus = OtaStateReadStatus::Unreadable;
  assert(firmwareUpdateBlock(input) ==
         FirmwareUpdateBlock::OtaStateUnreadable);

  input = validPreflight();
  input.otaStateStatus = OtaStateReadStatus::Missing;
  assert(firmwareUpdateBlock(input) == FirmwareUpdateBlock::OtaStateMissing);

  input = validPreflight();
  input.runningState = DurableImageState::Unknown;
  assert(firmwareUpdateBlock(input) == FirmwareUpdateBlock::OtaStateUnknown);

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

  // The full-width IDF sentinel must survive diagnostics and policy. Missing,
  // unreadable and unknown values remain distinct fail-closed states.
  assert(durableImageState(UINT32_MAX) == DurableImageState::Undefined);
  assert(strcmp(otaImageStateName(OtaStateReadStatus::Readable, UINT32_MAX),
                "undefined") == 0);
  assert(strcmp(otaImageStateName(OtaStateReadStatus::Missing, UINT32_MAX),
                "missing") == 0);
  assert(strcmp(otaImageStateName(OtaStateReadStatus::Unreadable, UINT32_MAX),
                "unreadable") == 0);
  assert(strcmp(otaImageStateName(OtaStateReadStatus::Readable, 0x12345678U),
                "unknown") == 0);

  assert(initialImageValidationDecision(
             OtaStateReadStatus::Readable, DurableImageState::Undefined,
             true) == InitialImageValidationDecision::Validate);
  assert(initialImageValidationDecision(
             OtaStateReadStatus::Readable, DurableImageState::Undefined,
             false) == InitialImageValidationDecision::BootPartitionMismatch);
  assert(initialImageValidationDecision(
             OtaStateReadStatus::Missing, DurableImageState::Unknown,
             true) == InitialImageValidationDecision::Missing);
  assert(initialImageValidationDecision(
             OtaStateReadStatus::Unreadable, DurableImageState::Unknown,
             true) == InitialImageValidationDecision::Unreadable);
  assert(initialImageValidationDecision(
             OtaStateReadStatus::Readable, DurableImageState::Unknown,
             true) == InitialImageValidationDecision::UnknownState);
  assert(initialImageValidationDecision(
             OtaStateReadStatus::Readable, DurableImageState::PendingVerify,
             true) == InitialImageValidationDecision::NotApplicable);
  assert(initialImageValidationDecision(
             OtaStateReadStatus::Readable, DurableImageState::New,
             true) == InitialImageValidationDecision::NotApplicable);
  assert(initialImageValidationDecision(
             OtaStateReadStatus::Readable, DurableImageState::Invalid,
             true) == InitialImageValidationDecision::NotApplicable);
  assert(initialImageValidationDecision(
             OtaStateReadStatus::Readable, DurableImageState::Aborted,
             true) == InitialImageValidationDecision::NotApplicable);
  assert(initialImageValidationDecision(
             OtaStateReadStatus::Readable, DurableImageState::Valid,
             true) == InitialImageValidationDecision::NotApplicable);

  // Once the base app is durably VALID, the existing preflight allows writing
  // an available, different app1 even when that inactive slot starts empty.
  input = validPreflight();
  assert(firmwareUpdateBlock(input) == FirmwareUpdateBlock::None);

  assert(strcmp(firmwareUpdateBlockName(FirmwareUpdateBlock::None), "none") ==
         0);
  assert(strcmp(firmwareUpdateBlockName(
                    FirmwareUpdateBlock::RunningImageNotValid),
                "running_image_not_valid") == 0);
  assert(strcmp(firmwareUpdateBlockName(FirmwareUpdateBlock::OtaStateMissing),
                "ota_state_missing") == 0);
  assert(strcmp(firmwareUpdateBlockName(FirmwareUpdateBlock::OtaStateUnknown),
                "ota_state_unknown") == 0);
}
