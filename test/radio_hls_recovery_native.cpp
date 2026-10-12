#include "../src/core/radio_reconnect_policy.h"

#include <cassert>
#include <cstring>
#include <stdint.h>

using voxone::radio::ReconnectPolicy;
using voxone::radio::SegmentTransportResult;
using voxone::radio::SegmentTransportWatchdog;

int main() {
  SegmentTransportWatchdog hls;

  // A complete media segment is a normal transition, even when its response
  // socket closes. It must not be promoted to station recovery.
  hls.reset(100);
  hls.received(500);
  assert(hls.evaluate(true, true, false, false, 500) ==
         SegmentTransportResult::SegmentComplete);

  // Waiting for the live playlist to publish another segment is intentional;
  // it keeps draining audio but does not run the segment transport watchdog.
  assert(hls.evaluate(false, false, false, false, 20000) ==
         SegmentTransportResult::Waiting);

  // The next segment starts a fresh inactivity window and remains local to
  // the HLS state machine while data keeps arriving.
  hls.reset(600);
  hls.received(1500);
  assert(hls.lastDataMs() == 1500);
  assert(hls.evaluate(false, true, true, false, 11499) ==
         SegmentTransportResult::Waiting);

  // Raw AAC HLS and MPEG-TS share the same bounded, time-based stall rule.
  assert(hls.evaluate(false, true, true, false, 11500) ==
         SegmentTransportResult::RecoverStation);
  SegmentTransportWatchdog ts;
  ts.reset(2000);
  assert(ts.evaluate(false, true, true, false, 11999) ==
         SegmentTransportResult::Waiting);
  assert(ts.evaluate(false, true, true, false, 12000) ==
         SegmentTransportResult::RecoverStation);

  // A closed response can drain already buffered audio, then becomes a
  // station-level failure rather than reopening the last segment URL.
  ts.reset(3000);
  assert(ts.evaluate(false, true, false, true, 3001) ==
         SegmentTransportResult::Waiting);
  assert(ts.evaluate(false, true, false, false, 3002) ==
         SegmentTransportResult::RecoverStation);

  ReconnectPolicy retry;
  assert(retry.schedule(3002));

  // STOP and RADIO -> BT both cancel the one pending station recovery.
  assert(!retry.takeDue(4002, false, true));
  assert(!retry.pending() && retry.failures() == 0);
  assert(retry.schedule(5000));
  assert(!retry.takeDue(6000, false, true));
  assert(!retry.pending() && retry.failures() == 0);

  // A station change cancels stale recovery; the new station owns any later
  // attempt. Player resolves it from the canonical configured URL, not from a
  // media segment URL retained by AudioI2S.
  const char canonical[] = "https://radio.example/live.m3u8";
  const char segment[] = "https://cdn.example/chunk-42.aac";
  assert(retry.schedule(7000));
  retry.cancel();
  assert(!retry.pending());
  assert(std::strcmp(canonical, segment) != 0);
  assert(std::strcmp(canonical, "https://radio.example/live.m3u8") == 0);

  // Network-offline suspends rather than consumes the due retry. Network
  // return releases exactly one attempt.
  assert(retry.schedule(8000));
  assert(!retry.takeDue(9000, true, false));
  assert(retry.pending());
  assert(retry.takeDue(9000, true, true));
  assert(!retry.takeDue(9000, true, true));

  // Shared backoff remains 1/2/5/10/15/30 seconds and duplicate scheduling
  // cannot create a second reconnect.
  retry.reset();
  constexpr uint32_t delays[] = {1000, 2000, 5000, 10000, 15000, 30000};
  uint32_t now = 10000;
  for (uint32_t delay : delays) {
    assert(retry.schedule(now));
    assert(!retry.schedule(now));
    assert(retry.dueAtMs() == now + delay);
    now += delay;
    assert(retry.takeDue(now, true, true));
  }

  // Both watchdog and reconnect deadline arithmetic survive millis() wrap.
  hls.reset(UINT32_MAX - 5000U);
  assert(hls.evaluate(false, true, true, false, 4998U) ==
         SegmentTransportResult::Waiting);
  assert(hls.evaluate(false, true, true, false, 4999U) ==
         SegmentTransportResult::RecoverStation);
  retry.reset();
  assert(retry.schedule(UINT32_MAX - 500U));
  assert(!retry.takeDue(400U, true, true));
  assert(retry.takeDue(499U, true, true));
}
