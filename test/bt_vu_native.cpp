#include <cassert>
#include <cstdint>

#include "../src/core/bt_vu_state.h"
#include "../src/core/source_vu_state.h"

int main() {
  assert(btVuScalePeak(0, 100) == 0);
  assert(btVuScalePeak(32768, 100) == 100);
  assert(btVuScalePeak(80, 100) == 0);     // below -50 dBFS
  assert(btVuScalePeak(200, 100) <= 8);   // about -44 dBFS
  assert(btVuScalePeak(2068, 100) >= 30 &&
         btVuScalePeak(2068, 100) <= 40); // about -24 dBFS
  assert(btVuScalePeak(8231, 100) >= 60 &&
         btVuScalePeak(8231, 100) <= 70); // about -12 dBFS
  assert(btVuScalePeak(16423, 100) >= 80 &&
         btVuScalePeak(16423, 100) <= 90); // about -6 dBFS
  assert(btVuScalePeak(16384, 100) == 85);
  assert(btVuScalePeak(65535, 100) == 100);
  // UART peaks are measured before the phone module's volume stage. The same
  // peaks therefore give the same meter reading at every user Volume.
  const uint8_t volumes[] = {0, 20, 50, 100};
  for (uint8_t volume : volumes) {
    (void)volume;
    const SourceVuResult result = sourceVuSelect(
        true, false, 0, true, 12000, 3000, 1000, 1000, 100);
    assert(result.playing);
    assert(result.levels == static_cast<uint16_t>(
        (btVuScalePeak(12000, 100) << 8) | btVuScalePeak(3000, 100)));
  }
  uint8_t previousLevel = 0;
  for (uint32_t peak = 0; peak <= 32768; ++peak) {
    const uint8_t level = btVuScalePeak(static_cast<uint16_t>(peak), 100);
    assert(level >= previousLevel);
    previousLevel = level;
  }
  assert(btVuDataFresh(1000, 850));
  assert(!btVuDataFresh(1001, 850));
  assert(btVuDataFresh(25, UINT32_MAX - 100));  // millis() wrap

  constexpr uint16_t dimension = 100;
  {
    // Radio's existing value is an inverted blanking level. Normalize it to
    // an amplitude level for the shared widget input.
    const SourceVuResult result = sourceVuSelect(
        false, true, static_cast<uint16_t>((80 << 8) | 20), true,
        32768, 32768, 1000, 1000, dimension);
    assert(result.playing);
    assert((result.levels >> 8) == 20);
    assert((result.levels & 0xff) == 80);
  }
  {
    // Selected BT exclusively supplies the bars even if background radio has
    // nonzero VU. L/R are scaled independently.
    const SourceVuResult result = sourceVuSelect(
        true, true, static_cast<uint16_t>((95 << 8) | 70), true,
        16384, 8192, 2000, 2100, dimension);
    assert(result.playing);
    assert((result.levels >> 8) == 85);
    assert((result.levels & 0xff) == 65);
  }
  {
    const SourceVuResult noFreshData = sourceVuSelect(
        true, true, 0xffff, true, 32768, 32768, 1000, 1151, dimension);
    assert(noFreshData.levels == 0);
    assert(noFreshData.playing);
    const SourceVuResult paused = sourceVuSelect(
        true, true, 0xffff, false, 32768, 32768, 1100, 1101, dimension);
    assert(paused.levels == 0);
    assert(!paused.playing);
  }
  {
    // A source switch back to RADIO selects its current VU immediately.
    const SourceVuResult result = sourceVuSelect(
        false, true, static_cast<uint16_t>((35 << 8) | 65), false,
        0, 0, 0, 2000, dimension);
    assert(result.playing);
    assert((result.levels >> 8) == 65);
    assert((result.levels & 0xff) == 35);
  }
}
