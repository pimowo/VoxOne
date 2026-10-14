#include "../src/core/display_scroll_engine.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <new>
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

static ScrollRowDescriptor row(bool present, bool needsScroll,
                               uint16_t cyclePixels, uint32_t revision) {
  return ScrollRowDescriptor{static_cast<uint8_t>(present),
                             static_cast<uint8_t>(needsScroll), cyclePixels,
                             revision};
}

static void configure(ScrollGroupEngine& engine, bool enabled,
                      ScrollSpeed speed, uint16_t pauseMs,
                      ScrollRendererProfile profile =
                          ScrollRendererProfile::St7789_284x76) {
  engine.configure(profile, ScrollSettings{enabled, speed, pauseMs},
                   ScrollSettingsSource::UserSettings);
}

static void expectNone(const ScrollEngineEvent& event) {
  assert(event.action == ScrollEngineAction::None);
  assert(event.stepPixels == 0);
  assert(!event.turnFinished);
}

static void expectStart(const ScrollEngineEvent& event, int rowIndex,
                        ScrollEnginePhase phase) {
  assert(event.action == ScrollEngineAction::TurnStarted);
  assert(event.row == rowIndex);
  assert(event.phase == phase);
  assert(event.offsetPixels == 0);
  assert(event.stepPixels == 0);
  assert(!event.turnFinished);
}

static void expectStep(const ScrollEngineEvent& event, int rowIndex,
                       uint8_t stepPixels, uint16_t offsetPixels,
                       bool finished = false) {
  assert(event.action == ScrollEngineAction::Step);
  assert(event.row == rowIndex);
  assert(event.stepPixels == stepPixels);
  assert(event.offsetPixels == offsetPixels);
  assert(event.turnFinished == static_cast<uint8_t>(finished));
  assert(event.phase == (finished ? ScrollEnginePhase::Idle
                                  : ScrollEnginePhase::Moving));
}

int main() {
  static_assert(ScrollGroupEngine::kMaximumRows == 3,
                "Current PLAYER capacity must remain explicitly bounded");
  static_assert(sizeof(ScrollEnginePhase) == sizeof(uint8_t),
                "Phase must have fixed width");
  static_assert(sizeof(ScrollEngineAction) == sizeof(uint8_t),
                "Action must have fixed width");
  static_assert(std::is_trivially_copyable<ScrollRowDescriptor>::value,
                "Row descriptors must not own text or dynamic resources");
  static_assert(std::is_standard_layout<ScrollGroupEngine>::value,
                "Engine must remain fixed-layout state");
  static_assert(sizeof(ScrollRowDescriptor) <= 8,
                "Row descriptor unexpectedly grew");
  static_assert(sizeof(ScrollGroupEngine) <= 64,
                "Engine unexpectedly grew");

  // No rows, null input and safely bounded counts.
  ScrollGroupEngine engine;
  configure(engine, true, ScrollSpeed::Normal, 0);
  assert(engine.setRows(0, 0));
  assert(engine.rowCount() == 0);
  expectNone(engine.tick(0));
  assert(!engine.setRows(0, 1));
  assert(engine.rowCount() == 0);
  const ScrollRowDescriptor fourRows[4] = {
      row(true, true, 4, 1), row(true, true, 4, 2),
      row(true, true, 4, 3), row(true, true, 4, 4)};
  assert(!engine.setRows(fourRows, 4));
  assert(engine.rowCount() == 3);

  // Configurable one/two/three-row layouts and logical slot order.
  for (uint8_t count = 1; count <= 3; ++count) {
    configure(engine, true, ScrollSpeed::Normal, 0);
    assert(engine.setRows(fourRows, count));
    assert(engine.rowCount() == count);
    expectStart(engine.tick(10), 0, ScrollEnginePhase::Moving);
  }

  // Empty, short and zero-cycle rows never consume a pause or a turn.
  const ScrollRowDescriptor noCandidates[3] = {
      row(false, true, 10, 1), row(true, false, 10, 2),
      row(true, true, 0, 3)};
  configure(engine, true, ScrollSpeed::Normal, 5000);
  assert(engine.setRows(noCandidates, 3));
  expectNone(engine.tick(0));
  assert(engine.phase() == ScrollEnginePhase::Idle);
  const ScrollRowDescriptor mixed[3] = {
      row(true, false, 0, 10), row(true, true, 2, 20),
      row(false, false, 0, 30)};
  assert(engine.setRows(mixed, 3));
  expectStart(engine.tick(1), 1, ScrollEnginePhase::Waiting);

  // Fair Station -> Artist -> Title round-robin. Each long row completes its
  // full cycle before the scheduler advances.
  const ScrollRowDescriptor allLong[3] = {
      row(true, true, 2, 100), row(true, true, 2, 200),
      row(true, true, 2, 300)};
  configure(engine, true, ScrollSpeed::Normal, 0);
  assert(engine.setRows(allLong, 3));
  expectStart(engine.tick(0), 0, ScrollEnginePhase::Moving);
  expectStep(engine.tick(9), 0, 1, 1);
  expectStep(engine.tick(18), 0, 1, 0, true);
  expectStart(engine.tick(19), 1, ScrollEnginePhase::Moving);
  expectStep(engine.tick(28), 1, 1, 1);
  expectStep(engine.tick(37), 1, 1, 0, true);
  expectStart(engine.tick(38), 2, ScrollEnginePhase::Moving);
  expectStep(engine.tick(47), 2, 1, 1);
  expectStep(engine.tick(56), 2, 1, 0, true);
  expectStart(engine.tick(57), 0, ScrollEnginePhase::Moving);

  // The scheduler skips non-qualifying rows without delaying the next long
  // row and cycles fairly through the remaining candidates.
  const ScrollRowDescriptor skipArtist[3] = {
      row(true, true, 1, 101), row(true, false, 0, 201),
      row(true, true, 1, 301)};
  configure(engine, true, ScrollSpeed::Normal, 0);
  assert(engine.setRows(skipArtist, 3));
  expectStart(engine.tick(0), 0, ScrollEnginePhase::Moving);
  expectStep(engine.tick(9), 0, 1, 0, true);
  expectStart(engine.tick(10), 2, ScrollEnginePhase::Moving);
  expectStep(engine.tick(19), 2, 1, 0, true);
  expectStart(engine.tick(20), 0, ScrollEnginePhase::Moving);

  // Every accepted pause is applied before the first step. Zero adds no
  // intentional wait, only the normal step cadence.
  const uint16_t pauses[] = {0, 1000, 2000, 3000, 4000, 5000};
  const ScrollRowDescriptor oneLong[1] = {row(true, true, 20, 1)};
  for (uint8_t index = 0; index < sizeof(pauses) / sizeof(pauses[0]); ++index) {
    const uint16_t pause = pauses[index];
    configure(engine, true, ScrollSpeed::Normal, pause);
    assert(engine.setRows(oneLong, 1));
    expectStart(engine.tick(100), 0,
                pause == 0 ? ScrollEnginePhase::Moving
                           : ScrollEnginePhase::Waiting);
    if (pause == 0) {
      expectNone(engine.tick(108));
      expectStep(engine.tick(109), 0, 1, 1);
    } else {
      expectNone(engine.tick(static_cast<uint32_t>(99U + pause)));
      expectStep(engine.tick(static_cast<uint32_t>(100U + pause)), 0, 1, 1);
    }
  }

  // Slow/Normal/Fast retain renderer-specific intervals and pixel steps.
  struct MotionCase {
    ScrollRendererProfile profile;
    ScrollSpeed speed;
    uint8_t step;
    uint16_t interval;
  };
  const MotionCase motionCases[] = {
      {ScrollRendererProfile::St7789_284x76, ScrollSpeed::Slow, 1, 18},
      {ScrollRendererProfile::St7789_284x76, ScrollSpeed::Normal, 1, 9},
      {ScrollRendererProfile::St7789_284x76, ScrollSpeed::Fast, 2, 9},
      {ScrollRendererProfile::Oled128x64, ScrollSpeed::Slow, 1, 35},
      {ScrollRendererProfile::Oled128x64, ScrollSpeed::Normal, 2, 35},
      {ScrollRendererProfile::Oled128x64, ScrollSpeed::Fast, 4, 35},
      {ScrollRendererProfile::St7796_480x320, ScrollSpeed::Slow, 1, 50},
      {ScrollRendererProfile::St7796_480x320, ScrollSpeed::Normal, 2, 50},
      {ScrollRendererProfile::St7796_480x320, ScrollSpeed::Fast, 4, 50}};
  for (uint8_t index = 0;
       index < sizeof(motionCases) / sizeof(motionCases[0]); ++index) {
    const MotionCase& expected = motionCases[index];
    configure(engine, true, expected.speed, 0, expected.profile);
    assert(engine.setRows(oneLong, 1));
    assert(engine.parameters().stepPixels == expected.step);
    assert(engine.parameters().intervalMs == expected.interval);
    expectStart(engine.tick(0), 0, ScrollEnginePhase::Moving);
    expectNone(engine.tick(static_cast<uint32_t>(expected.interval - 1)));
    expectStep(engine.tick(expected.interval), 0, expected.step,
               expected.step);
  }

  // RendererDefaults stays a separate compatibility path. In particular,
  // ST7796 retains its historical 2500 ms pause even though that is not a
  // valid future user-selected pause.
  engine.configure(ScrollRendererProfile::St7796_480x320,
                   ScrollSettings{true, ScrollSpeed::Fast, 0},
                   ScrollSettingsSource::RendererDefaults);
  assert(engine.parameters().enabled);
  assert(engine.parameters().stepPixels == 2);
  assert(engine.parameters().intervalMs == 50);
  assert(engine.parameters().pauseMs == 2500);

  // Disabled and NONE configurations are static: rows remain at offset zero.
  configure(engine, false, ScrollSpeed::Fast, 0);
  assert(engine.setRows(allLong, 3));
  expectNone(engine.tick(100000));
  assert(engine.phase() == ScrollEnginePhase::Idle);
  assert(engine.activeRow() == ScrollGroupEngine::kNoRow);
  assert(engine.offsetPixels(0) == 0);
  configure(engine, true, ScrollSpeed::Fast, 0,
            ScrollRendererProfile::None);
  assert(engine.setRows(allLong, 3));
  expectNone(engine.tick(200000));
  assert(!engine.parameters().supported && !engine.parameters().enabled);

  // A change to the moving row resets only that row and yields fairly to the
  // following candidate. Repeated changes to an inactive row do not disturb
  // the current turn.
  configure(engine, true, ScrollSpeed::Normal, 0);
  assert(engine.setRows(allLong, 3));
  expectStart(engine.tick(0), 0, ScrollEnginePhase::Moving);
  expectStep(engine.tick(9), 0, 1, 1);
  ScrollRowDescriptor changing[3] = {allLong[0], allLong[1], allLong[2]};
  changing[0].contentRevision++;
  assert(engine.setRows(changing, 3));
  assert(engine.offsetPixels(0) == 0);
  assert(engine.activeRow() == ScrollGroupEngine::kNoRow);
  expectStart(engine.tick(10), 1, ScrollEnginePhase::Moving);
  for (uint8_t update = 0; update < 32; ++update) {
    changing[0].contentRevision++;
    assert(engine.setRows(changing, 3));
  }
  assert(engine.activeRow() == 1);
  assert(engine.phase() == ScrollEnginePhase::Moving);
  expectStep(engine.tick(19), 1, 1, 1);
  expectStep(engine.tick(28), 1, 1, 0, true);
  expectStart(engine.tick(29), 2, ScrollEnginePhase::Moving);

  // The same fairness rule applies when active metadata changes during pause.
  configure(engine, true, ScrollSpeed::Normal, 1000);
  assert(engine.setRows(allLong, 3));
  expectStart(engine.tick(100), 0, ScrollEnginePhase::Waiting);
  changing[0] = allLong[0];
  changing[0].contentRevision++;
  assert(engine.setRows(changing, 3));
  assert(engine.phase() == ScrollEnginePhase::Idle);
  expectStart(engine.tick(200), 1, ScrollEnginePhase::Waiting);

  // Adding an inactive row preserves the current turn. Removing the current
  // row resets it and resumes at the first remaining logical slot.
  configure(engine, true, ScrollSpeed::Normal, 0);
  assert(engine.setRows(allLong, 1));
  expectStart(engine.tick(0), 0, ScrollEnginePhase::Moving);
  assert(engine.setRows(allLong, 2));
  assert(engine.activeRow() == 0);
  expectStep(engine.tick(9), 0, 1, 1);
  assert(engine.setRows(&allLong[1], 1));
  assert(engine.activeRow() == ScrollGroupEngine::kNoRow);
  assert(engine.offsetPixels(0) == 0);
  expectStart(engine.tick(10), 0, ScrollEnginePhase::Moving);

  // Unsigned elapsed arithmetic works across millis() wraparound.
  configure(engine, true, ScrollSpeed::Normal, 0);
  assert(engine.setRows(oneLong, 1));
  expectStart(engine.tick(0xfffffffau), 0, ScrollEnginePhase::Moving);
  expectNone(engine.tick(2));  // 8 ms elapsed.
  expectStep(engine.tick(3), 0, 1, 1);  // 9 ms elapsed.
  configure(engine, true, ScrollSpeed::Normal, 1000);
  assert(engine.setRows(oneLong, 1));
  expectStart(engine.tick(0xffffff00u), 0, ScrollEnginePhase::Waiting);
  expectNone(engine.tick(743));  // 999 ms elapsed across wrap.
  expectStep(engine.tick(744), 0, 1, 1);  // 1000 ms elapsed.

  // A long scheduling gap advances exactly one step and never catches up.
  configure(engine, true, ScrollSpeed::Fast, 0);
  const ScrollRowDescriptor veryLong[1] = {row(true, true, 65535, 99)};
  assert(engine.setRows(veryLong, 1));
  expectStart(engine.tick(0), 0, ScrollEnginePhase::Moving);
  expectStep(engine.tick(100000), 0, 2, 2);
  expectStep(engine.tick(200000), 0, 2, 4);

  // Boundary cycle lengths clamp the final step without overflow and return
  // the externally drawn position to the left edge.
  configure(engine, true, ScrollSpeed::Fast, 0,
            ScrollRendererProfile::Oled128x64);
  const ScrollRowDescriptor onePixel[1] = {row(true, true, 1, 7)};
  assert(engine.setRows(onePixel, 1));
  expectStart(engine.tick(0), 0, ScrollEnginePhase::Moving);
  expectStep(engine.tick(35), 0, 1, 0, true);

  // Tick and unchanged descriptor updates allocate nothing. With at most three
  // rows, idle selection has a fixed upper bound of three inspections; moving
  // and waiting ticks access only the active slot.
  configure(engine, true, ScrollSpeed::Fast, 0);
  assert(engine.setRows(allLong, 3));
  const std::size_t allocationsBefore = allocationCount;
  uint32_t now = 0;
  for (uint32_t iteration = 0; iteration < 100000; ++iteration) {
    assert(engine.setRows(allLong, 3));
    now += 10;
    (void)engine.tick(now);
  }
  assert(allocationCount == allocationsBefore);

  return 0;
}
