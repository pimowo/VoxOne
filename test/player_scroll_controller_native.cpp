#include "../src/core/player_scroll_controller.h"

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

static PlayerScrollRowMetrics row(bool present, uint16_t viewportPixels,
                                  uint16_t textPixels, uint16_t cyclePixels,
                                  uint32_t revision) {
  return PlayerScrollRowMetrics{static_cast<uint8_t>(present), viewportPixels,
                                textPixels, cyclePixels, revision};
}

static void configure(PlayerScrollController& controller, bool enabled = true,
                      uint16_t pauseMs = 0) {
  controller.configure(ScrollRendererProfile::St7789_284x76,
                       ScrollSettings{enabled, ScrollSpeed::Normal, pauseMs},
                       ScrollSettingsSource::UserSettings);
}

static void expectNone(const PlayerScrollCommand& command) {
  assert(command.action == PlayerScrollCommandAction::None);
}

static void expectRender(const PlayerScrollCommand& command, int rowIndex,
                         uint16_t offsetPixels, bool finished = false) {
  assert(command.action == PlayerScrollCommandAction::RenderOffset);
  assert(command.row == rowIndex);
  assert(command.offsetPixels == offsetPixels);
  assert(command.turnFinished == static_cast<uint8_t>(finished));
}

static uint32_t drainEntryResets(PlayerScrollController& controller,
                                 uint32_t now, uint8_t count) {
  for (uint8_t rowIndex = 0; rowIndex < count; ++rowIndex)
    expectRender(controller.tick(now++), rowIndex, 0);
  return now;
}

int main() {
  static_assert(PlayerScrollController::kMaximumRows == 3,
                "PLAYER row capacity must remain fixed and bounded");
  static_assert(std::is_trivially_copyable<PlayerScrollRowMetrics>::value,
                "Metrics must not own text or dynamic resources");
  static_assert(std::is_standard_layout<PlayerScrollController>::value,
                "Controller must remain fixed-layout state");
  static_assert(sizeof(PlayerScrollRowMetrics) <= 12,
                "Metrics unexpectedly grew");
  static_assert(sizeof(PlayerScrollController) <= 160,
                "Controller unexpectedly grew");

  const PlayerScrollRowMetrics longRows[3] = {
      row(true, 10, 20, 23, 1), row(true, 10, 20, 23, 2),
      row(true, 10, 20, 23, 3)};

  // Inactive lifecycle never emits renderer work, including after row changes.
  PlayerScrollController controller;
  configure(controller);
  assert(controller.setRows(nullptr, 0));
  assert(controller.rowCount() == 0);
  expectNone(controller.tick(0));
  assert(!controller.active());
  assert(controller.phase() == ScrollEnginePhase::Idle);
  assert(controller.setRows(longRows, 3));
  expectNone(controller.tick(1));

  // Layouts can contain zero through three rows; enter resets only rows that
  // are actually present and starts every cycle at the logical first row.
  for (uint8_t count = 0; count <= 3; ++count) {
    PlayerScrollController layout;
    configure(layout);
    assert(layout.setRows(longRows, count));
    layout.enter(100);
    assert(layout.active());
    assert(layout.rowCount() == count);
    uint32_t now = drainEntryResets(layout, 100, count);
    if (count == 0) {
      expectNone(layout.tick(now));
      assert(layout.phase() == ScrollEnginePhase::Idle);
    } else {
      assert(layout.activeRow() == 0);
      assert(layout.phase() == ScrollEnginePhase::Moving);
    }
  }

  // Invisible, empty, zero-width, short and malformed-cycle rows are skipped
  // without consuming a scheduler turn or pause.
  const PlayerScrollRowMetrics mixed[3] = {
      row(false, 10, 30, 33, 1), row(true, 10, 0, 3, 2),
      row(true, 10, 30, 33, 3)};
  configure(controller);
  assert(controller.setRows(mixed, 3));
  controller.enter(0);
  // Only present rows 1 and 2 need a static reset; row 1 is then skipped.
  expectRender(controller.tick(0), 1, 0);
  expectRender(controller.tick(1), 2, 0);
  assert(controller.activeRow() == 2);
  expectRender(controller.tick(9), 2, 1);

  const PlayerScrollRowMetrics nonScrolling[3] = {
      row(true, 10, 10, 13, 1), row(true, 10, 9, 12, 2),
      row(true, 10, 20, 19, 3)};
  configure(controller);
  assert(controller.setRows(nonScrolling, 3));
  controller.enter(0);
  drainEntryResets(controller, 0, 3);
  expectNone(controller.tick(100000));
  assert(controller.phase() == ScrollEnginePhase::Idle);

  // Fair Station -> Artist -> Title order. Exactly one render command can be
  // returned by each call, including the return to offset zero.
  const PlayerScrollRowMetrics shortCycles[3] = {
      row(true, 1, 2, 2, 10), row(true, 1, 2, 2, 20),
      row(true, 1, 2, 2, 30)};
  configure(controller);
  assert(controller.setRows(shortCycles, 3));
  controller.enter(0);
  drainEntryResets(controller, 0, 3);
  expectRender(controller.tick(9), 0, 1);
  expectRender(controller.tick(18), 0, 0, true);
  expectNone(controller.tick(19));
  assert(controller.activeRow() == 1);
  expectRender(controller.tick(28), 1, 1);
  expectRender(controller.tick(37), 1, 0, true);
  expectNone(controller.tick(38));
  assert(controller.activeRow() == 2);
  expectRender(controller.tick(47), 2, 1);
  expectRender(controller.tick(56), 2, 0, true);
  expectNone(controller.tick(57));
  assert(controller.activeRow() == 0);

  // Waiting starts at enter(); pause zero adds no intentional delay beyond
  // the renderer-specific step interval.
  configure(controller, true, 1000);
  assert(controller.setRows(longRows, 1));
  controller.enter(100);
  expectRender(controller.tick(100), 0, 0);
  assert(controller.phase() == ScrollEnginePhase::Waiting);
  expectNone(controller.tick(1099));
  expectRender(controller.tick(1100), 0, 1);
  assert(controller.phase() == ScrollEnginePhase::Moving);

  configure(controller, true, 0);
  assert(controller.setRows(longRows, 1));
  controller.enter(100);
  expectRender(controller.tick(100), 0, 0);
  expectNone(controller.tick(108));
  expectRender(controller.tick(109), 0, 1);

  // Metadata changes reset the changed moving row and yield fairly. Changes
  // during Waiting follow the same rule.
  PlayerScrollRowMetrics changing[3] = {
      shortCycles[0], shortCycles[1], shortCycles[2]};
  configure(controller);
  assert(controller.setRows(changing, 3));
  controller.enter(0);
  drainEntryResets(controller, 0, 3);
  expectRender(controller.tick(9), 0, 1);
  changing[0].contentRevision++;
  assert(controller.setRows(changing, 3));
  expectRender(controller.tick(10), 0, 0);
  expectNone(controller.tick(11));
  assert(controller.activeRow() == 1);

  configure(controller, true, 1000);
  assert(controller.setRows(changing, 3));
  controller.enter(100);
  drainEntryResets(controller, 100, 3);
  assert(controller.phase() == ScrollEnginePhase::Waiting);
  changing[0].contentRevision++;
  assert(controller.setRows(changing, 3));
  expectRender(controller.tick(200), 0, 0);
  expectNone(controller.tick(201));
  assert(controller.activeRow() == 1);
  assert(controller.phase() == ScrollEnginePhase::Waiting);

  // Geometry changes have their own reset semantics even if contentRevision
  // is unchanged. This covers viewport/font changes without text pointers.
  configure(controller);
  assert(controller.setRows(shortCycles, 2));
  controller.enter(0);
  drainEntryResets(controller, 0, 2);
  expectRender(controller.tick(9), 0, 1);
  PlayerScrollRowMetrics geometry[2] = {shortCycles[0], shortCycles[1]};
  geometry[0].viewportPixels = 2;  // The same text now fits.
  assert(controller.setRows(geometry, 2));
  expectRender(controller.tick(10), 0, 0);
  expectNone(controller.tick(11));
  assert(controller.activeRow() == 1);

  // leave() is inert and enter() always creates a fresh cycle and full pause.
  controller.leave();
  assert(!controller.active());
  assert(controller.phase() == ScrollEnginePhase::Idle);
  expectNone(controller.tick(10000));
  assert(controller.offsetPixels(0) == 0);
  assert(controller.setRows(longRows, 1));
  configure(controller, true, 1000);
  controller.enter(20000);
  expectRender(controller.tick(20000), 0, 0);
  expectNone(controller.tick(20999));
  expectRender(controller.tick(21000), 0, 1);

  // Disabled settings still issue one initial static offset per present row,
  // then remain idle forever.
  configure(controller, false, 0);
  assert(controller.setRows(longRows, 2));
  controller.enter(0);
  expectRender(controller.tick(0), 0, 0);
  expectRender(controller.tick(1), 1, 0);
  expectNone(controller.tick(100000));
  assert(controller.phase() == ScrollEnginePhase::Idle);

  // The complete 16-bit cycle is accepted without overflow. Invalid counts
  // and pointers produce a safe empty layout.
  const PlayerScrollRowMetrics boundary[1] = {
      row(true, 1, 65532, 65535, 0xffffffffu)};
  configure(controller);
  assert(controller.setRows(boundary, 1));
  controller.enter(0);
  expectRender(controller.tick(0), 0, 0);
  expectRender(controller.tick(9), 0, 1);
  assert(!controller.setRows(nullptr, 1));
  assert(controller.rowCount() == 0);
  expectNone(controller.tick(10));
  const PlayerScrollRowMetrics fourRows[4] = {
      longRows[0], longRows[1], longRows[2], longRows[0]};
  assert(!controller.setRows(fourRows, 4));
  assert(controller.rowCount() == 0);

  // Repeated metric updates and ticks have a fixed cost and allocate nothing.
  configure(controller);
  assert(controller.setRows(longRows, 3));
  controller.enter(0);
  const std::size_t allocationsBefore = allocationCount;
  uint32_t now = 0;
  for (uint32_t iteration = 0; iteration < 100000; ++iteration) {
    assert(controller.setRows(longRows, 3));
    now += 10;
    (void)controller.tick(now);
  }
  assert(allocationCount == allocationsBefore);

  return 0;
}
