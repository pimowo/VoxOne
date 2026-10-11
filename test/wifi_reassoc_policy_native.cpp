#include "../src/core/wifi_reassoc_policy.h"
#include "../src/core/radio_reconnect_policy.h"
#include "../src/core/radio_source_policy.h"

#include <cassert>
#include <stdint.h>

using voxone::wifi::ReassocPolicy;
using RadioReconnectPolicy = voxone::radio::ReconnectPolicy;

int main() {
  ReassocPolicy wifi;

  // A. A disconnect arms one delayed application-owned request.
  assert(wifi.arm(100));
  assert(wifi.pending());
  assert(wifi.dueAtMs() == 1100);

  // B. Driver status is deliberately not an input: WL_IDLE_STATUS with no
  // validated readiness still reaches the timer and requests reassociation.
  constexpr int wlIdleStatus = 0;
  assert(wlIdleStatus == 0);
  assert(!wifi.takeDue(1099, true, false));
  assert(wifi.takeDue(1100, true, false));
  assert(wifi.attempts() == 1);

  // C. Without GOT_IP the bounded sequence continues at 2/5/10/15/30/30 s.
  constexpr uint32_t retryDelays[] = {2000, 5000, 10000, 15000, 30000, 30000};
  uint32_t now = 1100;
  for (uint32_t delay : retryDelays) {
    assert(wifi.dueAtMs() == now + delay);
    now += delay;
    assert(wifi.takeDue(now, true, false));
  }
  assert(wifi.attempts() == 6);

  // D/E. Only validated GOT_IP/networkReady resets recovery; once ready,
  // neither a pending timer nor later loop passes issue another request.
  wifi.connected();
  assert(!wifi.pending() && wifi.attempts() == 0);
  assert(!wifi.takeDue(now + 60000, true, true));
  assert(wifi.arm(now));
  assert(!wifi.takeDue(now + 1000, true, true));
  assert(!wifi.pending());

  // F. Duplicate disconnect callbacks neither move the deadline nor create
  // parallel requests.
  assert(wifi.arm(200000));
  const uint32_t firstDue = wifi.dueAtMs();
  for (uint32_t i = 0; i < 100; ++i)
    assert(!wifi.arm(200001 + i));
  assert(wifi.dueAtMs() == firstDue);
  assert(wifi.takeDue(firstDue, true, false));
  assert(!wifi.takeDue(firstDue, true, false));

  // G. RADIO STOP is not an input to Wi-Fi recovery.
  const bool radioPlayIntent = false;
  RadioReconnectPolicy radio;
  assert(radio.schedule(200000));
  assert(!radio.takeDue(201000, radioPlayIntent, false));
  assert(!radioPlayIntent);
  assert(wifi.pending());
  assert(wifi.takeDue(wifi.dueAtMs(), true, false));

  // H. BT selection also leaves Wi-Fi recovery active, while the existing
  // RADIO source policy refuses automatic RADIO takeover.
  const bool radioSelected = false;
  assert(radio.schedule(210000));
  assert(!radio.takeDue(211000, radioSelected, false));
  assert(wifi.takeDue(wifi.dueAtMs(), true, false));
  assert(!radioWifiReconnectShouldPlay(true, radioSelected, true));

  // I. RADIO recovery remains gated on networkReady.
  assert(!radioWifiReconnectShouldPlay(true, true, false));
  const bool networkReady = false;
  assert(!networkReady);
  assert(radio.schedule(220000));
  assert(!radio.takeDue(221000, true, networkReady));
  assert(radio.pending());

  // J. A ten-minute outage cannot create a request storm: after saturation,
  // no two calls are accepted within the 30 s bound.
  wifi.reset();
  assert(wifi.arm(0));
  uint32_t calls = 0;
  for (uint32_t t = 0; t <= 600000; t += 100) {
    if (wifi.takeDue(t, true, false)) ++calls;
  }
  assert(calls <= 24);
  assert(wifi.attempts() == 6);

  // K. AP return first establishes validated readiness and resets Wi-Fi;
  // only the subsequent RADIO-level decision may resume a PLAY intent.
  wifi.connected();
  assert(!wifi.pending());
  assert(radioWifiReconnectShouldPlay(true, true, true));
  assert(radio.takeDue(221000, true, true));

  // STA disabled (AP/recovery mode) never consumes a pending request.
  assert(wifi.arm(UINT32_MAX - 500));
  assert(!wifi.takeDue(499, false, false));
  assert(wifi.takeDue(499, true, false));
}
