#include "config_backup_web.h"

namespace voxone {
namespace config_backup_web {

UploadDecision UploadState::append(std::size_t index, std::size_t length,
                                   std::size_t total) {
  if (complete_) return UploadDecision::AlreadyComplete;
  if (!started_) {
    if (index != 0) return UploadDecision::InvalidOffset;
    if (!total || total > kMaxUploadBytes) return UploadDecision::InvalidSize;
    total_ = total;
    started_ = true;
  }
  if (total != total_ || index != received_ || length > total_ - received_)
    return UploadDecision::InvalidOffset;
  received_ += length;
  if (received_ == total_) {
    complete_ = true;
    return UploadDecision::Complete;
  }
  return UploadDecision::Accept;
}

ApiResult mapRestoreResult(const config_backup::RestoreResult& result) {
  using config_backup::Status;
  switch (result.status) {
    case Status::Ok:
      return {200, nullptr, result.restartRequired};
    case Status::WrongSchema:
      return {422, "wrong_schema", false};
    case Status::InvalidChecksum:
      return {422, "bad_crc", false};
    case Status::UnsupportedHardware:
      return {422, "unsupported_hardware", false};
    case Status::RecoveryPending:
      return {503, "recovery_pending", false};
    case Status::StorageBeginFailed:
    case Status::StorageReadFailed:
    case Status::AllocationFailed:
    case Status::StorageWriteFailed:
    case Status::StorageCommitFailed:
      return {500, "restore_failed", false};
    case Status::Empty:
    case Status::Truncated:
    case Status::WrongMagic:
    case Status::InvalidLength:
    case Status::InvalidConfig:
    case Status::InvalidHardware:
    case Status::InvalidMqtt:
    case Status::InvalidUi:
    case Status::InvalidScroll:
    case Status::InvalidWifi:
    case Status::InvalidStations:
    case Status::InvalidArgument:
    case Status::OutputTooSmall:
      return {400, "invalid_file", false};
  }
  return {500, "restore_failed", false};
}

}  // namespace config_backup_web
}  // namespace voxone
