#include "../src/core/radio_reconnect_policy.h"

#include <cassert>
#include <cstring>
#include <stdint.h>

using voxone::radio::ReconnectPolicy;
using voxone::radio::SegmentTransportResult;
using voxone::radio::SegmentTransportWatchdog;

int main() {
  SegmentTransportWatchdog hls;

  // Content-Length is authoritative for both Connection: close and
  // keep-alive. Completion wins over socket state only after every body byte
  // has been consumed by the production tracker.
  hls.begin(true, 376, false, 100);
  assert(hls.readLimit(500) == 376);
  assert(hls.received(188, 200));
  assert(hls.evaluate(true, false, 188, false, 0, 200) ==
         SegmentTransportResult::Waiting);
  assert(hls.received(188, 300));
  assert(hls.evaluate(true, false, 0, false, 0, 300) ==
         SegmentTransportResult::SegmentComplete);
  assert(hls.evaluate(true, true, 0, false, 0, 300) ==
         SegmentTransportResult::SegmentComplete);
  assert(hls.bodyBytes() == 376);

  // Waiting for the live playlist to publish another segment is intentional;
  // it keeps draining audio but does not run the segment transport watchdog.
  assert(hls.evaluate(false, false, 0, false, 0, 20000) ==
         SegmentTransportResult::Waiting);

  // WiFiClient can report disconnected while retaining unread bytes. This is
  // not truncation until available() is drained.
  hls.begin(true, 376, false, 600);
  assert(hls.received(100, 700));
  assert(hls.evaluate(true, false, 276, false, 100, 700) ==
         SegmentTransportResult::Waiting);
  assert(hls.readLimit(400) == 276);
  assert(hls.received(276, 800));
  assert(hls.evaluate(true, false, 0, false, 0, 800) ==
         SegmentTransportResult::SegmentComplete);

  // A Content-Length body that ends in a partial 188-byte TS unit is invalid,
  // even when the declared byte count was received exactly.
  hls.begin(true, 200, false, 900);
  assert(hls.received(200, 1000));
  assert(hls.evaluate(true, false, 0, false, 12, 1000) ==
         SegmentTransportResult::RecoverStation);

  // A short response and a connected-but-stalled response are bounded errors.
  hls.begin(true, 376, false, 1100);
  assert(hls.received(188, 1200));
  assert(hls.evaluate(true, false, 0, false, 0, 1200) ==
         SegmentTransportResult::RecoverStation);
  hls.begin(true, 376, false, 2000);
  assert(hls.evaluate(true, true, 0, true, 0, 11999) ==
         SegmentTransportResult::Waiting);
  assert(hls.evaluate(true, true, 0, true, 0, 12000) ==
         SegmentTransportResult::RecoverStation);

  // Chunked transfer has its own terminator and never uses Content-Length.
  hls.begin(true, 1, true, 13000);
  assert(hls.received(376, 13100));
  assert(hls.evaluate(true, true, 0, false, 0, 13100) ==
         SegmentTransportResult::Waiting);
  hls.chunkComplete();
  assert(hls.evaluate(true, true, 0, false, 0, 13100) ==
         SegmentTransportResult::SegmentComplete);
  hls.begin(false, 0, true, 13200);
  hls.chunkError();
  assert(hls.evaluate(true, true, 0, false, 0, 13200) ==
         SegmentTransportResult::RecoverStation);

  // Close-delimited responses complete only at a clean unit boundary.
  hls.begin(false, 0, false, 14000);
  assert(hls.received(188, 14100));
  assert(hls.evaluate(true, false, 0, false, 0, 14100) ==
         SegmentTransportResult::SegmentComplete);

  // Twelve qualified segment sizes transition locally without station-level
  // recovery. Playlist request sequencing is covered by the HIL rerun.
  const uint32_t fixtureSizes[] = {
      97760, 97384, 97760, 97384, 97384, 97760,
      97384, 97760, 97384, 97384, 97760, 97384};
  unsigned segmentTransitions = 0;
  for (uint32_t size : fixtureSizes) {
    assert(size % 188 == 0);
    hls.begin(true, size, false, 15000 + segmentTransitions);
    uint32_t remaining = size;
    while (remaining) {
      const uint32_t available = remaining > 733 ? 733 : remaining;
      const uint32_t requested = hls.readLimit(available);
      assert(requested != 0 && requested <= remaining);
      assert(hls.received(requested, 15000 + segmentTransitions));
      remaining -= requested;
    }
    assert(hls.evaluate(true, false, 0, true, 0,
                        15000 + segmentTransitions) ==
           SegmentTransportResult::SegmentComplete);
    ++segmentTransitions;
  }
  assert(segmentTransitions == 12);

  // Raw AAC HLS and MPEG-TS share the same bounded, wrap-safe stall rule.
  SegmentTransportWatchdog ts;
  ts.begin(true, 188, false, UINT32_MAX - 5000U);
  assert(ts.evaluate(true, true, 0, false, 0, 4998U) ==
         SegmentTransportResult::Waiting);
  assert(ts.evaluate(true, true, 0, false, 0, 4999U) ==
         SegmentTransportResult::RecoverStation);

  ReconnectPolicy retry;
  assert(retry.schedule(3002));  // one controlled reconnect for real failure

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

  // Reconnect deadline arithmetic survives millis() wrap.
  retry.reset();
  assert(retry.schedule(UINT32_MAX - 500U));
  assert(!retry.takeDue(400U, true, true));
  assert(retry.takeDue(499U, true, true));
}
