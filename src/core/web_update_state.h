#ifndef VOXONE_WEB_UPDATE_STATE_H
#define VOXONE_WEB_UPDATE_STATE_H

#include <stddef.h>
#include <stdint.h>

enum class WebUpdateImageKind : uint8_t { Firmware, Filesystem };

enum class WebUpdateValidation : uint8_t {
  Ok,
  Empty,
  HeaderTooShort,
  BadImage,
  Oversized,
  FilesystemSizeMismatch,
  DeclaredSizeExceedsRequest
};

inline WebUpdateValidation validateWebUpdateStart(
    WebUpdateImageKind kind, const uint8_t* header, size_t headerSize,
    size_t expectedSize, size_t requestSize, size_t partitionSize) {
  if (expectedSize == 0) return WebUpdateValidation::Empty;
  if (!header || headerSize < 8) return WebUpdateValidation::HeaderTooShort;
  if (expectedSize > partitionSize) return WebUpdateValidation::Oversized;
  if (expectedSize > requestSize)
    return WebUpdateValidation::DeclaredSizeExceedsRequest;
  if (kind == WebUpdateImageKind::Filesystem && expectedSize != partitionSize)
    return WebUpdateValidation::FilesystemSizeMismatch;
  if (kind == WebUpdateImageKind::Firmware) {
    if (header[0] != 0xE9 || header[1] == 0 || header[1] > 16)
      return WebUpdateValidation::BadImage;
  } else if (header[0] != 0 || header[1] != 0 || header[2] != 1 ||
             header[3] != 0 || header[4] != 1 || header[5] != 0) {
    return WebUpdateValidation::BadImage;
  }
  return WebUpdateValidation::Ok;
}

inline bool webUpdateChunkFits(size_t received, size_t index, size_t length,
                               size_t expected, size_t partitionSize) {
  if (index != received || index > partitionSize ||
      length > partitionSize - index) return false;
  return expected == 0 ||
         (index <= expected && length <= expected - index);
}

inline bool webUpdateUploadComplete(size_t received, size_t expected) {
  return expected != 0 && received == expected;
}

struct WebUpdateFailurePolicy {
  bool abortSession;
  bool recoverFilesystem;
  bool allowRestart;
  bool resumeAudio;
};

inline WebUpdateFailurePolicy webUpdateFailurePolicy(
    bool sessionStarted, bool filesystemNeedsRecovery) {
  return {sessionStarted, filesystemNeedsRecovery, false, false};
}

inline bool webUpdateStorageReadyAfterFailure(bool filesystemNeedsRecovery,
                                              bool remounted,
                                              bool userDataRestored) {
  return !filesystemNeedsRecovery || (remounted && userDataRestored);
}

#endif
