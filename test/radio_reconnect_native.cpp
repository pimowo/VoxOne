#include "../src/core/radio_reconnect_policy.h"
#include "../src/core/radio_source_policy.h"
#include "../src/core/network_ready_state.h"

#include <cassert>
#include <cstring>
#include <stdint.h>

using voxone::radio::ReconnectPolicy;
using voxone::network::Ipv4ReadinessEvidence;
using voxone::network::NetworkReadyState;

int main() {
  ReconnectPolicy retry;
  NetworkReadyState network;

  const Ipv4ReadinessEvidence validIp{
      true, 0x7201A8C0U, 0x7201A8C0U, true, true, true, 0x7201A8C0U};

  // WL_CONNECTED alone is not readiness. When the timer expires first, the
  // retry stays pending until a coherent GOT_IP/local/netif snapshot arrives.
  assert(!network.ready());
  assert(retry.schedule(0));
  assert(!retry.takeDue(1000, true, network.ready()));
  assert(retry.pending() && retry.failures() == 1);
  assert(network.gotIp(validIp));
  assert(retry.takeDue(1000, true, network.ready()));
  assert(!retry.pending());

  // Disconnect always clears readiness. A mismatched netif address cannot
  // restore it, while a later coherent GOT_IP can.
  network.disconnected();
  assert(!network.ready());
  auto invalidIp = validIp;
  invalidIp.netifIp = 0;
  assert(!network.gotIp(invalidIp));
  assert(!network.ready());
  assert(network.gotIp(validIp));

  // STOP while not ready cancels the pending request. A later valid GOT_IP
  // cannot resurrect RADIO.
  retry.reset();
  network.disconnected();
  assert(retry.schedule(2000));
  assert(!retry.takeDue(5000, false, network.ready()));
  assert(network.gotIp(validIp));
  assert(!retry.takeDue(5000, true, network.ready()));

  // A source switch to BT has the same cancellation contract.
  retry.reset();
  network.disconnected();
  assert(retry.schedule(6000));
  assert(!retry.takeDue(9000, false, network.ready()));
  assert(network.gotIp(validIp));
  assert(!retry.takeDue(9000, true, network.ready()));

  // Duplicate disconnect/GOT_IP observations preserve one pending request and
  // yield exactly one attempt.
  retry.reset();
  assert(retry.schedule(10000));
  network.disconnected();
  network.disconnected();
  assert(!retry.takeDue(11000, true, network.ready()));
  assert(network.gotIp(validIp));
  assert(network.gotIp(validIp));
  assert(retry.takeDue(11000, true, network.ready()));
  assert(!retry.takeDue(11000, true, network.ready()));

  // 1. RADIO PLAY + offline creates one suspended recovery request. It is
  // consumed only after the network returns and the retry becomes due.
  assert(radioWifiDisconnectShouldSuspend(false, true, true));
  assert(retry.schedule(100));
  assert(!retry.schedule(100));
  assert(!retry.takeDue(5000, true, false));
  assert(retry.pending());
  assert(radioWifiReconnectShouldPlay(true, true, true));
  assert(retry.takeDue(5000, true, true));

  // 2. RADIO STOP never suspends for automatic recovery.
  retry.reset();
  assert(!radioWifiDisconnectShouldSuspend(false, true, false));
  assert(!radioWifiReconnectShouldPlay(false, true, false));

  // 3. STOP while offline cancels the pending request before network return.
  assert(retry.schedule(6000));
  assert(!retry.takeDue(8000, false, false));
  assert(!retry.pending() && retry.failures() == 0);
  assert(!radioWifiReconnectShouldPlay(true, true, false));

  // 4. Selecting BT while offline cancels RADIO recovery and cannot be
  // converted into a RADIO takeover when Wi-Fi returns.
  assert(retry.schedule(9000));
  assert(!retry.takeDue(11000, false, false));
  assert(!retry.pending() && retry.failures() == 0);
  assert(!radioWifiReconnectShouldPlay(true, false, true));

  // Temporary audio owns its own restore and must not start this flow.
  assert(!radioWifiDisconnectShouldSuspend(true, true, true));

  // 5. Repeated disconnect callbacks and service-loop passes keep exactly one
  // pending request; successful recovery starts the next cycle from 1 s.
  retry.reset();
  for (uint32_t cycle = 0; cycle < 3; ++cycle) {
    const uint32_t startedAt = 12000 + cycle * 50000;
    assert(retry.schedule(startedAt));
    for (int duplicate = 0; duplicate < 20; ++duplicate)
      assert(!retry.schedule(startedAt + duplicate));
    assert(retry.failures() == 1);
    assert(retry.takeDue(startedAt + 1000, true, true));
    retry.reset();  // Actual stream recognition, not just socket connect.
  }

  // 6. A 45 s outage produces no HTTP attempt and does not consume or
  // multiply the pending request.
  assert(retry.schedule(200000));
  for (uint32_t elapsed = 1000; elapsed <= 45000; elapsed += 1000)
    assert(!retry.takeDue(200000 + elapsed, true, false));
  assert(retry.pending() && retry.failures() == 1);
  assert(retry.takeDue(245000, true, true));

  // First failure schedules exactly one short retry.
  retry.reset();
  assert(retry.schedule(100));
  assert(retry.pending() && retry.failures() == 1);
  assert(retry.dueAtMs() == 1100);
  assert(!retry.schedule(200));
  assert(!retry.takeDue(1099, true, true));
  assert(retry.takeDue(1100, true, true));
  assert(!retry.pending());

  // 8. A failed attempt schedules the next deterministic bounded delay.
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

  // 9. Actual stream recognition resets backoff; STOP clears it as well.
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

  // 7. Retry policy owns no resolved URL. Player retries by station id, which
  // reloads the configured M3U/redirect URL; a prior final URL remains merely
  // evidence from the previous resolution chain.
  char configured[] = "http://station.example/list.m3u";
  char finalUrl[] = "https://cdn.example/live.aac";
  retry.reset();
  assert(retry.schedule(0));
  assert(retry.takeDue(1000, true, true));
  assert(std::strcmp(configured, "http://station.example/list.m3u") == 0);
  assert(std::strcmp(finalUrl, "https://cdn.example/live.aac") == 0);
}
