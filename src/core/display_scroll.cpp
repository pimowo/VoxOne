#include "display_scroll.h"

// Consolidated from display_scroll_persistence.cpp.
#include <string.h>

namespace voxone {
namespace display_scroll {
namespace {

constexpr uint8_t kMagic[4] = {'V', 'X', 'S', 'C'};
constexpr size_t kMagicOffset = 0;
constexpr size_t kVersionOffset = 4;
constexpr size_t kEnabledOffset = 5;
constexpr size_t kSpeedOffset = 6;
constexpr size_t kPauseOffset = 7;
constexpr size_t kCrcOffset = 9;

void write16(uint8_t* output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
}

void write32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
  output[2] = static_cast<uint8_t>(value >> 16);
  output[3] = static_cast<uint8_t>(value >> 24);
}

uint16_t read16(const uint8_t* input) {
  return static_cast<uint16_t>(input[0]) |
         static_cast<uint16_t>(input[1]) << 8;
}

uint32_t read32(const uint8_t* input) {
  return static_cast<uint32_t>(input[0]) |
         static_cast<uint32_t>(input[1]) << 8 |
         static_cast<uint32_t>(input[2]) << 16 |
         static_cast<uint32_t>(input[3]) << 24;
}

ScrollSettingsLoadResult rendererDefaults(
    ScrollSettingsLoadStatus status,
    ScrollSettingsRecordStatus recordStatus =
        ScrollSettingsRecordStatus::NotDecoded) {
  return ScrollSettingsLoadResult{defaultScrollSettings(),
                                  ScrollSettingsSource::RendererDefaults,
                                  status, recordStatus};
}

ScrollSettingsSaveStatus mapWriteStatus(
    ScrollSettingsStorageWriteStatus status) {
  switch (status) {
    case ScrollSettingsStorageWriteStatus::Stored:
      return ScrollSettingsSaveStatus::Saved;
    case ScrollSettingsStorageWriteStatus::OpenError:
      return ScrollSettingsSaveStatus::StorageOpenError;
    case ScrollSettingsStorageWriteStatus::WriteError:
      return ScrollSettingsSaveStatus::StorageWriteError;
    case ScrollSettingsStorageWriteStatus::CommitError:
      return ScrollSettingsSaveStatus::StorageCommitError;
    case ScrollSettingsStorageWriteStatus::VerifyError:
      return ScrollSettingsSaveStatus::StorageVerifyError;
  }
  return ScrollSettingsSaveStatus::StorageWriteError;
}

ScrollSettingsEraseStatus mapEraseStatus(
    ScrollSettingsStorageEraseStatus status) {
  switch (status) {
    case ScrollSettingsStorageEraseStatus::Erased:
      return ScrollSettingsEraseStatus::Erased;
    case ScrollSettingsStorageEraseStatus::Missing:
      return ScrollSettingsEraseStatus::Unchanged;
    case ScrollSettingsStorageEraseStatus::OpenError:
      return ScrollSettingsEraseStatus::StorageOpenError;
    case ScrollSettingsStorageEraseStatus::EraseError:
      return ScrollSettingsEraseStatus::StorageEraseError;
    case ScrollSettingsStorageEraseStatus::CommitError:
      return ScrollSettingsEraseStatus::StorageCommitError;
    case ScrollSettingsStorageEraseStatus::VerifyError:
      return ScrollSettingsEraseStatus::StorageVerifyError;
  }
  return ScrollSettingsEraseStatus::StorageEraseError;
}

}  // namespace

static_assert(kCrcOffset + sizeof(uint32_t) == kScrollSettingsRecordSize,
              "scroll settings record layout mismatch");

bool equalScrollSettings(const ScrollSettings& left,
                         const ScrollSettings& right) {
  return left.enabled == right.enabled && left.speed == right.speed &&
         left.pauseMs == right.pauseMs;
}

uint32_t scrollSettingsRecordCrc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  if (!data && size) return 0;
  for (size_t index = 0; index < size; ++index) {
    crc ^= data[index];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return crc ^ 0xffffffffu;
}

bool serializeScrollSettingsRecord(const ScrollSettings& settings,
                                   uint8_t* output, size_t outputSize) {
  if (!output || outputSize != kScrollSettingsRecordSize ||
      !isValidScrollSettings(settings)) return false;

  uint8_t record[kScrollSettingsRecordSize]{};
  memcpy(record + kMagicOffset, kMagic, sizeof(kMagic));
  record[kVersionOffset] = kScrollSettingsRecordVersion;
  record[kEnabledOffset] = settings.enabled ? 1 : 0;
  record[kSpeedOffset] = static_cast<uint8_t>(settings.speed);
  write16(record + kPauseOffset, settings.pauseMs);
  write32(record + kCrcOffset,
          scrollSettingsRecordCrc32(record, kCrcOffset));
  memcpy(output, record, sizeof(record));
  return true;
}

ScrollSettingsRecordStatus deserializeScrollSettingsRecord(
    const uint8_t* input, size_t inputSize, ScrollSettings& output) {
  if (!input || inputSize != kScrollSettingsRecordSize)
    return ScrollSettingsRecordStatus::InvalidSize;
  if (memcmp(input + kMagicOffset, kMagic, sizeof(kMagic)) != 0)
    return ScrollSettingsRecordStatus::InvalidMagic;
  if (input[kVersionOffset] != kScrollSettingsRecordVersion)
    return ScrollSettingsRecordStatus::InvalidVersion;
  if (read32(input + kCrcOffset) !=
      scrollSettingsRecordCrc32(input, kCrcOffset))
    return ScrollSettingsRecordStatus::InvalidCrc;
  if (!isValidScrollEnabledValue(input[kEnabledOffset]))
    return ScrollSettingsRecordStatus::InvalidEnabled;
  const ScrollSpeed speed = static_cast<ScrollSpeed>(input[kSpeedOffset]);
  if (!isValidScrollSpeed(speed))
    return ScrollSettingsRecordStatus::InvalidSpeed;
  const uint16_t pauseMs = read16(input + kPauseOffset);
  if (!isValidScrollPause(pauseMs))
    return ScrollSettingsRecordStatus::InvalidPause;

  output = ScrollSettings{input[kEnabledOffset] != 0, speed, pauseMs};
  return ScrollSettingsRecordStatus::Valid;
}

ScrollSettingsLoadResult loadScrollSettings(ScrollSettingsStorage& storage) {
  uint8_t record[kScrollSettingsRecordSize]{};
  size_t size = 0;
  const ScrollSettingsStorageReadStatus read =
      storage.read(record, sizeof(record), size);
  if (read == ScrollSettingsStorageReadStatus::Missing)
    return rendererDefaults(
        ScrollSettingsLoadStatus::RendererDefaultsMissing);
  if (read == ScrollSettingsStorageReadStatus::OpenError)
    return rendererDefaults(
        ScrollSettingsLoadStatus::RendererDefaultsOpenError);
  if (read == ScrollSettingsStorageReadStatus::ReadError)
    return rendererDefaults(
        ScrollSettingsLoadStatus::RendererDefaultsReadError);

  ScrollSettings settings = defaultScrollSettings();
  const ScrollSettingsRecordStatus decoded =
      deserializeScrollSettingsRecord(record, size, settings);
  if (decoded != ScrollSettingsRecordStatus::Valid)
    return rendererDefaults(ScrollSettingsLoadStatus::RendererDefaultsInvalid,
                            decoded);
  return ScrollSettingsLoadResult{settings, ScrollSettingsSource::UserSettings,
                                  ScrollSettingsLoadStatus::UserSettings,
                                  ScrollSettingsRecordStatus::Valid};
}

ScrollSettingsSaveStatus saveScrollSettings(
    ScrollSettingsStorage& storage, const ScrollSettingsLoadResult& current,
    const ScrollSettings& requested) {
  if (!isValidScrollSettings(requested))
    return ScrollSettingsSaveStatus::Invalid;
  if (current.source == ScrollSettingsSource::UserSettings &&
      current.recordStatus == ScrollSettingsRecordStatus::Valid &&
      equalScrollSettings(current.settings, requested))
    return ScrollSettingsSaveStatus::Unchanged;

  uint8_t record[kScrollSettingsRecordSize]{};
  if (!serializeScrollSettingsRecord(requested, record, sizeof(record)))
    return ScrollSettingsSaveStatus::Invalid;
  return mapWriteStatus(storage.write(record, sizeof(record)));
}

ScrollSettingsEraseStatus eraseScrollSettings(ScrollSettingsStorage& storage) {
  return mapEraseStatus(storage.erase());
}

}  // namespace display_scroll
}  // namespace voxone

// Consolidated from display_scroll_persistence_nvs.cpp.
#if defined(ARDUINO)
#include <nvs.h>

#include <string.h>

namespace voxone {
namespace display_scroll {
namespace {

constexpr char kNamespace[] = "voxscroll";
constexpr char kRecordKey[] = "settings";

class NvsScrollSettingsStorage : public ScrollSettingsStorage {
 public:
  ScrollSettingsStorageReadStatus read(uint8_t* output, size_t capacity,
                                       size_t& size) override {
    size = 0;
    nvs_handle_t handle;
    const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND)
      return ScrollSettingsStorageReadStatus::Missing;
    if (opened != ESP_OK) return ScrollSettingsStorageReadStatus::OpenError;

    size_t storedSize = 0;
    esp_err_t result = nvs_get_blob(handle, kRecordKey, nullptr, &storedSize);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
      nvs_close(handle);
      return ScrollSettingsStorageReadStatus::Missing;
    }
    if (result != ESP_OK) {
      nvs_close(handle);
      return ScrollSettingsStorageReadStatus::ReadError;
    }
    size = storedSize;
    if (storedSize > capacity) {
      nvs_close(handle);
      return ScrollSettingsStorageReadStatus::Found;
    }
    if (storedSize && !output) {
      nvs_close(handle);
      return ScrollSettingsStorageReadStatus::ReadError;
    }
    result = nvs_get_blob(handle, kRecordKey, output, &storedSize);
    nvs_close(handle);
    size = storedSize;
    return result == ESP_OK ? ScrollSettingsStorageReadStatus::Found
                            : ScrollSettingsStorageReadStatus::ReadError;
  }

  ScrollSettingsStorageWriteStatus write(const uint8_t* input,
                                          size_t size) override {
    if (!input || size != kScrollSettingsRecordSize)
      return ScrollSettingsStorageWriteStatus::WriteError;
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK)
      return ScrollSettingsStorageWriteStatus::OpenError;
    if (nvs_set_blob(handle, kRecordKey, input, size) != ESP_OK) {
      nvs_close(handle);
      return ScrollSettingsStorageWriteStatus::WriteError;
    }
    if (nvs_commit(handle) != ESP_OK) {
      nvs_close(handle);
      return ScrollSettingsStorageWriteStatus::CommitError;
    }

    uint8_t verified[kScrollSettingsRecordSize]{};
    size_t verifiedSize = sizeof(verified);
    const bool verifiedOk =
        nvs_get_blob(handle, kRecordKey, verified, &verifiedSize) == ESP_OK &&
        verifiedSize == size && memcmp(input, verified, size) == 0;
    nvs_close(handle);
    return verifiedOk ? ScrollSettingsStorageWriteStatus::Stored
                      : ScrollSettingsStorageWriteStatus::VerifyError;
  }

  ScrollSettingsStorageEraseStatus erase() override {
    nvs_handle_t handle;
    const esp_err_t opened = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND)
      return ScrollSettingsStorageEraseStatus::Missing;
    if (opened != ESP_OK) return ScrollSettingsStorageEraseStatus::OpenError;
    const esp_err_t erased = nvs_erase_key(handle, kRecordKey);
    if (erased == ESP_ERR_NVS_NOT_FOUND) {
      nvs_close(handle);
      return ScrollSettingsStorageEraseStatus::Missing;
    }
    if (erased != ESP_OK) {
      nvs_close(handle);
      return ScrollSettingsStorageEraseStatus::EraseError;
    }
    if (nvs_commit(handle) != ESP_OK) {
      nvs_close(handle);
      return ScrollSettingsStorageEraseStatus::CommitError;
    }
    size_t size = 0;
    const esp_err_t verified =
        nvs_get_blob(handle, kRecordKey, nullptr, &size);
    nvs_close(handle);
    return verified == ESP_ERR_NVS_NOT_FOUND
               ? ScrollSettingsStorageEraseStatus::Erased
               : ScrollSettingsStorageEraseStatus::VerifyError;
  }
};

}  // namespace

ScrollSettingsLoadResult readScrollSettingsPersistent() {
  NvsScrollSettingsStorage storage;
  return loadScrollSettings(storage);
}

ScrollSettingsSaveStatus writeScrollSettingsPersistent(
    const ScrollSettingsLoadResult& current,
    const ScrollSettings& requested) {
  NvsScrollSettingsStorage storage;
  return saveScrollSettings(storage, current, requested);
}

ScrollSettingsEraseStatus eraseScrollSettingsPersistent() {
  NvsScrollSettingsStorage storage;
  return eraseScrollSettings(storage);
}

}  // namespace display_scroll
}  // namespace voxone
#endif  // ARDUINO
// Consolidated from display_scroll_runtime_config.cpp.
namespace voxone {
namespace display_scroll {
namespace {

constexpr uint8_t kEnabledShift = 0;
constexpr uint8_t kSpeedShift = 1;
constexpr uint8_t kPauseShift = 3;
constexpr uint8_t kSourceShift = 6;
constexpr uint8_t kRevisionShift = 7;
constexpr uint32_t kRevisionMask = 0x01ffffffu;

constexpr uint32_t encodeState(const ScrollSettings& settings,
                               ScrollSettingsSource source,
                               uint32_t revision) {
  return (settings.enabled ? 1u : 0u) << kEnabledShift |
         static_cast<uint32_t>(settings.speed) << kSpeedShift |
         static_cast<uint32_t>(settings.pauseMs / kPauseStepMs)
             << kPauseShift |
         static_cast<uint32_t>(source) << kSourceShift |
         (revision & kRevisionMask) << kRevisionShift;
}

ScrollSettingsRuntimeSnapshot decodeState(uint32_t state) {
  return ScrollSettingsRuntimeSnapshot{
      ScrollSettings{((state >> kEnabledShift) & 1u) != 0,
                     static_cast<ScrollSpeed>((state >> kSpeedShift) & 3u),
                     static_cast<uint16_t>(((state >> kPauseShift) & 7u) *
                                           kPauseStepMs)},
      static_cast<ScrollSettingsSource>((state >> kSourceShift) & 1u),
      (state >> kRevisionShift) & kRevisionMask};
}

bool sameRuntimeConfiguration(const ScrollSettingsRuntimeSnapshot& current,
                              const ScrollSettings& settings,
                              ScrollSettingsSource source) {
  return current.source == source &&
         equalScrollSettings(current.settings, settings);
}

ScrollSettingsLoadResult safeReadFailure() {
  return ScrollSettingsLoadResult{
      defaultScrollSettings(), ScrollSettingsSource::RendererDefaults,
      ScrollSettingsLoadStatus::RendererDefaultsReadError,
      ScrollSettingsRecordStatus::NotDecoded};
}

bool isValidatedUserSettings(const ScrollSettingsLoadResult& loaded) {
  return loaded.status == ScrollSettingsLoadStatus::UserSettings &&
         loaded.source == ScrollSettingsSource::UserSettings &&
         loaded.recordStatus == ScrollSettingsRecordStatus::Valid &&
         isValidScrollSettings(loaded.settings);
}

}  // namespace

ScrollSettingsRuntimeConfig::ScrollSettingsRuntimeConfig()
    : state_(encodeState(defaultScrollSettings(),
                         ScrollSettingsSource::RendererDefaults, 0)) {}

ScrollSettingsRuntimeSnapshot ScrollSettingsRuntimeConfig::snapshot() const {
  return decodeState(state_.load(std::memory_order_acquire));
}

ScrollSettingsPublishStatus ScrollSettingsRuntimeConfig::publish(
    const ScrollSettings& settings, ScrollSettingsSource source) {
  if (!isValidScrollSettings(settings) ||
      !isValidScrollSettingsSource(source))
    return ScrollSettingsPublishStatus::Invalid;

  uint32_t observed = state_.load(std::memory_order_acquire);
  for (;;) {
    const ScrollSettingsRuntimeSnapshot current = decodeState(observed);
    if (sameRuntimeConfiguration(current, settings, source))
      return ScrollSettingsPublishStatus::Unchanged;
    const uint32_t nextRevision =
        (current.revision + 1u) & kRevisionMask;
    const uint32_t desired = encodeState(settings, source, nextRevision);
    if (state_.compare_exchange_weak(observed, desired,
                                     std::memory_order_release,
                                     std::memory_order_acquire))
      return ScrollSettingsPublishStatus::Changed;
  }
}

ScrollSettingsRuntimeConfig& scrollSettingsRuntimeConfig() {
  static ScrollSettingsRuntimeConfig runtime;
  return runtime;
}

ScrollSettingsLoadResult loadScrollSettingsRuntime(
    ScrollSettingsRuntimeConfig& runtime,
    ScrollSettingsPersistentReader reader) {
  const ScrollSettingsLoadResult loaded = reader ? reader() : safeReadFailure();
  if (isValidatedUserSettings(loaded)) {
    (void)runtime.publish(loaded.settings, ScrollSettingsSource::UserSettings);
  } else {
    (void)runtime.publish(defaultScrollSettings(),
                          ScrollSettingsSource::RendererDefaults);
  }
  return loaded;
}

}  // namespace display_scroll
}  // namespace voxone

// Consolidated from display_scroll_runtime_config_nvs.cpp.
#if defined(ARDUINO)
namespace voxone {
namespace display_scroll {

ScrollSettingsLoadResult initializeScrollSettingsRuntimePersistent() {
  return loadScrollSettingsRuntime(scrollSettingsRuntimeConfig(),
                                   readScrollSettingsPersistent);
}

ScrollSettingsLoadResult reloadScrollSettingsRuntimePersistent() {
  return loadScrollSettingsRuntime(scrollSettingsRuntimeConfig(),
                                   readScrollSettingsPersistent);
}

}  // namespace display_scroll
}  // namespace voxone
#endif  // ARDUINO
// Consolidated from display_scroll_api.cpp.
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

// Consolidated from display_scroll_api_runtime.cpp.
#if defined(ARDUINO)
#include "player_scroll_runtime.h"
#include "../displays/display_backend_selection.h"
#include "../hardware/hardware_descriptor.h"
#include "../hardware/runtime_hardware_config.h"

namespace voxone {
namespace display_scroll {
namespace {

ScrollRendererProfile scrollProfile(
    display_profile::ProfileId profile) {
  switch (profile) {
    case display_profile::ProfileId::Oled128x64:
      return ScrollRendererProfile::Oled128x64;
    case display_profile::ProfileId::St7789_284x76:
      return ScrollRendererProfile::St7789_284x76;
    case display_profile::ProfileId::St7796_480x320:
      return ScrollRendererProfile::St7796_480x320;
    case display_profile::ProfileId::Oled256x64:
    case display_profile::ProfileId::Gc9a01_240x240:
    case display_profile::ProfileId::None:
      return ScrollRendererProfile::None;
  }
  return ScrollRendererProfile::None;
}

ScrollSettingsLoadResult productionRead(void*) {
  return readScrollSettingsPersistent();
}

ScrollSettingsSaveStatus productionSave(
    void*, const ScrollSettingsLoadResult& current,
    const ScrollSettings& requested) {
  return writeScrollSettingsPersistent(current, requested);
}

ScrollSettingsLoadResult productionReload(void*) {
  return reloadScrollSettingsRuntimePersistent();
}

ScrollSettingsRuntimeSnapshot productionSnapshot(void*) {
  return scrollSettingsRuntimeConfig().snapshot();
}

}  // namespace

ScrollSettingsWebView currentScrollSettingsWebView() {
  const display_backend::Selection selected = display_backend::select(
      hardware::runtimeHardwareConfig(), hardware::currentHardware(),
      display_backend::compiledBackends(hardware::currentHardware()));
  return makeScrollSettingsWebView(
      scrollProfile(selected.profile), scrollSettingsRuntimeConfig().snapshot(),
      playerScrollCoreBuildEnabled());
}

ScrollSettingsApplyResult applyPersistentScrollSettings(
    const ScrollSettings& requested, bool coreActive) {
  const ScrollSettingsApiOperations operations{
      nullptr, productionRead, productionSave, productionReload,
      productionSnapshot};
  return applyScrollSettingsUpdate(requested, coreActive, operations);
}

}  // namespace display_scroll
}  // namespace voxone
#endif  // ARDUINO
