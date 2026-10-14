#include "../src/core/config_backup_web.h"

#include <cassert>
#include <cstring>
#include <initializer_list>

using namespace voxone::config_backup;
using namespace voxone::config_backup_web;

int main() {
  static_assert(kMaxUploadBytes == kBackupFixedOverhead + 355 + 192 * 1024,
                "upload limit must follow persistent storage bounds");
  static_assert(sizeof(UploadState) <= 4 * sizeof(std::size_t),
                "HTTP upload accounting must remain bounded");
  assert(std::strcmp(kBackupRoute, "/api/config/backup") == 0);
  assert(std::strcmp(kRestoreRoute, "/api/config/restore") == 0);
  assert(std::strcmp(kContentType, "application/octet-stream") == 0);
  assert(std::strcmp(kCacheControl, "no-store") == 0);
  assert(restoreStagingFits(196608, 48009, 140000));
  assert(!restoreStagingFits(196608, 48009, 144504));
  assert(!restoreStagingFits(196608, 196609, 0));

  UploadState upload;
  assert(upload.append(0, 4, 10) == UploadDecision::Accept);
  assert(upload.append(4, 6, 10) == UploadDecision::Complete);
  assert(upload.complete() && upload.size() == 10);
  assert(upload.append(10, 0, 10) == UploadDecision::AlreadyComplete);

  UploadState offset;
  assert(offset.append(1, 1, 10) == UploadDecision::InvalidOffset);
  UploadState empty;
  assert(empty.append(0, 0, 0) == UploadDecision::InvalidSize);
  UploadState oversized;
  assert(oversized.append(0, 1, kMaxUploadBytes + 1) ==
         UploadDecision::InvalidSize);
  UploadState truncated;
  assert(truncated.append(0, 3, 5) == UploadDecision::Accept);
  assert(!truncated.complete());
  assert(truncated.append(4, 1, 5) == UploadDecision::InvalidOffset);

  // The maximum accepted request is accounted for incrementally; transport
  // state does not grow with Content-Length.
  UploadState maximum;
  constexpr std::size_t chunk = 997;
  for (std::size_t offset = 0; offset < kMaxUploadBytes;) {
    const std::size_t count =
        kMaxUploadBytes - offset < chunk ? kMaxUploadBytes - offset : chunk;
    const UploadDecision decision = maximum.append(
        offset, count, kMaxUploadBytes);
    offset += count;
    assert(decision == (offset == kMaxUploadBytes
                            ? UploadDecision::Complete
                            : UploadDecision::Accept));
  }
  assert(maximum.complete() && maximum.size() == kMaxUploadBytes);

  // Dropping an incomplete UploadState models a disconnected request: no
  // process-wide buffer or state survives the request object.
  {
    UploadState disconnected;
    assert(disconnected.append(0, 4096, kMaxUploadBytes) ==
           UploadDecision::Accept);
    assert(!disconnected.complete());
  }

  ApiResult mapped = mapRestoreResult({Status::Ok, true});
  assert(mapped.httpStatus == 200 && !mapped.error && mapped.restartRequired);
  mapped = mapRestoreResult({Status::WrongSchema, false});
  assert(mapped.httpStatus == 422 && std::strcmp(mapped.error, "wrong_schema") == 0);
  mapped = mapRestoreResult({Status::InvalidChecksum, false});
  assert(mapped.httpStatus == 422 && std::strcmp(mapped.error, "bad_crc") == 0);
  mapped = mapRestoreResult({Status::UnsupportedHardware, false});
  assert(mapped.httpStatus == 422 &&
         std::strcmp(mapped.error, "unsupported_hardware") == 0);
  mapped = mapRestoreResult({Status::RecoveryPending, false});
  assert(mapped.httpStatus == 503 &&
         std::strcmp(mapped.error, "recovery_pending") == 0 &&
         !mapped.restartRequired);
  mapped = mapRestoreResult({Status::StorageCommitFailed, false});
  assert(mapped.httpStatus == 500 &&
         std::strcmp(mapped.error, "restore_failed") == 0);
  for (Status status : {Status::Empty, Status::Truncated, Status::WrongMagic,
                        Status::InvalidLength, Status::InvalidHardware,
                        Status::InvalidScroll}) {
    mapped = mapRestoreResult({status, false});
    assert(mapped.httpStatus == 400 &&
           std::strcmp(mapped.error, "invalid_file") == 0 &&
           !mapped.restartRequired);
  }
}
