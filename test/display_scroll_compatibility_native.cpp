#include "../src/core/player_scroll_runtime.h"
#include "../src/displays/widgets/scroll_text_state.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <string>

using namespace voxone::display_scroll;

namespace {

std::size_t allocationCount = 0;

struct WidgetProbe {
  char text[96]{};
  char drawnText[96]{};
  char window[128]{};
  uint16_t viewportPixels = 4;
  uint16_t charWidth = 1;
  uint16_t textPixels = 0;
  uint16_t cyclePixels = 0;
  uint32_t contentRevision = 0;
  mutable uint64_t metricReads = 0;
  uint64_t controlChanges = 0;
  uint64_t renderRequests = 0;
  uint64_t physicalDraws = 0;
  uint64_t rejectedOffsets = 0;
  int16_t origin = 0;
  int16_t coordinate = 0;
  uint16_t lastOffset = 0;
  bool present = true;
  bool coreControlled = false;
  bool active = true;
  bool locked = false;

  void updateGeometry(uint16_t viewport, uint16_t glyphWidth) {
    viewportPixels = viewport;
    charWidth = glyphWidth;
    recalculate();
  }

  bool setText(const char* value) {
    int16_t assignedOffset = coordinate;
    const bool changed = scrollTextAssign(text, sizeof(text), value,
                                          assignedOffset, origin);
    if (!changed) return false;
    if (!coreControlled) coordinate = assignedOffset;
    ++contentRevision;
    if (contentRevision == 0) ++contentRevision;
    recalculate();
    return true;
  }

  PlayerScrollRowMetrics metrics() const {
    ++metricReads;
    return PlayerScrollRowMetrics{
        static_cast<uint8_t>(present), viewportPixels, textPixels,
        cyclePixels, contentRevision};
  }

  void setCoreControlled(bool enabled) {
    ++controlChanges;
    coreControlled = enabled;
    coordinate = origin;
    lastOffset = 0;
  }

  bool render(uint16_t offsetPixels) {
    assert(coreControlled);
    ++renderRequests;
    if (!active || locked) return false;
    int16_t next = origin;
    const bool needsScroll = textPixels > viewportPixels;
    const bool valid = !needsScroll || scrollAbsoluteCoordinate(
        origin, offsetPixels, cyclePixels, next);
    if (!valid) {
      ++rejectedOffsets;
      next = origin;
    }
    lastOffset = valid && needsScroll ? offsetPixels : 0;
    if (coordinate == next && std::strcmp(drawnText, text) == 0) return valid;
    coordinate = next;
    ++physicalDraws;
    const size_t hiddenGlyphs = charWidth ? lastOffset / charWidth : 0;
    (void)scrollComposeWindow(window, sizeof(window), text, " * ",
                              hiddenGlyphs);
    std::snprintf(drawnText, sizeof(drawnText), "%s", text);
    return valid;
  }

 private:
  void recalculate() {
    textPixels = scrollTextPixelWidth(std::strlen(text), charWidth);
    cyclePixels = scrollCyclePixelWidth(
        textPixels, scrollTextPixelWidth(3, charWidth));
  }
};

PlayerScrollRowMetrics readMetrics(const void* context) {
  return static_cast<const WidgetProbe*>(context)->metrics();
}

void setCoreControlled(void* context, bool enabled) {
  static_cast<WidgetProbe*>(context)->setCoreControlled(enabled);
}

bool renderOffset(void* context, uint16_t offsetPixels) {
  return static_cast<WidgetProbe*>(context)->render(offsetPixels);
}

PlayerScrollWidgetBinding bind(WidgetProbe& widget) {
  return PlayerScrollWidgetBinding{&widget, readMetrics, setCoreControlled,
                                   renderOffset};
}

struct DisplayPathHarness {
  PlayerScrollRuntime runtime;
  WidgetProbe rows[3];
  PlayerScrollWidgetBinding bindings[3]{};
  uint8_t rowCount = 0;
  bool playerVisible = false;

  void configure(ScrollRendererProfile profile, uint8_t count,
                 const ScrollSettings& settings,
                 ScrollSettingsSource source =
                     ScrollSettingsSource::UserSettings,
                 bool featureEnabled = true) {
    assert(count <= 3);
    runtime.configure(featureEnabled, profile, settings, source);
    rowCount = count;
    for (uint8_t row = 0; row < count; ++row) bindings[row] = bind(rows[row]);
    assert(runtime.setRows(bindings, count));
  }

  void enter(uint32_t nowMs) {
    playerVisible = true;
    runtime.enter(nowMs);
  }

  void leave() {
    runtime.leave();
    playerVisible = false;
  }

  PlayerScrollRuntimeTick tick(uint32_t nowMs) {
    const uint64_t requestsBefore = totalRenderRequests();
    const uint64_t drawsBefore = totalPhysicalDraws();
    const PlayerScrollRuntimeTick result = runtime.tick(nowMs);
    assert(totalRenderRequests() - requestsBefore <= 1);
    assert(totalPhysicalDraws() - drawsBefore <= 1);
    return result;
  }

  uint64_t totalMetricReads() const {
    return rows[0].metricReads + rows[1].metricReads + rows[2].metricReads;
  }

  uint64_t totalRenderRequests() const {
    return rows[0].renderRequests + rows[1].renderRequests +
           rows[2].renderRequests;
  }

  uint64_t totalPhysicalDraws() const {
    return rows[0].physicalDraws + rows[1].physicalDraws +
           rows[2].physicalDraws;
  }
};

const char* profileName(ScrollRendererProfile profile) {
  switch (profile) {
    case ScrollRendererProfile::Oled128x64: return "OLED_128X64";
    case ScrollRendererProfile::St7789_284x76: return "ST7789_284X76";
    case ScrollRendererProfile::St7796_480x320: return "ST7796_480X320";
    default: return "NONE";
  }
}

const char* phaseName(ScrollEnginePhase phase) {
  switch (phase) {
    case ScrollEnginePhase::Waiting: return "Waiting";
    case ScrollEnginePhase::Moving: return "Moving";
    default: return "Idle";
  }
}

void prepareLongRows(DisplayPathHarness& display, uint8_t count) {
  static const char* values[3] = {
      "STATION-LONG", "ARTIST-LONG", "TITLE-LONG"};
  for (uint8_t row = 0; row < count; ++row) {
    display.rows[row].present = true;
    display.rows[row].updateGeometry(4, 1);
    assert(display.rows[row].setText(values[row]));
  }
}

void drainInitialResets(DisplayPathHarness& display, uint32_t firstMs) {
  for (uint8_t row = 0; row < display.rowCount; ++row) {
    const PlayerScrollRuntimeTick tick = display.tick(firstMs + row);
    assert(tick.renderRequested == 1);
    assert(tick.command.row == static_cast<int8_t>(row));
    assert(tick.command.offsetPixels == 0);
  }
}

void testCompleteRoundRobin(ScrollRendererProfile profile, uint8_t count,
                            bool emitTrace) {
  DisplayPathHarness display;
  prepareLongRows(display, count);
  display.configure(profile, count,
                    ScrollSettings{true, ScrollSpeed::Normal, 0});
  display.enter(0);
  assert(display.runtime.active() == (count != 0));
  drainInitialResets(display, 0);

  uint8_t completed[8]{};
  uint8_t completedCount = 0;
  uint16_t previousOffsets[3]{};
  uint32_t nowMs = count;
  for (uint32_t ticks = 0; ticks < 10000 && completedCount < count * 2;
       ++ticks) {
    nowMs += kNominalDisplayLoopIntervalMs;
    const PlayerScrollRuntimeTick result = display.tick(nowMs);
    if (!result.renderRequested) continue;
    const uint8_t row = static_cast<uint8_t>(result.command.row);
    if (result.command.turnFinished) {
      assert(result.command.offsetPixels == 0);
      completed[completedCount++] = row;
      previousOffsets[row] = 0;
    } else if (result.command.offsetPixels != 0) {
      assert(result.command.offsetPixels > previousOffsets[row]);
      assert(result.command.offsetPixels < display.rows[row].cyclePixels);
      previousOffsets[row] = result.command.offsetPixels;
    }
    if (emitTrace) {
      std::printf("TRACE profile=%s t=%u row=%u phase=%s offset=%u "
                  "finished=%u\n",
                  profileName(profile), nowMs, row,
                  phaseName(result.command.phase),
                  result.command.offsetPixels,
                  result.command.turnFinished);
    }
  }
  assert(completedCount == count * 2);
  for (uint8_t turn = 0; turn < completedCount; ++turn)
    assert(completed[turn] == static_cast<uint8_t>(turn % count));
  for (uint8_t row = 0; row < count; ++row) {
    assert(display.rows[row].rejectedOffsets == 0);
    assert(display.runtime.controller().offsetPixels(row) == 0);
  }
}

void testLayoutsAndQualification() {
  const ScrollRendererProfile profiles[3] = {
      ScrollRendererProfile::Oled128x64,
      ScrollRendererProfile::St7789_284x76,
      ScrollRendererProfile::St7796_480x320};
  for (ScrollRendererProfile profile : profiles)
    testCompleteRoundRobin(profile, 3,
                           profile == ScrollRendererProfile::St7789_284x76);
  testCompleteRoundRobin(ScrollRendererProfile::Oled128x64, 1, false);
  testCompleteRoundRobin(ScrollRendererProfile::St7789_284x76, 2, false);

  DisplayPathHarness filtered;
  filtered.rows[0].updateGeometry(4, 1);
  filtered.rows[1].updateGeometry(20, 1);
  filtered.rows[2].updateGeometry(4, 1);
  assert(filtered.rows[0].setText("STATION-LONG"));
  assert(filtered.rows[1].setText("short"));
  assert(!filtered.rows[2].setText(""));
  filtered.configure(ScrollRendererProfile::St7789_284x76, 3,
                     ScrollSettings{true, ScrollSpeed::Normal, 0});
  filtered.enter(0);
  drainInitialResets(filtered, 0);
  uint8_t completions = 0;
  for (uint32_t nowMs = 10; nowMs < 2000 && completions < 3; nowMs += 10) {
    const PlayerScrollRuntimeTick result = filtered.tick(nowMs);
    if (result.renderRequested && result.command.turnFinished) {
      assert(result.command.row == 0);
      ++completions;
    }
    const int8_t active = filtered.runtime.controller().activeRow();
    assert(active == ScrollGroupEngine::kNoRow || active == 0);
  }
  assert(completions == 3);

  DisplayPathHarness none;
  none.configure(ScrollRendererProfile::None, 0,
                 ScrollSettings{true, ScrollSpeed::Fast, 0});
  none.enter(0);
  assert(!none.runtime.active());
  assert(!none.tick(100000).renderRequested);
}

void testSettingsMatrix() {
  const ScrollRendererProfile profiles[3] = {
      ScrollRendererProfile::Oled128x64,
      ScrollRendererProfile::St7789_284x76,
      ScrollRendererProfile::St7796_480x320};
  const ScrollSpeed speeds[3] = {
      ScrollSpeed::Slow, ScrollSpeed::Normal, ScrollSpeed::Fast};
  const uint16_t pauses[6] = {0, 1000, 2000, 3000, 4000, 5000};

  for (ScrollRendererProfile profile : profiles) {
    for (ScrollSpeed speed : speeds) {
      for (uint16_t pause : pauses) {
        DisplayPathHarness display;
        prepareLongRows(display, 1);
        display.configure(profile, 1, ScrollSettings{true, speed, pause});
        display.enter(100);
        drainInitialResets(display, 100);
        const ResolvedScrollParameters& parameters =
            display.runtime.controller().parameters();
        assert(parameters.enabled && parameters.pauseMs == pause);
        assert(parameters.stepPixels == scrollMotion(profile, speed).stepPixels);
        assert(parameters.intervalMs == scrollMotion(profile, speed).intervalMs);
        if (pause != 0) {
          assert(!display.tick(static_cast<uint32_t>(100 + pause - 1))
                      .renderRequested);
          const PlayerScrollRuntimeTick first =
              display.tick(static_cast<uint32_t>(100 + pause));
          assert(first.renderRequested && first.command.offsetPixels > 0);
        } else {
          assert(!display.tick(static_cast<uint32_t>(
                      100 + parameters.intervalMs - 1)).renderRequested);
          const PlayerScrollRuntimeTick first = display.tick(
              static_cast<uint32_t>(100 + parameters.intervalMs));
          assert(first.renderRequested && first.command.offsetPixels > 0);
        }
      }
    }
  }

  assert(rendererScrollDefaults(ScrollRendererProfile::Oled128x64).pauseMs ==
         5000);
  assert(rendererScrollDefaults(ScrollRendererProfile::St7789_284x76)
             .pauseMs == 5000);
  assert(rendererScrollDefaults(ScrollRendererProfile::St7796_480x320)
             .pauseMs == 2500);

  DisplayPathHarness disabled;
  prepareLongRows(disabled, 1);
  disabled.configure(ScrollRendererProfile::St7789_284x76, 1,
                     ScrollSettings{false, ScrollSpeed::Fast, 0});
  disabled.enter(0);
  drainInitialResets(disabled, 0);
  const uint64_t requests = disabled.totalRenderRequests();
  for (uint32_t nowMs = 10; nowMs < 10000; nowMs += 10)
    assert(!disabled.tick(nowMs).renderRequested);
  assert(disabled.totalRenderRequests() == requests);
  assert(disabled.rows[0].coordinate == disabled.rows[0].origin);
  assert(std::strncmp(disabled.rows[0].window, "STATION-LONG", 4) == 0);
}

void advanceToNonZeroOffset(DisplayPathHarness& display, uint32_t& nowMs) {
  for (uint32_t attempts = 0; attempts < 1000; ++attempts) {
    nowMs += 10;
    const PlayerScrollRuntimeTick result = display.tick(nowMs);
    if (result.renderRequested && result.command.offsetPixels != 0) return;
  }
  assert(false && "scroll never advanced");
}

void testLifecycleAndChanges() {
  DisplayPathHarness display;
  prepareLongRows(display, 3);
  display.configure(ScrollRendererProfile::St7789_284x76, 3,
                    ScrollSettings{true, ScrollSpeed::Normal, 0});
  display.enter(0);
  drainInitialResets(display, 0);
  uint32_t nowMs = 3;
  advanceToNonZeroOffset(display, nowMs);
  const int8_t activeRow = display.runtime.controller().activeRow();
  assert(activeRow >= 0);
  WidgetProbe& active = display.rows[static_cast<uint8_t>(activeRow)];
  assert(active.coordinate < active.origin);

  assert(active.setText("CHANGED-METADATA"));
  const PlayerScrollRuntimeTick reset = display.tick(++nowMs);
  assert(reset.renderRequested && reset.command.row == activeRow &&
         reset.command.offsetPixels == 0);
  assert(active.coordinate == active.origin);
  const uint32_t revision = active.contentRevision;
  assert(!active.setText("CHANGED-METADATA"));
  assert(active.contentRevision == revision);
  assert(!display.tick(++nowMs).renderRequested);

  active.updateGeometry(5, 2);
  const PlayerScrollRuntimeTick geometryReset = display.tick(++nowMs);
  assert(geometryReset.renderRequested &&
         geometryReset.command.row == activeRow &&
         geometryReset.command.offsetPixels == 0);

  // WEB -> BT: changed displayed fields reset one row per tick without stale
  // offsets. Identical values intentionally do not manufacture revisions.
  assert(display.rows[0].setText("BT-STATION"));
  assert(display.rows[1].setText("BT-ARTIST"));
  assert(display.rows[2].setText("BT-TITLE"));
  for (uint8_t row = 0; row < 3; ++row) {
    const PlayerScrollRuntimeTick sourceReset = display.tick(++nowMs);
    assert(sourceReset.renderRequested && sourceReset.command.row == row &&
           sourceReset.command.offsetPixels == 0);
    assert(display.rows[row].coordinate == display.rows[row].origin);
  }

  // PLAYER -> VOL/STATIONS/UPDATE -> PLAYER, including shared _meta row 0.
  for (uint8_t screen = 0; screen < 3; ++screen) {
    advanceToNonZeroOffset(display, nowMs);
    display.leave();
    assert(!display.runtime.active());
    for (uint8_t row = 0; row < 3; ++row) {
      assert(!display.rows[row].coreControlled);
      assert(display.rows[row].coordinate == display.rows[row].origin);
    }
    const uint64_t draws = display.totalPhysicalDraws();
    assert(!display.tick(nowMs + 10000).renderRequested);
    assert(display.totalPhysicalDraws() == draws);
    nowMs += 10001;
    display.enter(nowMs);
    assert(display.runtime.active());
    drainInitialResets(display, nowMs);
  }

  // Fast metadata churn remains bounded to one command per tick and does not
  // allocate or retain an old coordinate.
  for (uint8_t update = 0; update < 30; ++update) {
    char value[32];
    std::snprintf(value, sizeof(value), "rapid-%u-long", update);
    assert(display.rows[update % 3].setText(value));
    const PlayerScrollRuntimeTick result = display.tick(++nowMs);
    assert(result.renderRequested <= 1);
    if (result.renderRequested)
      assert(display.rows[static_cast<uint8_t>(result.command.row)].lastOffset ==
             result.command.offsetPixels);
  }
  display.leave();
  for (uint8_t row = 0; row < 3; ++row)
    assert(!display.rows[row].coreControlled);
}

void testClippingAndCycleHelpers() {
  char window[64]{};
  assert(scrollComposeWindow(window, sizeof(window), "ABCDEFG", " * ", 0) ==
         17);
  assert(std::strcmp(window, "ABCDEFG * ABCDEFG") == 0);
  assert(scrollComposeWindow(window, sizeof(window), "ABCDEFG", " * ", 7) ==
         10);
  assert(std::strcmp(window, " * ABCDEFG") == 0);
  assert(scrollComposeWindow(window, sizeof(window), "ABCDEFG", " * ", 10) ==
         7);
  assert(std::strcmp(window, "ABCDEFG") == 0);

  const char localPolish[] = {static_cast<char>(0x91), static_cast<char>(0x8A),
                              static_cast<char>(0x86), static_cast<char>(0x82),
                              '\0'};
  assert(scrollComposeWindow(window, sizeof(window), localPolish, " * ", 2) ==
         9);
  assert(static_cast<unsigned char>(window[0]) == 0x86);

  int16_t coordinate = 0;
  assert(scrollAbsoluteCoordinate(2, 0, 20, coordinate) && coordinate == 2);
  assert(scrollAbsoluteCoordinate(2, 19, 20, coordinate) && coordinate == -17);
  assert(!scrollAbsoluteCoordinate(2, 20, 20, coordinate) && coordinate == 2);
}

void testPerformanceAndNoCatchUp() {
  DisplayPathHarness display;
  prepareLongRows(display, 3);
  display.configure(ScrollRendererProfile::St7789_284x76, 3,
                    ScrollSettings{true, ScrollSpeed::Fast, 0});
  display.enter(0);
  drainInitialResets(display, 0);

  uint32_t nowMs = 3;
  advanceToNonZeroOffset(display, nowMs);
  const int8_t rowBefore = display.runtime.controller().activeRow();
  const uint16_t offsetBefore = display.runtime.controller().offsetPixels(
      static_cast<uint8_t>(rowBefore));
  const uint64_t requestsBeforeGap = display.totalRenderRequests();
  const PlayerScrollRuntimeTick afterGap = display.tick(nowMs + 60000);
  assert(afterGap.renderRequested <= 1);
  assert(display.totalRenderRequests() - requestsBeforeGap <= 1);
  if (afterGap.renderRequested && !afterGap.command.turnFinished) {
    assert(afterGap.command.offsetPixels - offsetBefore <=
           display.runtime.controller().parameters().stepPixels);
  }
  nowMs += 60000;

  constexpr uint32_t kTicks = 250000;
  const std::size_t allocationsBefore = allocationCount;
  const uint64_t metricsBefore = display.totalMetricReads();
  const uint64_t rendersBefore = display.totalRenderRequests();
  const uint64_t drawsBefore = display.totalPhysicalDraws();
  const auto started = std::chrono::steady_clock::now();
  for (uint32_t tick = 0; tick < kTicks; ++tick) {
    nowMs += kNominalDisplayLoopIntervalMs;
    (void)display.tick(nowMs);
  }
  const auto stopped = std::chrono::steady_clock::now();
  assert(allocationCount == allocationsBefore);
  const uint64_t metricReads = display.totalMetricReads() - metricsBefore;
  const uint64_t renderRequests = display.totalRenderRequests() - rendersBefore;
  const uint64_t physicalDraws = display.totalPhysicalDraws() - drawsBefore;
  assert(metricReads == static_cast<uint64_t>(kTicks) * 3U);
  assert(renderRequests <= kTicks);
  assert(physicalDraws <= renderRequests);
  const long long elapsedNs =
      std::chrono::duration_cast<std::chrono::nanoseconds>(stopped - started)
          .count();
  std::printf("PERF profile=ST7789_284X76 ticks=%u elapsed_ns=%lld "
              "ns_per_tick=%.2f metric_reads=%llu render_requests=%llu "
              "physical_draws=%llu allocations=0\n",
              kTicks, elapsedNs,
              static_cast<double>(elapsedNs) / static_cast<double>(kTicks),
              static_cast<unsigned long long>(metricReads),
              static_cast<unsigned long long>(renderRequests),
              static_cast<unsigned long long>(physicalDraws));

  // Source-derived upper bounds per DisplayTask iteration. OLED legacy can
  // independently advance three widgets; both TFT legacy paths and CORE are
  // bounded to one. These are draw-callback bounds, not bus-transfer counts.
  constexpr uint8_t legacyMaximumRowsPerTick[3] = {3, 1, 1};
  for (uint8_t profile = 0; profile < 3; ++profile) {
    assert(legacyMaximumRowsPerTick[profile] >= 1);
    constexpr uint8_t coreMaximumRowsPerTick = 1;
    assert(coreMaximumRowsPerTick <= legacyMaximumRowsPerTick[profile]);
  }
}

std::string readFile(const char* path) {
  std::ifstream stream(path);
  assert(stream.good());
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

size_t countText(const std::string& value, const std::string& needle) {
  size_t count = 0;
  size_t position = 0;
  while ((position = value.find(needle, position)) != std::string::npos) {
    ++count;
    position += needle.size();
  }
  return count;
}

void testProductionIntegrationGuards() {
  const std::string header = readFile("src/core/display.h");
  const std::string display = readFile("src/core/display.cpp");
  const std::string widgets = readFile("src/displays/widgets/widgets.cpp");
  const std::string platformio = readFile("platformio.ini");
  assert(header.find("ScrollWidget *_title2 = nullptr;") != std::string::npos);
  assert(display.find("if (_title2) _title2->setChangeObserver") !=
         std::string::npos);
  assert(display.find("if (_title2) rows[count++]") != std::string::npos);
  assert(countText(display, "_playerScrollRuntime.tick(nowMs)") == 1);
  assert(display.find("if (_playerScrollCoreEnabled())") != std::string::npos);
  assert(display.find("ScrollWidget::nextSt7789ScrollFrame()") !=
         std::string::npos);
  assert(display.find("_st7796ScrollTick()") != std::string::npos);
  assert(widgets.find("if (_coreControlled) return;") != std::string::npos);
  assert(widgets.find("scrollComposeWindow(") != std::string::npos);
  assert(widgets.find("const char* encoded = utf8Rus") != std::string::npos);
  assert(countText(platformio, "-DVOXONE_PLAYER_SCROLL_CORE_ENABLED=1") == 1);
  const std::size_t m0 = platformio.find("[env:m0]");
  const std::size_t m0Security = platformio.find("[env:m0-security-test]");
  const std::size_t flag =
      platformio.find("-DVOXONE_PLAYER_SCROLL_CORE_ENABLED=1");
  assert(m0 != std::string::npos && m0Security != std::string::npos &&
         flag > m0 && flag < m0Security);
}

}  // namespace

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

int main() {
  testLayoutsAndQualification();
  testSettingsMatrix();
  testLifecycleAndChanges();
  testClippingAndCycleHelpers();
  testPerformanceAndNoCatchUp();
  testProductionIntegrationGuards();
  return 0;
}
