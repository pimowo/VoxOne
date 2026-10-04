#include "../src/core/bt_runtime.h"

#include <cassert>

int main() {
  BtRuntime desk(false);
  assert(!desk.shouldStart());
  assert(desk.configureBeforeStart(true));
  assert(!desk.start());
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
  assert(!disabled.shouldStart());

  BtRuntime enabled(true);
  assert(enabled.start());  // Current BT targets keep their existing default.
  const BtRuntimeStatus offline = enabled.status(false, true);
  assert(offline.supportsBt && offline.btEnabled);
  assert(!offline.btOnline && !offline.btConnected);
  const BtRuntimeStatus online = enabled.status(true, false);
  assert(online.btOnline && !online.btConnected);
  const BtRuntimeStatus connected = enabled.status(true, true);
  assert(connected.btOnline && connected.btConnected);
  assert(!enabled.configureBeforeStart(false));
}
