#include <cassert>
#include "../src/core/system_operation_state.h"

int main() {
  SystemOperationState state;

  // Normal Wi-Fi loss follows the ordinary reconnect/recovery path.
  assert(!state.blocksRequests());
  assert(!state.restartPending());
  assert(wifiRecoveryAllowed(state.restartPending()));

  // Intentional system reboot publishes pending before ESP.restart().
  state.restartRequested();
  assert(state.restartPending());
  assert(state.blocksRequests());
  assert(!wifiRecoveryAllowed(state.restartPending()));

  state.updateFailed();
  assert(!state.restartPending());
  assert(!state.blocksRequests());
  assert(wifiRecoveryAllowed(state.restartPending()));

  // An unmounted SPIFFS is distinct from a pending restart.
  state.filesystemUnmounting();
  assert(state.filesystemUnavailable());
  assert(state.blocksRequests());
  assert(!state.restartPending());
  assert(wifiRecoveryAllowed(state.restartPending()));

  state.updateFailed();
  assert(!state.blocksRequests());

  // A successful Web Update uses the same restart-pending state.
  state.filesystemUnmounting();
  state.restartRequested();
  assert(state.restartPending());
  assert(state.blocksRequests());
  assert(!wifiRecoveryAllowed(state.restartPending()));
}
