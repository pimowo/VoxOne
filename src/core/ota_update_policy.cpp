#include "ota_update_policy.h"

namespace voxone {
namespace ota {

const char* firmwareUpdateBlockName(FirmwareUpdateBlock block) {
  switch (block) {
    case FirmwareUpdateBlock::None: return "none";
    case FirmwareUpdateBlock::OtaStateUnreadable:
      return "ota_state_unreadable";
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
