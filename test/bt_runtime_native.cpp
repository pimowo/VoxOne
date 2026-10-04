#include "../src/core/bt_runtime.h"

#include <cassert>

int main() {
  BtRuntime desk(false);
  assert(!desk.available());
  assert(desk.configureBeforeStart(true));
  assert(!desk.start());
  assert(!desk.physicalStarted() && !desk.available());
  const BtRuntimeStatus noHardware = desk.status(true, true);
  assert(!noHardware.supportsBt && noHardware.btEnabled);
  assert(!noHardware.btOnline && !noHardware.btConnected);

  BtRuntime disabled(true);
  assert(disabled.configureBeforeStart(false));
  assert(!disabled.start());
  const BtRuntimeStatus off = disabled.status(true, true);
  assert(off.supportsBt && !off.btEnabled);
  assert(!off.btOnline && !off.btConnected);
  assert(!disabled.configureBeforeStart(true));
  disabled.setEnabled(true);
  assert(!disabled.available());  // Boot without BT cannot hot-start it.
  disabled.setEnabled(false);
  assert(!disabled.available());

  BtRuntime enabled(true);
  assert(enabled.start());  // Current BT targets keep their existing default.
  assert(enabled.physicalStarted() && enabled.available());
  const BtRuntimeStatus offline = enabled.status(false, true);
  assert(offline.supportsBt && offline.btEnabled);
  assert(!offline.btOnline && !offline.btConnected);
  const BtRuntimeStatus online = enabled.status(true, false);
  assert(online.btOnline && !online.btConnected);
  const BtRuntimeStatus connected = enabled.status(true, true);
  assert(connected.btOnline && connected.btConnected);
  assert(!enabled.configureBeforeStart(false));
  BtLinkState raw{};
  raw.runtimeAvailable = true;
  raw.connected = true;
  assert(enabled.effectiveLinkState(raw).runtimeAvailable);
  enabled.setEnabled(false);
  assert(!enabled.available() && enabled.physicalStarted());
  assert(!enabled.status(true, true).btOnline);
  assert(!enabled.status(true, true).btConnected);
  assert(!enabled.effectiveLinkState(raw).runtimeAvailable);
  assert(!enabled.effectiveLinkState(raw).connected);
  assert(raw.runtimeAvailable && raw.connected);  // Raw protocol state is separate.
  enabled.setEnabled(true);
  assert(enabled.available());
}
