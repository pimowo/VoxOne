#include "../src/core/player_scroll_controller.h"
#include "../src/core/st7796_player_scroll.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

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

enum class ComparableAction : uint8_t { None, Start, Step };

struct ComparableEvent {
  ComparableAction action;
  int8_t row;
  uint16_t offset;
  bool finished;
};

class LegacyHarness {
 public:
  LegacyHarness() : scheduler_{}, needs_{false, false, false}, cycles_{0, 0, 0},
                    offsets_{0, 0, 0} {}

  void configure(const bool needs[3], const uint16_t cycles[3]) {
    for (uint8_t row = 0; row < 3; ++row) {
      needs_[row] = needs[row];
      cycles_[row] = cycles[row];
      offsets_[row] = 0;
    }
    scheduler_.enter();
  }

  void change(uint8_t row, bool needsScroll, uint16_t cyclePixels) {
    needs_[row] = needsScroll;
    cycles_[row] = cyclePixels;
    offsets_[row] = 0;
    scheduler_.textChanged(row);
  }

  ComparableEvent tick(uint32_t nowMs) {
    const St7796PlayerScroll::Event event = scheduler_.tick(nowMs, needs_);
    if (event.action == St7796PlayerScroll::Action::None)
      return {ComparableAction::None, event.row,
              event.row >= 0 ? visualOffset(static_cast<uint8_t>(event.row))
                             : static_cast<uint16_t>(0),
              false};
    if (event.action == St7796PlayerScroll::Action::Start) {
      offsets_[static_cast<uint8_t>(event.row)] = 0;
      return {ComparableAction::Start, event.row, 0, false};
    }

    const uint8_t row = static_cast<uint8_t>(event.row);
    const uint32_t next = static_cast<uint32_t>(offsets_[row]) +
                          St7796PlayerScroll::kStepPixels;
    if (next > cycles_[row]) {
      offsets_[row] = 0;
      scheduler_.cycleFinished();
      return {ComparableAction::Step, event.row, 0, true};
    }
    offsets_[row] = static_cast<uint16_t>(next);
    return {ComparableAction::Step, event.row, visualOffset(row), false};
  }

  int8_t active() const { return scheduler_.active(); }

 private:
  uint16_t visualOffset(uint8_t row) const {
    return offsets_[row] == cycles_[row] ? 0 : offsets_[row];
  }

  St7796PlayerScroll scheduler_;
  bool needs_[3];
  uint16_t cycles_[3];
  uint16_t offsets_[3];
};

class CoreHarness {
 public:
  CoreHarness() : engine_{}, descriptors_{}, revisions_{1, 1, 1} {
    engine_.configure(ScrollRendererProfile::St7796_480x320,
                      defaultScrollSettings(),
                      ScrollSettingsSource::RendererDefaults);
  }

  void configure(const bool needs[3], const uint16_t cycles[3]) {
    engine_.configure(ScrollRendererProfile::St7796_480x320,
                      defaultScrollSettings(),
                      ScrollSettingsSource::RendererDefaults);
    for (uint8_t row = 0; row < 3; ++row) {
      descriptors_[row] = descriptor(needs[row], cycles[row], revisions_[row]);
    }
    assert(engine_.setRows(descriptors_, 3));
  }

  void change(uint8_t row, bool needsScroll, uint16_t cyclePixels) {
    ++revisions_[row];
    if (revisions_[row] == 0) revisions_[row] = 1;
    descriptors_[row] = descriptor(needsScroll, cyclePixels, revisions_[row]);
    assert(engine_.setRows(descriptors_, 3));
    engine_.restartAtRow(row);
  }

  ComparableEvent tick(uint32_t nowMs) {
    const ScrollEngineEvent event = engine_.tick(nowMs);
    const ComparableAction action =
        event.action == ScrollEngineAction::TurnStarted
            ? ComparableAction::Start
        : event.action == ScrollEngineAction::Step
            ? ComparableAction::Step
            : ComparableAction::None;
    return {action, event.row, event.offsetPixels, event.turnFinished != 0};
  }

  int8_t active() const { return engine_.activeRow(); }
  const ResolvedScrollParameters& parameters() const {
    return engine_.parameters();
  }

 private:
  static ScrollRowDescriptor descriptor(bool needsScroll, uint16_t cyclePixels,
                                        uint32_t revision) {
    return ScrollRowDescriptor{1, static_cast<uint8_t>(needsScroll),
                               needsScroll ? cyclePixels
                                           : static_cast<uint16_t>(0),
                               revision};
  }

  ScrollGroupEngine engine_;
  ScrollRowDescriptor descriptors_[3];
  uint32_t revisions_[3];
};

static void expectSame(LegacyHarness& legacy, CoreHarness& core,
                       uint32_t nowMs) {
  const ComparableEvent expected = legacy.tick(nowMs);
  const ComparableEvent actual = core.tick(nowMs);
  assert(actual.action == expected.action);
  assert(actual.row == expected.row);
  assert(actual.offset == expected.offset);
  assert(actual.finished == expected.finished);
  assert(core.active() == legacy.active());
}

static void configureBoth(LegacyHarness& legacy, CoreHarness& core,
                          const bool needs[3], const uint16_t cycles[3]) {
  legacy.configure(needs, cycles);
  core.configure(needs, cycles);
}

static void changeBoth(LegacyHarness& legacy, CoreHarness& core, uint8_t row,
                       bool needsScroll, uint16_t cyclePixels) {
  legacy.change(row, needsScroll, cyclePixels);
  core.change(row, needsScroll, cyclePixels);
}

int main() {
  LegacyHarness legacy;
  CoreHarness core;
  const bool allLong[3] = {true, true, true};
  const uint16_t mixedCycles[3] = {4, 5, 6};

  assert(core.parameters().pauseMs == St7796PlayerScroll::kStartDelayMs);
  assert(core.parameters().intervalMs ==
         St7796PlayerScroll::kStepIntervalMs);
  assert(core.parameters().stepPixels == St7796PlayerScroll::kStepPixels);

  // Regular cadence: at 2500 ms both schedulers only enter motion. The first
  // two-pixel step is at 2550 ms. Even cycles hold the origin-equivalent
  // boundary for one cadence; odd cycles finish on the overshooting step.
  configureBoth(legacy, core, allLong, mixedCycles);
  const uint32_t regular[] = {0, 2499, 2500, 2549, 2550, 2599, 2600,
                              2649, 2650, 2660, 5160, 5210, 5260, 5270};
  for (uint32_t nowMs : regular) expectSame(legacy, core, nowMs);

  // Short/empty fields are skipped. This covers no artist and no title while
  // retaining station-only looping, then Station -> Title with artist absent.
  const bool stationOnly[3] = {true, false, false};
  const uint16_t stationOnlyCycles[3] = {5, 0, 0};
  configureBoth(legacy, core, stationOnly, stationOnlyCycles);
  const uint32_t stationTimes[] = {100, 2600, 2650, 2700, 2750, 2760};
  for (uint32_t nowMs : stationTimes) expectSame(legacy, core, nowMs);
  changeBoth(legacy, core, 2, true, 6);
  expectSame(legacy, core, 3000);
  assert(core.active() == 2);

  // A station change during movement restarts Station. Consecutive artist and
  // title metadata changes select the last changed field, exactly as the
  // legacy observer sequence does for RADIO and BT source updates.
  configureBoth(legacy, core, allLong, mixedCycles);
  expectSame(legacy, core, 10000);
  expectSame(legacy, core, 12500);
  expectSame(legacy, core, 12550);
  changeBoth(legacy, core, 0, true, 8);
  expectSame(legacy, core, 12560);
  assert(core.active() == 0);
  changeBoth(legacy, core, 1, true, 10);
  changeBoth(legacy, core, 2, true, 12);
  expectSame(legacy, core, 12600);
  assert(core.active() == 2);

  // STOP/PLAY and reinitialization create a fresh Station-first cycle.
  configureBoth(legacy, core, allLong, mixedCycles);
  expectSame(legacy, core, 20000);
  assert(core.active() == 0);

  // Very long text and irregular DisplayTask cadence advance one step only;
  // neither scheduler catches up frames after a long delay.
  const uint16_t longCycles[3] = {65535, 0, 0};
  configureBoth(legacy, core, stationOnly, longCycles);
  expectSame(legacy, core, 30000);
  expectSame(legacy, core, 33000);  // End pause, no pixel movement yet.
  expectSame(legacy, core, 50000);  // Exactly one two-pixel step.
  expectSame(legacy, core, 90000);  // Exactly one further step.

  // Unsigned millis arithmetic remains identical through wraparound.
  configureBoth(legacy, core, stationOnly, stationOnlyCycles);
  expectSame(legacy, core, 0xffffff00u);
  expectSame(legacy, core, 2243u);  // 2499 ms.
  expectSame(legacy, core, 2244u);  // 2500 ms, transition only.
  expectSame(legacy, core, 2293u);  // 49 ms into moving.
  expectSame(legacy, core, 2294u);  // First step.

  // Controller integration automatically applies last-changed-row priority
  // while preserving renderer-owned reset draws and the full pause/cadence.
  PlayerScrollController controller;
  assert(controller.configure(ScrollRendererProfile::St7796_480x320,
                              defaultScrollSettings(),
                              ScrollSettingsSource::RendererDefaults));
  PlayerScrollRowMetrics rows[3] = {
      {1, 100, 200, 220, 1}, {1, 100, 200, 220, 1},
      {1, 100, 200, 220, 1}};
  assert(controller.setRows(rows, 3));
  controller.enter(0);
  (void)controller.tick(0);
  (void)controller.tick(1);
  (void)controller.tick(2);
  assert(controller.activeRow() == 0);
  assert(controller.tick(2500).action == PlayerScrollCommandAction::None);
  const PlayerScrollCommand firstStep = controller.tick(2550);
  assert(firstStep.action == PlayerScrollCommandAction::RenderOffset);
  assert(firstStep.row == 0 && firstStep.offsetPixels == 2);
  ++rows[1].contentRevision;
  ++rows[2].contentRevision;
  assert(controller.setRows(rows, 3));
  const PlayerScrollCommand artistReset = controller.tick(2600);
  assert(artistReset.row == 1 && artistReset.offsetPixels == 0);
  assert(controller.activeRow() == 2);
  const PlayerScrollCommand titleReset = controller.tick(2601);
  assert(titleReset.row == 2 && titleReset.offsetPixels == 0);
  assert(controller.tick(5099).action == PlayerScrollCommandAction::None);
  assert(controller.tick(5100).action == PlayerScrollCommandAction::None);
  const PlayerScrollCommand titleStep = controller.tick(5150);
  assert(titleStep.row == 2 && titleStep.offsetPixels == 2);

  // The parity path is fixed-capacity and performs no dynamic allocation in
  // configuration-stable hot ticks.
  const std::size_t allocationsBefore = allocationCount;
  uint32_t nowMs = 5150;
  for (uint32_t iteration = 0; iteration < 100000; ++iteration) {
    nowMs += 17;
    (void)controller.tick(nowMs);
  }
  assert(allocationCount == allocationsBefore);

  return 0;
}
