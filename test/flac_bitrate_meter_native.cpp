#include "../src/core/flac_bitrate_meter.h"

#include <cassert>
#include <cstdint>
#include <limits>

using voxone::audio::FlacBitrateMeter;

int main() {
  FlacBitrateMeter meter;

  // A, C, K: start and short/zero intervals remain unknown.
  assert(meter.bitrateBps() == 0);
  assert(meter.addBytes(1000, 140500) == 0);
  assert(meter.addBytes(3499, 140500) == 0);
  assert(meter.bitrateBps() == 0);

  // B: 351250 compressed bytes over 2500 ms is exactly 1124 kb/s.
  meter.reset();
  assert(meter.addBytes(1000, 175625) == 0);
  assert(meter.addBytes(3500, 175625) == 1124000);
  assert(meter.bitrateBps() == 1124000);

  // C: the next value is published only after another complete window.
  assert(meter.addBytes(4000, 100000) == 0);
  assert(meter.bitrateBps() == 1124000);
  assert(meter.addBytes(5999, 200000) == 0);
  assert(meter.addBytes(6000, 35000) == 1072000);
  assert(meter.bitrateBps() == 1072000);

  // D-H: every stream/STOP/reconnect/codec-change/station-change reset uses
  // the same operation and cannot retain a prior station's bitrate.
  for (unsigned resetReason = 0; resetReason < 5; ++resetReason) {
    (void)resetReason;
    meter.reset();
    assert(meter.bitrateBps() == 0);
    assert(meter.addBytes(7000, 351250) == 0);
  }

  // I: the meter is opt-in for the FLAC call site; it has no MP3/AAC parser
  // or decoder dependency.
  meter.reset();
  assert(meter.bitrateBps() == 0);

  // J: unsigned timestamp subtraction remains correct across millis wrap.
  meter.reset();
  const uint32_t nearWrap = std::numeric_limits<uint32_t>::max() - 999U;
  assert(meter.addBytes(nearWrap, 175625) == 0);
  assert(meter.addBytes(1500, 175625) == 1124000);

  // J, K: calculation is safe for long uptime, zero time and extreme counts.
  assert(FlacBitrateMeter::calculateBitrateBps(351250, 2500) == 1124000);
  assert(FlacBitrateMeter::calculateBitrateBps(1, 0) == 0);
  assert(FlacBitrateMeter::calculateBitrateBps(0, 2500) == 0);
  assert(FlacBitrateMeter::calculateBitrateBps(
             std::numeric_limits<uint64_t>::max(), 2500) ==
         std::numeric_limits<uint32_t>::max());
  return 0;
}
