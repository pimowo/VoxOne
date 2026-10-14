#include "../src/core/player_scroll_runtime.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <type_traits>

namespace {
std::size_t allocationCount = 0;
}

void* operator new(std::size_t size) {
  ++allocationCount;
  if (void* memory = std::malloc(size)) return memory;
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
  ++allocationCount;
  if (void* memory = std::malloc(size)) return memory;
  throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }

using namespace voxone::display_scroll;

struct FakeWidget {
  PlayerScrollRowMetrics metrics;
  bool coreControlled = false;
  bool acceptRender = true;
  uint32_t controlChanges = 0;
  uint32_t renderCalls = 0;
  uint16_t lastOffset = 0;
};

static PlayerScrollRowMetrics fakeMetrics(const void* context) {
  return static_cast<const FakeWidget*>(context)->metrics;
}

static void fakeSetCoreControlled(void* context, bool enabled) {
  FakeWidget& widget = *static_cast<FakeWidget*>(context);
  widget.coreControlled = enabled;
  ++widget.controlChanges;
  if (!enabled) widget.lastOffset = 0;
}

static bool fakeRenderOffset(void* context, uint16_t offsetPixels) {
  FakeWidget& widget = *static_cast<FakeWidget*>(context);
  assert(widget.coreControlled);
  ++widget.renderCalls;
  widget.lastOffset = offsetPixels;
  return widget.acceptRender;
}

static PlayerScrollWidgetBinding binding(FakeWidget& widget) {
  return PlayerScrollWidgetBinding{&widget, fakeMetrics,
                                   fakeSetCoreControlled, fakeRenderOffset};
}

static FakeWidget scrolling(uint32_t revision) {
  FakeWidget widget;
  widget.metrics = PlayerScrollRowMetrics{1, 1, 2, 2, revision};
  return widget;
}

static void configure(PlayerScrollRuntime& runtime, bool featureEnabled = true,
                      bool scrollEnabled = true, uint16_t pauseMs = 0,
                      ScrollSpeed speed = ScrollSpeed::Normal) {
  runtime.configure(featureEnabled, ScrollRendererProfile::St7789_284x76,
                    ScrollSettings{scrollEnabled, speed, pauseMs},
                    ScrollSettingsSource::UserSettings);
}

static PlayerScrollRuntimeTick expectTick(PlayerScrollRuntime& runtime,
                                          uint32_t nowMs,
                                          bool renderRequested,
                                          int8_t row = -1,
                                          uint16_t offset = 0) {
  const PlayerScrollRuntimeTick result = runtime.tick(nowMs);
  assert(result.renderRequested == static_cast<uint8_t>(renderRequested));
  if (renderRequested) {
    assert(result.command.row == row);
    assert(result.command.offsetPixels == offset);
  }
  return result;
}

static std::string readFile(const char* path) {
  std::ifstream stream(path);
  assert(stream.good());
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

static std::size_t countText(const std::string& value,
                             const std::string& needle) {
  std::size_t count = 0;
  std::size_t position = 0;
  while ((position = value.find(needle, position)) != std::string::npos) {
    ++count;
    position += needle.size();
  }
  return count;
}

int main() {
  static_assert(!playerScrollCoreBuildEnabled(),
                "The production default must keep CORE scrolling disabled");
  static_assert(PlayerScrollRuntime::kMaximumRows == 3,
                "Runtime capacity must remain fixed");
  static_assert(std::is_standard_layout<PlayerScrollRuntime>::value,
                "Runtime must use fixed-layout storage");
  static_assert(sizeof(PlayerScrollRuntime) <= 256,
                "Runtime unexpectedly grew");

  FakeWidget station = scrolling(1);
  FakeWidget artist = scrolling(2);
  FakeWidget title = scrolling(3);
  const PlayerScrollWidgetBinding rows[3] = {
      binding(station), binding(artist), binding(title)};

  // The build gate leaves every widget and the legacy path untouched.
  PlayerScrollRuntime runtime;
  configure(runtime, false);
  assert(runtime.setRows(rows, 3));
  runtime.enter(0);
  assert(!runtime.active());
  assert(!station.coreControlled && !artist.coreControlled &&
         !title.coreControlled);
  expectTick(runtime, 100000, false);

  // Renderer defaults retain the historical pause and motion parameters.
  runtime.configure(true, ScrollRendererProfile::Oled128x64,
                    defaultScrollSettings(),
                    ScrollSettingsSource::RendererDefaults);
  assert(runtime.controller().parameters().pauseMs == 5000);
  assert(runtime.controller().parameters().intervalMs == 35);
  assert(runtime.controller().parameters().stepPixels == 2);
  runtime.configure(true, ScrollRendererProfile::St7789_284x76,
                    defaultScrollSettings(),
                    ScrollSettingsSource::RendererDefaults);
  assert(runtime.controller().parameters().pauseMs == 5000);
  assert(runtime.controller().parameters().intervalMs == 9);
  assert(runtime.controller().parameters().stepPixels == 1);
  runtime.configure(true, ScrollRendererProfile::St7796_480x320,
                    defaultScrollSettings(),
                    ScrollSettingsSource::RendererDefaults);
  assert(runtime.controller().parameters().pauseMs == 2500);
  assert(runtime.controller().parameters().intervalMs == 50);
  assert(runtime.controller().parameters().stepPixels == 2);

  // User-selected speeds resolve exactly through the shared renderer model.
  const ScrollSpeed speeds[3] = {ScrollSpeed::Slow, ScrollSpeed::Normal,
                                  ScrollSpeed::Fast};
  const uint8_t oledSteps[3] = {1, 2, 4};
  const uint8_t st7789Steps[3] = {1, 1, 2};
  const uint16_t st7789Intervals[3] = {18, 9, 9};
  const uint8_t st7796Steps[3] = {1, 2, 4};
  for (uint8_t index = 0; index < 3; ++index) {
    runtime.configure(true, ScrollRendererProfile::Oled128x64,
                      ScrollSettings{true, speeds[index], 0},
                      ScrollSettingsSource::UserSettings);
    assert(runtime.controller().parameters().intervalMs == 35);
    assert(runtime.controller().parameters().stepPixels == oledSteps[index]);
    runtime.configure(true, ScrollRendererProfile::St7789_284x76,
                      ScrollSettings{true, speeds[index], 0},
                      ScrollSettingsSource::UserSettings);
    assert(runtime.controller().parameters().intervalMs ==
           st7789Intervals[index]);
    assert(runtime.controller().parameters().stepPixels == st7789Steps[index]);
    runtime.configure(true, ScrollRendererProfile::St7796_480x320,
                      ScrollSettings{true, speeds[index], 0},
                      ScrollSettingsSource::UserSettings);
    assert(runtime.controller().parameters().intervalMs == 50);
    assert(runtime.controller().parameters().stepPixels == st7796Steps[index]);
  }

  // Layouts with zero through three fields are accepted without allocation.
  for (uint8_t count = 0; count <= 3; ++count) {
    PlayerScrollRuntime layout;
    configure(layout);
    assert(layout.setRows(rows, count));
    layout.enter(0);
    assert(layout.active() == (count != 0));
    assert(layout.rowCount() == count);
    layout.leave();
  }

  // One shared controller schedules Station -> Artist -> Title fairly and
  // emits no more than one renderer callback from any tick.
  configure(runtime);
  assert(runtime.setRows(rows, 3));
  runtime.enter(0);
  assert(runtime.active());
  assert(station.coreControlled && artist.coreControlled &&
         title.coreControlled);
  expectTick(runtime, 0, true, 0, 0);
  expectTick(runtime, 1, true, 1, 0);
  expectTick(runtime, 2, true, 2, 0);
  expectTick(runtime, 9, true, 0, 1);
  expectTick(runtime, 18, true, 0, 0);
  expectTick(runtime, 19, false);
  expectTick(runtime, 28, true, 1, 1);
  expectTick(runtime, 37, true, 1, 0);
  expectTick(runtime, 38, false);
  expectTick(runtime, 47, true, 2, 1);
  expectTick(runtime, 56, true, 2, 0);
  expectTick(runtime, 57, false);
  assert(runtime.controller().activeRow() == 0);

  // Metadata and geometry are read from cached widget metrics. A changed row
  // resets to zero while unrelated rows retain fair scheduling.
  expectTick(runtime, 66, true, 0, 1);
  station.metrics.contentRevision++;
  expectTick(runtime, 67, true, 0, 0);
  expectTick(runtime, 68, false);
  assert(runtime.controller().activeRow() == 1);
  artist.metrics.viewportPixels = artist.metrics.textPixels;
  expectTick(runtime, 69, true, 1, 0);
  expectTick(runtime, 70, false);
  assert(runtime.controller().activeRow() == 2);

  // Waiting is anchored at enter(). A metadata change during the pause resets
  // only that row and starts the following fair turn with a fresh pause.
  runtime.leave();
  station = scrolling(20);
  artist = scrolling(21);
  title = scrolling(22);
  configure(runtime, true, true, 1000);
  assert(runtime.setRows(rows, 3));
  runtime.enter(100);
  expectTick(runtime, 100, true, 0, 0);
  expectTick(runtime, 101, true, 1, 0);
  expectTick(runtime, 102, true, 2, 0);
  assert(runtime.controller().phase() == ScrollEnginePhase::Waiting);
  expectTick(runtime, 1099, false);
  station.metrics.contentRevision++;
  expectTick(runtime, 1100, true, 0, 0);
  expectTick(runtime, 1101, false);
  assert(runtime.controller().activeRow() == 1);
  assert(runtime.controller().phase() == ScrollEnginePhase::Waiting);

  // Empty, short and absent rows never animate. enabled=false retains static
  // core-controlled rendering without starting a legacy scheduler.
  station.metrics = PlayerScrollRowMetrics{1, 10, 0, 0, 10};
  artist.metrics = PlayerScrollRowMetrics{1, 10, 10, 13, 11};
  title.metrics = PlayerScrollRowMetrics{0, 10, 20, 23, 12};
  runtime.leave();
  configure(runtime, true, false);
  assert(runtime.setRows(rows, 3));
  runtime.enter(100);
  expectTick(runtime, 100, true, 0, 0);
  expectTick(runtime, 101, true, 1, 0);
  expectTick(runtime, 100000, false);
  assert(runtime.controller().phase() == ScrollEnginePhase::Idle);

  // leave() restores every shared widget to legacy mode. Re-entry starts from
  // offset zero and a new pause; inactive ticks never render.
  runtime.leave();
  assert(!runtime.active());
  assert(!station.coreControlled && !artist.coreControlled &&
         !title.coreControlled);
  const uint32_t rendersAfterLeave = station.renderCalls + artist.renderCalls +
                                     title.renderCalls;
  expectTick(runtime, 200000, false);
  assert(station.renderCalls + artist.renderCalls + title.renderCalls ==
         rendersAfterLeave);

  // Invalid bindings fail closed without touching widget state.
  PlayerScrollRuntime invalid;
  configure(invalid);
  assert(!invalid.setRows(nullptr, 1));
  assert(!invalid.active() && invalid.rowCount() == 0);
  PlayerScrollWidgetBinding broken = rows[0];
  broken.renderOffset = nullptr;
  assert(!invalid.setRows(&broken, 1));
  invalid.enter(0);
  assert(!invalid.active());

  // Repeated refresh/tick operations have fixed cost and allocate nothing.
  station = scrolling(101);
  artist = scrolling(102);
  title = scrolling(103);
  configure(runtime);
  assert(runtime.setRows(rows, 3));
  runtime.enter(0);
  const std::size_t allocationsBefore = allocationCount;
  uint32_t now = 0;
  for (uint32_t iteration = 0; iteration < 100000; ++iteration) {
    now += 10;
    const uint32_t rendersBefore = station.renderCalls + artist.renderCalls +
                                   title.renderCalls;
    (void)runtime.tick(now);
    const uint32_t rendersAfter = station.renderCalls + artist.renderCalls +
                                  title.renderCalls;
    assert(rendersAfter - rendersBefore <= 1);
  }
  assert(allocationCount == allocationsBefore);

  // Source-level integration guard: Display owns one runtime, calls one tick,
  // binds the three layouts, and keeps every legacy scheduler in the fallback.
  const std::string displayHeader = readFile("src/core/display.h");
  const std::string displaySource = readFile("src/core/display.cpp");
  const std::string runtimeHeader = readFile("src/core/player_scroll_runtime.h");
  assert(displayHeader.find("PlayerScrollRuntime _playerScrollRuntime") !=
         std::string::npos);
  assert(countText(displaySource, "_playerScrollRuntime.tick(nowMs)") == 1);
  assert(displaySource.find("if (_st7789Station) rows[count++]") !=
         std::string::npos);
  assert(displaySource.find("if (_meta) rows[count++]") != std::string::npos);
  assert(displaySource.find("if (_title1) rows[count++]") != std::string::npos);
  assert(displaySource.find("if (_title2) rows[count++]") != std::string::npos);
  assert(displaySource.find("if (_playerScrollCoreEnabled())") !=
         std::string::npos);
  assert(displaySource.find("ScrollWidget::nextSt7789ScrollFrame()") !=
         std::string::npos);
  assert(displaySource.find("_st7796ScrollTick()") != std::string::npos);
  assert(runtimeHeader.find("xTaskCreate") == std::string::npos);
  assert(runtimeHeader.find("millis(") == std::string::npos);
  assert(runtimeHeader.find("malloc(") == std::string::npos);
  assert(runtimeHeader.find("std::vector") == std::string::npos);

  return 0;
}
