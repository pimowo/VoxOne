#include "../src/core/display_scroll_api.h"
#include "../src/core/player_scroll_runtime.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

using namespace voxone::display_scroll;

namespace {

ScrollSettingsLoadResult userResult(const ScrollSettings& settings) {
  return ScrollSettingsLoadResult{
      settings, ScrollSettingsSource::UserSettings,
      ScrollSettingsLoadStatus::UserSettings,
      ScrollSettingsRecordStatus::Valid};
}

ScrollSettingsLoadResult fallbackResult(ScrollSettingsLoadStatus status) {
  return ScrollSettingsLoadResult{
      defaultScrollSettings(), ScrollSettingsSource::RendererDefaults,
      status, ScrollSettingsRecordStatus::NotDecoded};
}

struct FakeOperations {
  ScrollSettingsLoadResult current = fallbackResult(
      ScrollSettingsLoadStatus::RendererDefaultsMissing);
  ScrollSettingsLoadResult reloaded = userResult(defaultScrollSettings());
  ScrollSettingsRuntimeSnapshot published{
      defaultScrollSettings(), ScrollSettingsSource::UserSettings, 1};
  ScrollSettingsSaveStatus saveStatus = ScrollSettingsSaveStatus::Saved;
  std::string calls;
};

class MemoryScrollStorage final : public ScrollSettingsStorage {
 public:
  ScrollSettingsStorageReadStatus read(uint8_t* output, size_t capacity,
                                       size_t& outputSize) override {
    if (size == 0) {
      outputSize = 0;
      return ScrollSettingsStorageReadStatus::Missing;
    }
    assert(output && capacity >= size);
    std::memcpy(output, bytes, size);
    outputSize = size;
    return ScrollSettingsStorageReadStatus::Found;
  }

  ScrollSettingsStorageWriteStatus write(const uint8_t* input,
                                          size_t inputSize) override {
    assert(input && inputSize == kScrollSettingsRecordSize);
    std::memcpy(bytes, input, inputSize);
    size = inputSize;
    return ScrollSettingsStorageWriteStatus::Stored;
  }

  ScrollSettingsStorageEraseStatus erase() override {
    size = 0;
    return ScrollSettingsStorageEraseStatus::Erased;
  }

  uint8_t bytes[kScrollSettingsRecordSize]{};
  size_t size = 0;
};

struct IntegratedSettingsPath {
  MemoryScrollStorage storage;
  ScrollSettingsRuntimeConfig runtime;
};

ScrollSettingsLoadResult integratedRead(void* context) {
  return loadScrollSettings(
      static_cast<IntegratedSettingsPath*>(context)->storage);
}

ScrollSettingsSaveStatus integratedSave(
    void* context, const ScrollSettingsLoadResult& current,
    const ScrollSettings& requested) {
  return saveScrollSettings(
      static_cast<IntegratedSettingsPath*>(context)->storage, current,
      requested);
}

ScrollSettingsLoadResult integratedReload(void* context) {
  auto& path = *static_cast<IntegratedSettingsPath*>(context);
  const ScrollSettingsLoadResult loaded = loadScrollSettings(path.storage);
  const ScrollSettings settings =
      loaded.source == ScrollSettingsSource::UserSettings
          ? loaded.settings
          : defaultScrollSettings();
  assert(path.runtime.publish(settings, loaded.source) !=
         ScrollSettingsPublishStatus::Invalid);
  return loaded;
}

ScrollSettingsRuntimeSnapshot integratedSnapshot(void* context) {
  return static_cast<IntegratedSettingsPath*>(context)->runtime.snapshot();
}

ScrollSettingsApiOperations integratedOperations(
    IntegratedSettingsPath& path) {
  return ScrollSettingsApiOperations{&path, integratedRead, integratedSave,
                                     integratedReload, integratedSnapshot};
}

struct ScrollRenderProbe {
  PlayerScrollRowMetrics metrics{1, 100, 220, 250, 1};
  bool coreControlled = false;
  uint32_t renderCalls = 0;
  uint16_t lastOffset = 0;
};

PlayerScrollRowMetrics probeMetrics(const void* context) {
  return static_cast<const ScrollRenderProbe*>(context)->metrics;
}

void probeCoreControlled(void* context, bool enabled) {
  auto& probe = *static_cast<ScrollRenderProbe*>(context);
  probe.coreControlled = enabled;
  probe.lastOffset = 0;
}

bool probeRenderOffset(void* context, uint16_t offset) {
  auto& probe = *static_cast<ScrollRenderProbe*>(context);
  assert(probe.coreControlled);
  ++probe.renderCalls;
  probe.lastOffset = offset;
  return true;
}

PlayerScrollRuntimeTick runL0UntilFirstMotion(
    const ScrollSettingsRuntimeSnapshot& snapshot, ScrollRenderProbe& probe) {
  PlayerScrollRuntime runtime;
  assert(runtime.configure(true, ScrollRendererProfile::St7796_480x320,
                           snapshot.settings, snapshot.source));
  const PlayerScrollWidgetBinding binding{
      &probe, probeMetrics, probeCoreControlled, probeRenderOffset};
  assert(runtime.setRows(&binding, 1));
  runtime.enter(0);
  const PlayerScrollRuntimeTick reset = runtime.tick(0);
  assert(reset.renderRequested && reset.command.offsetPixels == 0);
  if (!runtime.controller().parameters().enabled) {
    const PlayerScrollRuntimeTick disabled = runtime.tick(100000);
    assert(!disabled.renderRequested && probe.lastOffset == 0);
    return disabled;
  }
  const uint16_t pause = runtime.controller().parameters().pauseMs;
  const uint16_t interval = runtime.controller().parameters().intervalMs;
  if (pause != 0) {
    assert(!runtime.tick(static_cast<uint32_t>(pause - 1)).renderRequested);
    assert(!runtime.tick(pause).renderRequested);
  }
  assert(!runtime.tick(static_cast<uint32_t>(pause + interval - 1))
              .renderRequested);
  return runtime.tick(static_cast<uint32_t>(pause + interval));
}

ScrollSettingsLoadResult fakeRead(void* context) {
  auto& fake = *static_cast<FakeOperations*>(context);
  fake.calls += 'R';
  return fake.current;
}

ScrollSettingsSaveStatus fakeSave(
    void* context, const ScrollSettingsLoadResult&,
    const ScrollSettings&) {
  auto& fake = *static_cast<FakeOperations*>(context);
  fake.calls += 'S';
  return fake.saveStatus;
}

ScrollSettingsLoadResult fakeReload(void* context) {
  auto& fake = *static_cast<FakeOperations*>(context);
  fake.calls += 'L';
  return fake.reloaded;
}

ScrollSettingsRuntimeSnapshot fakeSnapshot(void* context) {
  auto& fake = *static_cast<FakeOperations*>(context);
  fake.calls += 'V';
  return fake.published;
}

ScrollSettingsApiOperations operations(FakeOperations& fake) {
  return ScrollSettingsApiOperations{
      &fake, fakeRead, fakeSave, fakeReload, fakeSnapshot};
}

std::string readFile(const char* path) {
  std::ifstream stream(path);
  assert(stream.good());
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

std::string section(const std::string& source, const char* begin,
                    const char* end) {
  const std::size_t first = source.find(begin);
  assert(first != std::string::npos);
  const std::size_t last = source.find(end, first + strlen(begin));
  assert(last != std::string::npos);
  return source.substr(first, last - first);
}

void testCommandMatrixAndValidation() {
  const char* speeds[] = {"slow", "normal", "fast"};
  const ScrollSpeed speedValues[] = {ScrollSpeed::Slow, ScrollSpeed::Normal,
                                     ScrollSpeed::Fast};
  const uint16_t pauses[] = {0, 1000, 2000, 3000, 4000, 5000};
  char text[32];
  for (uint8_t enabled = 0; enabled < 2; ++enabled) {
    for (size_t speed = 0; speed < 3; ++speed) {
      for (uint16_t pause : pauses) {
        snprintf(text, sizeof(text), "%s,%s,%u",
                 enabled ? "true" : "false", speeds[speed], pause);
        ScrollSettings parsed{};
        assert(parseScrollSettingsCommand(text, parsed) ==
               ScrollSettingsCommandStatus::Ok);
        assert(parsed.enabled == (enabled != 0));
        assert(parsed.speed == speedValues[speed]);
        assert(parsed.pauseMs == pause);
      }
    }
  }

  const char* invalidStructure[] = {
      nullptr, "", "true", "true,normal", "true,,5000",
      ",normal,5000", "true,normal,", "true,normal,5000,extra"};
  for (const char* value : invalidStructure) {
    ScrollSettings parsed{};
    assert(parseScrollSettingsCommand(value, parsed) ==
           ScrollSettingsCommandStatus::InvalidStructure);
  }
  ScrollSettings parsed{};
  assert(parseScrollSettingsCommand("1,normal,5000", parsed) ==
         ScrollSettingsCommandStatus::InvalidEnabled);
  assert(parseScrollSettingsCommand("TRUE,normal,5000", parsed) ==
         ScrollSettingsCommandStatus::InvalidEnabled);
  assert(parseScrollSettingsCommand("true,medium,5000", parsed) ==
         ScrollSettingsCommandStatus::InvalidSpeed);
  assert(parseScrollSettingsCommand("true,Normal,5000", parsed) ==
         ScrollSettingsCommandStatus::InvalidSpeed);
  const char* invalidPauses[] = {"500", "2500", "6000", "05000", "+1000",
                                 "1000 ", "-1"};
  for (const char* pause : invalidPauses) {
    snprintf(text, sizeof(text), "true,normal,%s", pause);
    assert(parseScrollSettingsCommand(text, parsed) ==
           ScrollSettingsCommandStatus::InvalidPause);
  }
}

void testWebViews() {
  const ScrollSettingsRuntimeSnapshot fallback{
      ScrollSettings{false, ScrollSpeed::Fast, 1000},
      ScrollSettingsSource::RendererDefaults, 8};
  const auto none = makeScrollSettingsWebView(
      ScrollRendererProfile::None, fallback, true);
  assert(!none.canScrollSettings && !none.scrollCoreActive);

  const auto oled = makeScrollSettingsWebView(
      ScrollRendererProfile::Oled128x64, fallback, false);
  assert(oled.canScrollSettings && !oled.scrollCoreActive);
  assert(oled.settings.enabled && oled.settings.speed == ScrollSpeed::Normal &&
         oled.settings.pauseMs == 5000);

  const auto st7789 = makeScrollSettingsWebView(
      ScrollRendererProfile::St7789_284x76, fallback, true);
  assert(st7789.canScrollSettings && st7789.scrollCoreActive);
  assert(st7789.settings.pauseMs == 5000);

  const auto st7796 = makeScrollSettingsWebView(
      ScrollRendererProfile::St7796_480x320, fallback, false);
  assert(st7796.canScrollSettings && !st7796.scrollCoreActive);
  assert(st7796.settings.pauseMs == 2500);

  const ScrollSettings user{false, ScrollSpeed::Slow, 0};
  const auto configured = makeScrollSettingsWebView(
      ScrollRendererProfile::St7796_480x320,
      ScrollSettingsRuntimeSnapshot{
          user, ScrollSettingsSource::UserSettings, 9},
      false);
  assert(equalScrollSettings(configured.settings, user));
  assert(configured.source == ScrollSettingsSource::UserSettings);
}

void testApplyOrderAndFailures() {
  const ScrollSettings requested{false, ScrollSpeed::Fast, 3000};
  FakeOperations fake;
  fake.reloaded = userResult(requested);
  fake.published = ScrollSettingsRuntimeSnapshot{
      requested, ScrollSettingsSource::UserSettings, 2};
  auto result = applyScrollSettingsUpdate(requested, false,
                                           operations(fake));
  assert(result.status == ScrollSettingsApplyStatus::Applied);
  assert(result.changed && result.persisted && result.runtimePublished);
  assert(!result.scrollCoreActive && fake.calls == "RSLV");

  fake = FakeOperations{};
  fake.current = userResult(requested);
  fake.reloaded = userResult(requested);
  fake.published = ScrollSettingsRuntimeSnapshot{
      requested, ScrollSettingsSource::UserSettings, 4};
  fake.saveStatus = ScrollSettingsSaveStatus::Unchanged;
  result = applyScrollSettingsUpdate(requested, true, operations(fake));
  assert(result.status == ScrollSettingsApplyStatus::Applied);
  assert(!result.changed && result.persisted && result.runtimePublished);
  assert(result.scrollCoreActive && fake.calls == "RSLV");

  const struct {
    ScrollSettingsSaveStatus save;
    ScrollSettingsApplyStatus expected;
  } storageFailures[] = {
      {ScrollSettingsSaveStatus::Invalid,
       ScrollSettingsApplyStatus::Invalid},
      {ScrollSettingsSaveStatus::StorageOpenError,
       ScrollSettingsApplyStatus::StorageOpenFailed},
      {ScrollSettingsSaveStatus::StorageWriteError,
       ScrollSettingsApplyStatus::StorageWriteFailed},
      {ScrollSettingsSaveStatus::StorageCommitError,
       ScrollSettingsApplyStatus::StorageCommitFailed},
      {ScrollSettingsSaveStatus::StorageVerifyError,
       ScrollSettingsApplyStatus::StorageVerifyFailed}};
  for (const auto& failure : storageFailures) {
    fake = FakeOperations{};
    fake.saveStatus = failure.save;
    result = applyScrollSettingsUpdate(requested, true, operations(fake));
    assert(result.status == failure.expected);
    assert(!result.persisted && !result.runtimePublished);
    assert(fake.calls == "RS");
  }

  for (ScrollSettingsLoadStatus readStatus : {
           ScrollSettingsLoadStatus::RendererDefaultsOpenError,
           ScrollSettingsLoadStatus::RendererDefaultsReadError}) {
    fake = FakeOperations{};
    fake.current = fallbackResult(readStatus);
    result = applyScrollSettingsUpdate(requested, true, operations(fake));
    assert(result.status == ScrollSettingsApplyStatus::ReadFailed);
    assert(fake.calls == "R");
  }

  fake = FakeOperations{};
  fake.reloaded = fallbackResult(
      ScrollSettingsLoadStatus::RendererDefaultsReadError);
  result = applyScrollSettingsUpdate(requested, true, operations(fake));
  assert(result.status == ScrollSettingsApplyStatus::ReloadFailed);
  assert(result.persisted && !result.runtimePublished);
  assert(fake.calls == "RSL");

  for (ScrollSettingsRecordStatus invalidRecord : {
           ScrollSettingsRecordStatus::InvalidCrc,
           ScrollSettingsRecordStatus::InvalidVersion,
           ScrollSettingsRecordStatus::InvalidSize}) {
    fake = FakeOperations{};
    fake.reloaded = fallbackResult(
        ScrollSettingsLoadStatus::RendererDefaultsInvalid);
    fake.reloaded.recordStatus = invalidRecord;
    result = applyScrollSettingsUpdate(requested, true, operations(fake));
    assert(result.status == ScrollSettingsApplyStatus::ReloadFailed);
    assert(result.persisted && !result.runtimePublished);
    assert(fake.calls == "RSL");
  }

  fake = FakeOperations{};
  fake.reloaded = userResult(requested);
  fake.published = ScrollSettingsRuntimeSnapshot{
      ScrollSettings{true, ScrollSpeed::Slow, 1000},
      ScrollSettingsSource::UserSettings, 7};
  result = applyScrollSettingsUpdate(requested, true, operations(fake));
  assert(result.status == ScrollSettingsApplyStatus::RuntimeVerifyFailed);
  assert(result.persisted && !result.runtimePublished);
  assert(fake.calls == "RSLV");

  fake = FakeOperations{};
  result = applyScrollSettingsUpdate(
      ScrollSettings{true, static_cast<ScrollSpeed>(9), 5000}, true,
      operations(fake));
  assert(result.status == ScrollSettingsApplyStatus::Invalid);
  assert(fake.calls.empty());
}

void testSequentialUpdatesAndResults() {
  const ScrollSettings first{true, ScrollSpeed::Slow, 1000};
  const ScrollSettings second{false, ScrollSpeed::Fast, 5000};
  for (const ScrollSettings& requested : {first, second}) {
    FakeOperations fake;
    fake.reloaded = userResult(requested);
    fake.published = ScrollSettingsRuntimeSnapshot{
        requested, ScrollSettingsSource::UserSettings, 2};
    assert(applyScrollSettingsUpdate(requested, true, operations(fake)).status ==
           ScrollSettingsApplyStatus::Applied);
  }

  char json[224];
  assert(formatScrollSettingsResultJson(
      json, sizeof(json),
      ScrollSettingsApplyResult{ScrollSettingsApplyStatus::Applied, true,
                                true, true, false}));
  assert(strstr(json, "\"ok\":true") != nullptr);
  assert(strstr(json, "\"persisted\":true") != nullptr);
  assert(strstr(json, "\"runtimePublished\":true") != nullptr);
  assert(strstr(json, "\"activeApplied\":false") != nullptr);
  assert(strstr(json, "\"scrollCoreActive\":false") != nullptr);
  assert(formatScrollSettingsResultJson(
      json, sizeof(json),
      ScrollSettingsApplyResult{ScrollSettingsApplyStatus::ReloadFailed,
                                true, true, false, true}));
  assert(strstr(json, "\"ok\":false") != nullptr);
  assert(strstr(json, "\"error\":\"reload_failed\"") != nullptr);
  assert(strstr(json, "\"activeApplied\":false") != nullptr);
}

void testL0PersistentSettingsDriveRenderer() {
  IntegratedSettingsPath path;

  const ScrollSettingsLoadResult missing = loadScrollSettings(path.storage);
  assert(missing.status ==
         ScrollSettingsLoadStatus::RendererDefaultsMissing);
  assert(missing.source == ScrollSettingsSource::RendererDefaults);
  const auto defaultView = makeScrollSettingsWebView(
      ScrollRendererProfile::St7796_480x320, path.runtime.snapshot(), true);
  assert(defaultView.canScrollSettings && defaultView.scrollCoreActive);
  assert(defaultView.settings.enabled);
  assert(defaultView.settings.speed == ScrollSpeed::Normal);
  assert(defaultView.settings.pauseMs == 2500);

  ScrollRenderProbe defaultProbe;
  const PlayerScrollRuntimeTick defaultMotion =
      runL0UntilFirstMotion(path.runtime.snapshot(), defaultProbe);
  assert(defaultMotion.renderRequested && defaultMotion.renderAccepted);
  assert(defaultMotion.command.offsetPixels == 2);
  assert(defaultProbe.lastOffset == 2 && defaultProbe.renderCalls == 2);

  const char* speeds[] = {"slow", "normal", "fast"};
  const ScrollSpeed speedValues[] = {ScrollSpeed::Slow, ScrollSpeed::Normal,
                                     ScrollSpeed::Fast};
  const uint16_t pauses[] = {0, 1000, 2000, 3000, 4000, 5000};
  char command[32];
  for (size_t speed = 0; speed < 3; ++speed) {
    for (uint16_t pause : pauses) {
      std::snprintf(command, sizeof(command), "true,%s,%u", speeds[speed],
                    pause);
      ScrollSettings requested{};
      assert(parseScrollSettingsCommand(command, requested) ==
             ScrollSettingsCommandStatus::Ok);
      const ScrollSettingsApplyResult applied = applyScrollSettingsUpdate(
          requested, true, integratedOperations(path));
      assert(applied.status == ScrollSettingsApplyStatus::Applied);
      assert(applied.persisted && applied.runtimePublished &&
             applied.scrollCoreActive);

      const ScrollSettingsLoadResult persisted =
          loadScrollSettings(path.storage);
      assert(persisted.status == ScrollSettingsLoadStatus::UserSettings);
      assert(persisted.recordStatus == ScrollSettingsRecordStatus::Valid);
      assert(equalScrollSettings(persisted.settings, requested));
      const ScrollSettingsRuntimeSnapshot snapshot = path.runtime.snapshot();
      assert(snapshot.source == ScrollSettingsSource::UserSettings);
      assert(equalScrollSettings(snapshot.settings, requested));

      const auto view = makeScrollSettingsWebView(
          ScrollRendererProfile::St7796_480x320, snapshot, true);
      assert(view.canScrollSettings && view.scrollCoreActive);
      assert(equalScrollSettings(view.settings, requested));

      ScrollRenderProbe probe;
      const PlayerScrollRuntimeTick motion =
          runL0UntilFirstMotion(snapshot, probe);
      const ScrollMotion expected = scrollMotion(
          ScrollRendererProfile::St7796_480x320, speedValues[speed]);
      assert(motion.renderRequested && motion.renderAccepted);
      assert(motion.command.offsetPixels == expected.stepPixels);
      assert(probe.lastOffset == expected.stepPixels);
    }
  }

  ScrollSettings disabled{};
  assert(parseScrollSettingsCommand("false,fast,5000", disabled) ==
         ScrollSettingsCommandStatus::Ok);
  assert(applyScrollSettingsUpdate(disabled, true, integratedOperations(path))
             .status == ScrollSettingsApplyStatus::Applied);
  ScrollRenderProbe disabledProbe;
  const PlayerScrollRuntimeTick disabledTick =
      runL0UntilFirstMotion(path.runtime.snapshot(), disabledProbe);
  assert(!disabledTick.renderRequested);
  assert(disabledProbe.lastOffset == 0 && disabledProbe.renderCalls == 1);

  const ScrollSettings normalNoPause{true, ScrollSpeed::Normal, 0};
  assert(applyScrollSettingsUpdate(normalNoPause, true,
                                   integratedOperations(path))
             .status == ScrollSettingsApplyStatus::Applied);
  PlayerScrollRuntime activeRuntime;
  ScrollRenderProbe activeProbe;
  const PlayerScrollWidgetBinding activeBinding{
      &activeProbe, probeMetrics, probeCoreControlled, probeRenderOffset};
  assert(activeRuntime.configure(true,
                                 ScrollRendererProfile::St7796_480x320,
                                 path.runtime.snapshot().settings,
                                 path.runtime.snapshot().source));
  assert(activeRuntime.setRows(&activeBinding, 1));
  activeRuntime.enter(100);
  assert(activeRuntime.tick(100).command.offsetPixels == 0);
  assert(activeRuntime.tick(150).command.offsetPixels == 2);

  const ScrollSettings fastWithPause{true, ScrollSpeed::Fast, 1000};
  assert(applyScrollSettingsUpdate(fastWithPause, true,
                                   integratedOperations(path))
             .status == ScrollSettingsApplyStatus::Applied);
  const ScrollSettingsRuntimeSnapshot changed = path.runtime.snapshot();
  assert(activeRuntime.configure(true,
                                 ScrollRendererProfile::St7796_480x320,
                                 changed.settings, changed.source));
  assert(!activeRuntime.active() && !activeProbe.coreControlled);
  activeRuntime.enter(200);
  assert(activeRuntime.tick(200).command.offsetPixels == 0);
  assert(!activeRuntime.tick(1199).renderRequested);
  assert(!activeRuntime.tick(1200).renderRequested);
  assert(!activeRuntime.tick(1249).renderRequested);
  const PlayerScrollRuntimeTick changedMotion = activeRuntime.tick(1250);
  assert(changedMotion.renderRequested && changedMotion.renderAccepted);
  assert(changedMotion.command.offsetPixels == 4);

  ScrollSettingsRuntimeConfig restartedRuntime;
  const ScrollSettingsLoadResult restarted = loadScrollSettings(path.storage);
  assert(restarted.source == ScrollSettingsSource::UserSettings);
  assert(restarted.recordStatus == ScrollSettingsRecordStatus::Valid);
  assert(restartedRuntime.publish(restarted.settings, restarted.source) ==
         ScrollSettingsPublishStatus::Changed);
  assert(equalScrollSettings(restartedRuntime.snapshot().settings,
                             fastWithPause));

  const size_t savedSize = path.storage.size;
  uint8_t savedRecord[kScrollSettingsRecordSize]{};
  std::memcpy(savedRecord, path.storage.bytes, savedSize);
  const uint32_t savedRevision = path.runtime.snapshot().revision;
  ScrollSettings rejected{};
  assert(parseScrollSettingsCommand("true,normal,2500", rejected) ==
         ScrollSettingsCommandStatus::InvalidPause);
  assert(path.storage.size == savedSize);
  assert(std::memcmp(path.storage.bytes, savedRecord, savedSize) == 0);
  assert(path.runtime.snapshot().revision == savedRevision);
}

void testFirmwareIntegrationContract() {
  const std::string server = readFile("src/core/netserver.cpp");
  const std::string main = readFile("src/main.cpp");
  const std::string security = readFile("src/core/security_http_adapter.cpp");
  const std::string platformio = readFile("platformio.ini");

  const std::string wsCallback = section(
      server, "void NetServer::onWsMessage", "void NetServer::requestOnChange");
  const std::string serverLoop = section(
      server, "void NetServer::loop()", "void NetServer::serviceBtFirmwareUpdate");
  const std::string scrollService = section(
      server, "void NetServer::serviceScrollSettings()",
      "void NetServer::onWsMessage");
  const std::string enqueue = section(
      server, "ScrollCommandQueueStatus enqueueScrollSettingsCommand",
      "WebSocketAuthorization queuedScrollCommandAuthorization");
  const std::string queuedAuthorization = section(
      server, "WebSocketAuthorization queuedScrollCommandAuthorization",
      "void sendScrollSettingsResult");
  assert(wsCallback.find("parseScrollSettingsCommand") != std::string::npos);
  assert(wsCallback.find("enqueueScrollSettingsCommand") != std::string::npos);
  assert(wsCallback.find("applyPersistentScrollSettings") == std::string::npos);
  assert(wsCallback.find("writeScrollSettingsPersistent") == std::string::npos);
  assert(serverLoop.find("serviceScrollSettings") == std::string::npos);
  assert(scrollService.find("applyPersistentScrollSettings") !=
         std::string::npos);
  assert(scrollService.find("queuedScrollCommandAuthorization") !=
         std::string::npos);
  assert(scrollService.find("WebSocketAuthorization::Denied") <
         scrollService.find("applyPersistentScrollSettings"));
  assert(scrollService.find("xQueueReceive") <
         scrollService.find("applyPersistentScrollSettings"));
  assert(enqueue.find("WS_CONNECTED") != std::string::npos);
  assert(enqueue.find("validateBinding") != std::string::npos);
  assert(enqueue.find("xQueueSend") != std::string::npos);
  assert(queuedAuthorization.find("equalSecurityBinding") !=
         std::string::npos);
  assert(queuedAuthorization.find("validateBinding") != std::string::npos);
  assert(main.find("netserver.serviceScrollSettings();") != std::string::npos);
  assert(security.find("\"scrollsettings\"") != std::string::npos);
  assert(server.find("canScrollSettings") != std::string::npos);
  assert(server.find("scrollEnabled") != std::string::npos);
  assert(server.find("scrollSpeed") != std::string::npos);
  assert(server.find("scrollPauseMs") != std::string::npos);
  assert(server.find("scrollSettingsSource") != std::string::npos);
  assert(server.find("scrollCoreActive") != std::string::npos);
  assert(server.find("configurationMutationMutex") != std::string::npos);
  const std::string coreFlag =
      "-DVOXONE_PLAYER_SCROLL_CORE_ENABLED=1";
  const std::string m0Profile = section(
      platformio, "[env:m0]", "[env:m0-security-test]");
  assert(m0Profile.find(coreFlag) != std::string::npos);
  const std::size_t firstFlag = platformio.find(coreFlag);
  const std::size_t secondFlag = platformio.find(coreFlag, firstFlag + 1);
  const std::size_t l0Profile = platformio.find("[env:l0]");
  assert(firstFlag != std::string::npos && secondFlag != std::string::npos);
  assert(platformio.find(coreFlag, secondFlag + 1) == std::string::npos);
  assert(secondFlag > l0Profile);
}

}  // namespace

int main() {
  testCommandMatrixAndValidation();
  testWebViews();
  testApplyOrderAndFailures();
  testSequentialUpdatesAndResults();
  testL0PersistentSettingsDriveRenderer();
  testFirmwareIntegrationContract();
  return 0;
}
