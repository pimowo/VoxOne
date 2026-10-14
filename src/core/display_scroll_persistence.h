#ifndef VOXONE_DISPLAY_SCROLL_PERSISTENCE_H
#define VOXONE_DISPLAY_SCROLL_PERSISTENCE_H

#include <stddef.h>
#include <stdint.h>

#include "display_scroll_model.h"

namespace voxone {
namespace display_scroll {

constexpr uint8_t kScrollSettingsRecordVersion = 1;
constexpr size_t kScrollSettingsRecordSize = 13;

enum class ScrollSettingsRecordStatus : uint8_t {
  Valid,
  NotDecoded,
  InvalidSize,
  InvalidMagic,
  InvalidVersion,
  InvalidCrc,
  InvalidEnabled,
  InvalidSpeed,
  InvalidPause
};

enum class ScrollSettingsStorageReadStatus : uint8_t {
  Found,
  Missing,
  OpenError,
  ReadError
};

enum class ScrollSettingsStorageWriteStatus : uint8_t {
  Stored,
  OpenError,
  WriteError,
  CommitError,
  VerifyError
};

enum class ScrollSettingsLoadStatus : uint8_t {
  UserSettings,
  RendererDefaultsMissing,
  RendererDefaultsInvalid,
  RendererDefaultsOpenError,
  RendererDefaultsReadError
};

enum class ScrollSettingsSaveStatus : uint8_t {
  Saved,
  Unchanged,
  Invalid,
  StorageOpenError,
  StorageWriteError,
  StorageCommitError,
  StorageVerifyError
};

class ScrollSettingsStorage {
 public:
  virtual ~ScrollSettingsStorage() {}
  virtual ScrollSettingsStorageReadStatus read(uint8_t* output,
                                                size_t capacity,
                                                size_t& size) = 0;
  virtual ScrollSettingsStorageWriteStatus write(const uint8_t* input,
                                                   size_t size) = 0;
};

// This is a value snapshot: it owns no storage handles and is safe to copy to
// a future DisplayTask handoff. RendererDefaults means that the renderer must
// resolve its historical values instead of interpreting settings as user data.
struct ScrollSettingsLoadResult {
  ScrollSettings settings;
  ScrollSettingsSource source;
  ScrollSettingsLoadStatus status;
  ScrollSettingsRecordStatus recordStatus;
};

bool equalScrollSettings(const ScrollSettings& left,
                         const ScrollSettings& right);
uint32_t scrollSettingsRecordCrc32(const uint8_t* data, size_t size);
bool serializeScrollSettingsRecord(const ScrollSettings& settings,
                                   uint8_t* output, size_t outputSize);
ScrollSettingsRecordStatus deserializeScrollSettingsRecord(
    const uint8_t* input, size_t inputSize, ScrollSettings& output);

ScrollSettingsLoadResult loadScrollSettings(ScrollSettingsStorage& storage);
ScrollSettingsSaveStatus saveScrollSettings(
    ScrollSettingsStorage& storage, const ScrollSettingsLoadResult& current,
    const ScrollSettings& requested);

// Production NVS adapter. These functions are intentionally not called by the
// active Display runtime yet; persistence remains an isolated subsystem.
ScrollSettingsLoadResult readScrollSettingsPersistent();
ScrollSettingsSaveStatus writeScrollSettingsPersistent(
    const ScrollSettingsLoadResult& current,
    const ScrollSettings& requested);

}  // namespace display_scroll
}  // namespace voxone

#endif
