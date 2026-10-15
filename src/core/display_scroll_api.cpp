#include "display_scroll_api.h"

#include <stdio.h>
#include <string.h>

namespace voxone {
namespace display_scroll {
namespace {

bool validUserLoad(const ScrollSettingsLoadResult& loaded,
                   const ScrollSettings& requested) {
  return loaded.status == ScrollSettingsLoadStatus::UserSettings &&
         loaded.source == ScrollSettingsSource::UserSettings &&
         loaded.recordStatus == ScrollSettingsRecordStatus::Valid &&
         equalScrollSettings(loaded.settings, requested);
}

ScrollSettingsApplyResult failure(ScrollSettingsApplyStatus status,
                                  bool persisted, bool changed,
                                  bool coreActive) {
  return ScrollSettingsApplyResult{status, changed, persisted, false,
                                   coreActive};
}

ScrollSettingsApplyStatus mapSaveFailure(ScrollSettingsSaveStatus status) {
  switch (status) {
    case ScrollSettingsSaveStatus::Invalid:
      return ScrollSettingsApplyStatus::Invalid;
    case ScrollSettingsSaveStatus::StorageOpenError:
      return ScrollSettingsApplyStatus::StorageOpenFailed;
    case ScrollSettingsSaveStatus::StorageWriteError:
      return ScrollSettingsApplyStatus::StorageWriteFailed;
    case ScrollSettingsSaveStatus::StorageCommitError:
      return ScrollSettingsApplyStatus::StorageCommitFailed;
    case ScrollSettingsSaveStatus::StorageVerifyError:
      return ScrollSettingsApplyStatus::StorageVerifyFailed;
    case ScrollSettingsSaveStatus::Saved:
    case ScrollSettingsSaveStatus::Unchanged:
      break;
  }
  return ScrollSettingsApplyStatus::StorageWriteFailed;
}

bool parsePause(const char* value, uint16_t& pauseMs) {
  static const struct {
    const char* text;
    uint16_t value;
  } allowed[] = {{"0", 0},       {"1000", 1000}, {"2000", 2000},
                 {"3000", 3000}, {"4000", 4000}, {"5000", 5000}};
  for (const auto& entry : allowed) {
    if (strcmp(value, entry.text) == 0) {
      pauseMs = entry.value;
      return true;
    }
  }
  return false;
}

}  // namespace

ScrollSettingsCommandStatus parseScrollSettingsCommand(
    const char* value, ScrollSettings& output) {
  if (!value || !*value) return ScrollSettingsCommandStatus::InvalidStructure;
  const char* firstComma = strchr(value, ',');
  if (!firstComma || firstComma == value)
    return ScrollSettingsCommandStatus::InvalidStructure;
  const char* secondComma = strchr(firstComma + 1, ',');
  if (!secondComma || secondComma == firstComma + 1 ||
      strchr(secondComma + 1, ',') || !secondComma[1])
    return ScrollSettingsCommandStatus::InvalidStructure;

  bool enabled;
  const size_t enabledLength = static_cast<size_t>(firstComma - value);
  if (enabledLength == 4 && strncmp(value, "true", 4) == 0)
    enabled = true;
  else if (enabledLength == 5 && strncmp(value, "false", 5) == 0)
    enabled = false;
  else
    return ScrollSettingsCommandStatus::InvalidEnabled;

  ScrollSpeed speed;
  const char* speedText = firstComma + 1;
  const size_t speedLength = static_cast<size_t>(secondComma - speedText);
  if (speedLength == 4 && strncmp(speedText, "slow", 4) == 0)
    speed = ScrollSpeed::Slow;
  else if (speedLength == 6 && strncmp(speedText, "normal", 6) == 0)
    speed = ScrollSpeed::Normal;
  else if (speedLength == 4 && strncmp(speedText, "fast", 4) == 0)
    speed = ScrollSpeed::Fast;
  else
    return ScrollSettingsCommandStatus::InvalidSpeed;

  uint16_t pauseMs = 0;
  if (!parsePause(secondComma + 1, pauseMs))
    return ScrollSettingsCommandStatus::InvalidPause;
  output = ScrollSettings{enabled, speed, pauseMs};
  return ScrollSettingsCommandStatus::Ok;
}

ScrollSettingsWebView makeScrollSettingsWebView(
    ScrollRendererProfile profile,
    const ScrollSettingsRuntimeSnapshot& snapshot,
    bool coreBuildEnabled) {
  const ScrollRendererDefaults defaults = rendererScrollDefaults(profile);
  const bool supported = defaults.supported;
  ScrollSettings settings = snapshot.settings;
  if (snapshot.source == ScrollSettingsSource::RendererDefaults && supported)
    settings = ScrollSettings{defaults.enabled, defaults.speed,
                              defaults.pauseMs};
  return ScrollSettingsWebView{supported, settings, snapshot.source,
                               supported && coreBuildEnabled};
}

const char* scrollSpeedApiName(ScrollSpeed speed) {
  switch (speed) {
    case ScrollSpeed::Slow: return "slow";
    case ScrollSpeed::Normal: return "normal";
    case ScrollSpeed::Fast: return "fast";
  }
  return "normal";
}

const char* scrollSettingsSourceApiName(ScrollSettingsSource source) {
  return source == ScrollSettingsSource::UserSettings ? "userSettings"
                                                       : "rendererDefaults";
}

ScrollSettingsApplyResult applyScrollSettingsUpdate(
    const ScrollSettings& requested, bool coreActive,
    const ScrollSettingsApiOperations& operations) {
  if (!isValidScrollSettings(requested) || !operations.read ||
      !operations.save || !operations.reload || !operations.snapshot)
    return failure(ScrollSettingsApplyStatus::Invalid, false, false,
                   coreActive);

  const ScrollSettingsLoadResult current = operations.read(operations.context);
  if (current.status == ScrollSettingsLoadStatus::RendererDefaultsOpenError ||
      current.status == ScrollSettingsLoadStatus::RendererDefaultsReadError)
    return failure(ScrollSettingsApplyStatus::ReadFailed, false, false,
                   coreActive);

  const ScrollSettingsSaveStatus saved =
      operations.save(operations.context, current, requested);
  const bool changed = saved == ScrollSettingsSaveStatus::Saved;
  if (saved != ScrollSettingsSaveStatus::Saved &&
      saved != ScrollSettingsSaveStatus::Unchanged)
    return failure(mapSaveFailure(saved), false, false, coreActive);

  const ScrollSettingsLoadResult reloaded =
      operations.reload(operations.context);
  if (!validUserLoad(reloaded, requested))
    return failure(ScrollSettingsApplyStatus::ReloadFailed, true, changed,
                   coreActive);

  const ScrollSettingsRuntimeSnapshot published =
      operations.snapshot(operations.context);
  if (published.source != ScrollSettingsSource::UserSettings ||
      !equalScrollSettings(published.settings, requested))
    return failure(ScrollSettingsApplyStatus::RuntimeVerifyFailed, true,
                   changed, coreActive);

  return ScrollSettingsApplyResult{ScrollSettingsApplyStatus::Applied,
                                   changed, true, true, coreActive};
}

const char* scrollSettingsApplyError(ScrollSettingsApplyStatus status) {
  switch (status) {
    case ScrollSettingsApplyStatus::Applied: return "none";
    case ScrollSettingsApplyStatus::Invalid: return "invalid_value";
    case ScrollSettingsApplyStatus::Unsupported: return "unsupported_display";
    case ScrollSettingsApplyStatus::Busy: return "busy";
    case ScrollSettingsApplyStatus::ReadFailed: return "storage_read_failed";
    case ScrollSettingsApplyStatus::StorageOpenFailed:
      return "storage_open_failed";
    case ScrollSettingsApplyStatus::StorageWriteFailed:
      return "storage_write_failed";
    case ScrollSettingsApplyStatus::StorageCommitFailed:
      return "storage_commit_failed";
    case ScrollSettingsApplyStatus::StorageVerifyFailed:
      return "storage_verify_failed";
    case ScrollSettingsApplyStatus::ReloadFailed: return "reload_failed";
    case ScrollSettingsApplyStatus::RuntimeVerifyFailed:
      return "runtime_verify_failed";
  }
  return "unknown";
}

bool formatScrollSettingsResultJson(
    char* output, size_t capacity,
    const ScrollSettingsApplyResult& result) {
  if (!output || !capacity) return false;
  const bool ok = result.status == ScrollSettingsApplyStatus::Applied;
  const int length = snprintf(
      output, capacity,
      "{\"event\":\"scrollSettings.result\",\"ok\":%s,"
      "\"error\":\"%s\",\"changed\":%s,\"persisted\":%s,"
      "\"runtimePublished\":%s,\"activeApplied\":%s,"
      "\"scrollCoreActive\":%s}",
      ok ? "true" : "false", scrollSettingsApplyError(result.status),
      result.changed ? "true" : "false",
      result.persisted ? "true" : "false",
      result.runtimePublished ? "true" : "false",
      result.runtimePublished && result.scrollCoreActive ? "true" : "false",
      result.scrollCoreActive ? "true" : "false");
  if (length < 0 || static_cast<size_t>(length) >= capacity) {
    output[0] = '\0';
    return false;
  }
  return true;
}

}  // namespace display_scroll
}  // namespace voxone
