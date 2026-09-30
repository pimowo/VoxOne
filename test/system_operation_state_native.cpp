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

  // A valid update gates both outputs once and stays silent through restart.
  SystemOperationState update;
  assert(update.updateStarted());
  assert(!update.updateStarted());
  assert(update.audioBlocked());
  assert(!update.releaseFailedUpdateAudio());
  update.radioStopped();
  assert(update.isRadioStopped());
  update.awaitRadioStop();
  update.updateFailed();
  assert(update.audioBlocked());
  assert(!update.updateStarted());
  assert(!update.releaseFailedUpdateAudio());
  update.radioStopped();
  assert(update.releaseFailedUpdateAudio());
  assert(!update.audioBlocked());
  assert(update.updateStarted());
  update.restartRequested();
  assert(update.audioBlocked());
}
