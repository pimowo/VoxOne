#ifndef VOXONE_CONFIG_BACKUP_WEB_H
#define VOXONE_CONFIG_BACKUP_WEB_H

#include <cstddef>
#include <cstdint>

#include "config_backup.h"

namespace voxone {
namespace config_backup_web {

constexpr char kBackupRoute[] = "/api/config/backup";
constexpr char kRestoreRoute[] = "/api/config/restore";
constexpr char kContentType[] = "application/octet-stream";
constexpr char kCacheControl[] = "no-store";
constexpr std::size_t kMaxStationsBytes = 192u * 1024u;
constexpr std::size_t kMaxWifiBytes = 5u * (29u + 1u + 39u + 2u);
constexpr std::size_t kMaxUploadBytes =
    config_backup::kBackupFixedOverhead + kMaxWifiBytes + kMaxStationsBytes;
constexpr std::size_t kRestoreStagingReserveBytes = 4096;

constexpr bool restoreStagingFits(std::size_t totalBytes,
                                  std::size_t usedBytes,
                                  std::size_t candidateBytes) {
  return usedBytes <= totalBytes &&
         candidateBytes <= totalBytes - usedBytes &&
         totalBytes - usedBytes - candidateBytes >=
             kRestoreStagingReserveBytes;
}

enum class UploadDecision : uint8_t {
  Accept,
  Complete,
  InvalidSize,
  InvalidOffset,
  AlreadyComplete
};

class UploadState {
 public:
  UploadDecision append(std::size_t index, std::size_t length,
                        std::size_t total);
  std::size_t size() const { return total_; }
  bool complete() const { return complete_; }

 private:
  std::size_t total_ = 0;
  std::size_t received_ = 0;
  bool started_ = false;
  bool complete_ = false;
};

struct ApiResult {
  uint16_t httpStatus;
  const char* error;
  bool restartRequired;
};

ApiResult mapRestoreResult(const config_backup::RestoreResult& result);

}  // namespace config_backup_web
}  // namespace voxone

#endif
