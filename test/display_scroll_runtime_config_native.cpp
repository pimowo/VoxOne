#include "../src/core/display_scroll_runtime_config.h"

#include <atomic>
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

using namespace voxone::display_scroll;

namespace {

ScrollSettingsLoadResult fakeResult{
    defaultScrollSettings(), ScrollSettingsSource::RendererDefaults,
    ScrollSettingsLoadStatus::RendererDefaultsMissing,
    ScrollSettingsRecordStatus::NotDecoded};
uint32_t fakeReadCount = 0;

ScrollSettingsLoadResult fakeReader() {
  ++fakeReadCount;
  return fakeResult;
}

ScrollSettingsLoadResult userResult(const ScrollSettings& settings) {
  return ScrollSettingsLoadResult{
      settings, ScrollSettingsSource::UserSettings,
      ScrollSettingsLoadStatus::UserSettings,
      ScrollSettingsRecordStatus::Valid};
}

ScrollSettingsLoadResult fallbackResult(
    ScrollSettingsLoadStatus status,
    ScrollSettingsRecordStatus recordStatus) {
  return ScrollSettingsLoadResult{
      defaultScrollSettings(), ScrollSettingsSource::RendererDefaults,
      status, recordStatus};
}

bool sameSnapshot(const ScrollSettingsRuntimeSnapshot& snapshot,
                  const ScrollSettings& settings,
                  ScrollSettingsSource source) {
  return snapshot.source == source &&
         equalScrollSettings(snapshot.settings, settings);
}

std::string readFile(const char* path) {
  std::ifstream stream(path);
  assert(stream.good());
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

void testInitializationAndFallback() {
  ScrollSettingsRuntimeConfig runtime;
  assert(sameSnapshot(runtime.snapshot(), defaultScrollSettings(),
                      ScrollSettingsSource::RendererDefaults));

  const ScrollSettingsLoadResult fallbackCases[] = {
      fallbackResult(ScrollSettingsLoadStatus::RendererDefaultsMissing,
                     ScrollSettingsRecordStatus::NotDecoded),
      fallbackResult(ScrollSettingsLoadStatus::RendererDefaultsInvalid,
                     ScrollSettingsRecordStatus::InvalidCrc),
      fallbackResult(ScrollSettingsLoadStatus::RendererDefaultsInvalid,
                     ScrollSettingsRecordStatus::InvalidVersion),
      fallbackResult(ScrollSettingsLoadStatus::RendererDefaultsOpenError,
                     ScrollSettingsRecordStatus::NotDecoded),
      fallbackResult(ScrollSettingsLoadStatus::RendererDefaultsReadError,
                     ScrollSettingsRecordStatus::NotDecoded)};

  for (const ScrollSettingsLoadResult& expected : fallbackCases) {
    const ScrollSettings user{false, ScrollSpeed::Fast, 3000};
    assert(runtime.publish(user, ScrollSettingsSource::UserSettings) ==
           ScrollSettingsPublishStatus::Changed);
    fakeResult = expected;
    const uint32_t readsBefore = fakeReadCount;
    const ScrollSettingsLoadResult actual =
        loadScrollSettingsRuntime(runtime, fakeReader);
    assert(fakeReadCount == readsBefore + 1);
    assert(actual.status == expected.status);
    assert(actual.recordStatus == expected.recordStatus);
    assert(sameSnapshot(runtime.snapshot(), defaultScrollSettings(),
                        ScrollSettingsSource::RendererDefaults));
  }

  // A contradictory or invalid "user" result is never published even if a
  // faulty adapter labels it as successful.
  fakeResult = userResult(
      ScrollSettings{true, static_cast<ScrollSpeed>(9), 5000});
  (void)loadScrollSettingsRuntime(runtime, fakeReader);
  assert(runtime.snapshot().source == ScrollSettingsSource::RendererDefaults);

  fakeResult = userResult(ScrollSettings{true, ScrollSpeed::Normal, 5000});
  fakeResult.recordStatus = ScrollSettingsRecordStatus::InvalidCrc;
  (void)loadScrollSettingsRuntime(runtime, fakeReader);
  assert(runtime.snapshot().source == ScrollSettingsSource::RendererDefaults);

  const ScrollSettingsLoadResult nullRead =
      loadScrollSettingsRuntime(runtime, nullptr);
  assert(nullRead.status ==
         ScrollSettingsLoadStatus::RendererDefaultsReadError);
  assert(runtime.snapshot().source == ScrollSettingsSource::RendererDefaults);
}

void testSettingsMatrixAndReinitialization() {
  ScrollSettingsRuntimeConfig runtime;
  const ScrollSpeed speeds[] = {ScrollSpeed::Slow, ScrollSpeed::Normal,
                                ScrollSpeed::Fast};
  const uint16_t pauses[] = {0, 1000, 2000, 3000, 4000, 5000};

  for (uint8_t enabled = 0; enabled <= 1; ++enabled) {
    for (ScrollSpeed speed : speeds) {
      for (uint16_t pause : pauses) {
        const ScrollSettings expected{enabled != 0, speed, pause};
        fakeResult = userResult(expected);
        const uint32_t readsBefore = fakeReadCount;
        (void)loadScrollSettingsRuntime(runtime, fakeReader);
        assert(fakeReadCount == readsBefore + 1);
        assert(sameSnapshot(runtime.snapshot(), expected,
                            ScrollSettingsSource::UserSettings));

        const uint32_t revision = runtime.snapshot().revision;
        (void)loadScrollSettingsRuntime(runtime, fakeReader);
        assert(runtime.snapshot().revision == revision);
      }
    }
  }

  const ScrollSettingsRuntimeSnapshot beforeInvalid = runtime.snapshot();
  assert(runtime.publish(
             ScrollSettings{true, static_cast<ScrollSpeed>(3), 5000},
             ScrollSettingsSource::UserSettings) ==
         ScrollSettingsPublishStatus::Invalid);
  assert(runtime.publish(ScrollSettings{true, ScrollSpeed::Normal, 2500},
                         ScrollSettingsSource::UserSettings) ==
         ScrollSettingsPublishStatus::Invalid);
  assert(runtime.publish(defaultScrollSettings(),
                         static_cast<ScrollSettingsSource>(2)) ==
         ScrollSettingsPublishStatus::Invalid);
  assert(runtime.snapshot().revision == beforeInvalid.revision);
}

void testAtomicPublication() {
  ScrollSettingsRuntimeConfig runtime;
  const ScrollSettings first{false, ScrollSpeed::Slow, 1000};
  const ScrollSettings second{true, ScrollSpeed::Fast, 5000};
  assert(runtime.publish(first, ScrollSettingsSource::UserSettings) ==
         ScrollSettingsPublishStatus::Changed);

  std::atomic<bool> start{false};
  std::atomic<bool> done{false};
  std::atomic<bool> valid{true};
  std::thread writer([&]() {
    while (!start.load(std::memory_order_acquire)) {}
    for (uint32_t iteration = 0; iteration < 100000; ++iteration) {
      const ScrollSettings& next = (iteration & 1u) ? first : second;
      (void)runtime.publish(next, ScrollSettingsSource::UserSettings);
    }
    done.store(true, std::memory_order_release);
  });
  std::thread reader([&]() {
    start.store(true, std::memory_order_release);
    do {
      const ScrollSettingsRuntimeSnapshot current = runtime.snapshot();
      const bool isFirst = sameSnapshot(
          current, first, ScrollSettingsSource::UserSettings);
      const bool isSecond = sameSnapshot(
          current, second, ScrollSettingsSource::UserSettings);
      if (!isFirst && !isSecond) valid.store(false, std::memory_order_release);
    } while (!done.load(std::memory_order_acquire));
  });
  writer.join();
  reader.join();
  assert(valid.load(std::memory_order_acquire));
}

void testProductionBoundaries() {
  const std::string mainSource = readFile("src/main.cpp");
  const std::string displaySource = readFile("src/core/display.cpp");
  const std::string runtimeSource =
      readFile("src/core/display_scroll_runtime_config_nvs.cpp");
  const std::string platformio = readFile("platformio.ini");

  const std::size_t configInit = mainSource.find("config.init();");
  const std::size_t scrollInit = mainSource.find(
      "initializeScrollSettingsRuntimePersistent()");
  const std::size_t displayInit = mainSource.find("display.init();");
  assert(configInit != std::string::npos && scrollInit != std::string::npos &&
         displayInit != std::string::npos);
  assert(configInit < scrollInit && scrollInit < displayInit);
  assert(displaySource.find("scrollSettingsRuntimeConfig().snapshot()") !=
         std::string::npos);
  assert(displaySource.find("readScrollSettingsPersistent") ==
         std::string::npos);
  assert(runtimeSource.find("readScrollSettingsPersistent") !=
         std::string::npos);
  assert(runtimeSource.find("writeScrollSettingsPersistent") ==
         std::string::npos);
  assert(platformio.find("VOXONE_PLAYER_SCROLL_CORE_ENABLED") ==
         std::string::npos);
}

}  // namespace

int main() {
  static_assert(sizeof(ScrollSettingsRuntimeConfig) == sizeof(uint32_t),
                "runtime owner must remain one compact atomic word");
  testInitializationAndFallback();
  testSettingsMatrixAndReinitialization();
  testAtomicPublication();
  testProductionBoundaries();
  return 0;
}
