#include "ota_update_policy.h"

namespace voxone {
namespace ota {

const char* otaImageStateName(OtaStateReadStatus status, uint32_t rawState) {
  if (status == OtaStateReadStatus::Missing) return "missing";
  if (status == OtaStateReadStatus::Unreadable) return "unreadable";
  switch (rawState) {
    case 0U: return "new";
    case 1U: return "pending_verify";
    case 2U: return "valid";
    case 3U: return "invalid";
    case 4U: return "aborted";
    case UINT32_MAX: return "undefined";
  }
  return "unknown";
}

DurableImageState durableImageState(uint32_t rawState) {
  switch (rawState) {
    case 0U: return DurableImageState::New;
    case 1U: return DurableImageState::PendingVerify;
    case 2U: return DurableImageState::Valid;
    case 3U: return DurableImageState::Invalid;
    case 4U: return DurableImageState::Aborted;
    case UINT32_MAX: return DurableImageState::Undefined;
  }
  return DurableImageState::Unknown;
}

const char* firmwareUpdateBlockName(FirmwareUpdateBlock block) {
  switch (block) {
    case FirmwareUpdateBlock::None: return "none";
    case FirmwareUpdateBlock::OtaStateUnreadable:
      return "ota_state_unreadable";
    case FirmwareUpdateBlock::OtaStateMissing:
      return "ota_state_missing";
    case FirmwareUpdateBlock::OtaStateUnknown:
      return "ota_state_unknown";
    case FirmwareUpdateBlock::RunningImageNotValid:
      return "running_image_not_valid";
    case FirmwareUpdateBlock::BootPartitionMismatch:
      return "boot_partition_mismatch";
    case FirmwareUpdateBlock::UpdatePartitionUnavailable:
      return "update_partition_unavailable";
    case FirmwareUpdateBlock::UpdatePartitionConflict:
      return "update_partition_conflict";
  }
  return "unknown";
}

}  // namespace ota
}  // namespace voxone
