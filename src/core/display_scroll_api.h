#ifndef VOXONE_DISPLAY_SCROLL_API_H
#define VOXONE_DISPLAY_SCROLL_API_H

#include <stddef.h>
#include <stdint.h>

#include "display_scroll_runtime_config.h"

namespace voxone {
namespace display_scroll {

enum class ScrollSettingsCommandStatus : uint8_t {
  Ok,
  InvalidStructure,
  InvalidEnabled,
  InvalidSpeed,
  InvalidPause
};

ScrollSettingsCommandStatus parseScrollSettingsCommand(
    const char* value, ScrollSettings& output);

struct ScrollSettingsWebView {
  bool canScrollSettings;
  ScrollSettings settings;
  ScrollSettingsSource source;
  bool scrollCoreActive;
};

ScrollSettingsWebView makeScrollSettingsWebView(
    ScrollRendererProfile profile,
    const ScrollSettingsRuntimeSnapshot& snapshot,
    bool coreBuildEnabled);
const char* scrollSpeedApiName(ScrollSpeed speed);
const char* scrollSettingsSourceApiName(ScrollSettingsSource source);

struct ScrollSettingsApiOperations {
  void* context;
  ScrollSettingsLoadResult (*read)(void* context);
  ScrollSettingsSaveStatus (*save)(
      void* context, const ScrollSettingsLoadResult& current,
      const ScrollSettings& requested);
  ScrollSettingsLoadResult (*reload)(void* context);
  ScrollSettingsRuntimeSnapshot (*snapshot)(void* context);
};

enum class ScrollSettingsApplyStatus : uint8_t {
  Applied,
  Invalid,
  Unsupported,
  Busy,
  ReadFailed,
  StorageOpenFailed,
  StorageWriteFailed,
  StorageCommitFailed,
  StorageVerifyFailed,
  ReloadFailed,
  RuntimeVerifyFailed
};

struct ScrollSettingsApplyResult {
  ScrollSettingsApplyStatus status;
  bool changed;
  bool persisted;
  bool runtimePublished;
  bool scrollCoreActive;
};

ScrollSettingsApplyResult applyScrollSettingsUpdate(
    const ScrollSettings& requested, bool coreActive,
    const ScrollSettingsApiOperations& operations);
const char* scrollSettingsApplyError(ScrollSettingsApplyStatus status);
bool formatScrollSettingsResultJson(
    char* output, size_t capacity,
    const ScrollSettingsApplyResult& result);

// Production adapters. The view is read-only and allocation-free. The update
// performs NVS write/commit/verify, reload and runtime snapshot verification;
// callers must invoke it outside AsyncTCP and DisplayTask.
ScrollSettingsWebView currentScrollSettingsWebView();
ScrollSettingsApplyResult applyPersistentScrollSettings(
    const ScrollSettings& requested, bool coreActive);

}  // namespace display_scroll
}  // namespace voxone

#endif
