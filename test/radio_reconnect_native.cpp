#include "../src/core/radio_reconnect_policy.h"

#include <cassert>
#include <cstring>
#include <stdint.h>

using voxone::radio::ReconnectPolicy;

int main() {
  ReconnectPolicy retry;

  // First failure schedules exactly one short retry.
  assert(retry.schedule(100));
  assert(retry.pending() && retry.failures() == 1);
  assert(retry.dueAtMs() == 1100);
  assert(!retry.schedule(200));
  assert(!retry.takeDue(1099, true, true));
  assert(retry.takeDue(1100, true, true));
  assert(!retry.pending());

  // Repeated failures use deterministic bounded backoff.
  constexpr uint32_t delays[] = {2000, 5000, 10000, 15000, 30000, 30000};
  uint32_t now = 1100;
  for (uint32_t delay : delays) {
    assert(retry.schedule(now));
    assert(retry.dueAtMs() == now + delay);
    now += delay;
    assert(retry.takeDue(now, true, true));
  }
  for (int i = 0; i < 1000; ++i) {
    assert(retry.schedule(now));
    now += 30000;
    assert(retry.takeDue(now, true, true));
  }
  assert(retry.failures() == 6);
  assert(ReconnectPolicy::delayForFailure(255) == 30000);

  // Success/reset and STOP both clear pending state and backoff.
  assert(retry.schedule(now));
  retry.reset();
  assert(!retry.pending() && retry.failures() == 0);
  assert(retry.schedule(now));
  retry.cancel();
  assert(!retry.pending() && retry.failures() == 0);

  // Offline never consumes a retry. Recovery with PLAY intent does.
  assert(retry.schedule(1000));
  assert(!retry.takeDue(60000, true, false));
  assert(retry.pending());
  assert(retry.takeDue(60000, true, true));

  // STOP or a source change cancels before recovery and cannot reconnect.
  assert(retry.schedule(61000));
  assert(!retry.takeDue(100000, false, true));
  assert(!retry.pending() && retry.failures() == 0);
  assert(!retry.takeDue(100000, true, true));

  // Millisecond wrap does not trigger early or overflow the retry count.
  retry.reset();
  assert(retry.schedule(UINT32_MAX - 500));
  assert(!retry.takeDue(400, true, true));
  assert(retry.takeDue(499, true, true));

  // A socket close is terminal only after buffered audio drains; an open
  // socket needs the bounded inactivity timeout.
  assert(!voxone::radio::streamIsDead(false, true, 20000, 0));
  assert(voxone::radio::streamIsDead(false, false, 20000, 19999));
  assert(!voxone::radio::streamIsDead(true, false, 9999, 0));
  assert(voxone::radio::streamIsDead(true, false, 10000, 0));

  // Retry policy owns no URL storage and cannot mutate configured/final URLs.
  char configured[] = "http://station.example/list.m3u";
  char finalUrl[] = "https://cdn.example/live.aac";
  retry.reset();
  assert(retry.schedule(0));
  assert(retry.takeDue(1000, true, true));
  assert(std::strcmp(configured, "http://station.example/list.m3u") == 0);
  assert(std::strcmp(finalUrl, "https://cdn.example/live.aac") == 0);
}
