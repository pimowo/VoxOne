#include <cassert>
#include "../src/core/web_update_state.h"

int main() {
  WebUpdateState state;

  // Ordinary Wi-Fi loss still follows the normal reconnect path.
  assert(!state.blocksRequests());
  assert(!state.restartPending());

  // A firmware upload leaves SPIFFS and normal requests available until success.
  assert(!state.blocksRequests());
  state.restartScheduled();
  assert(state.restartPending());
  assert(state.blocksRequests());

  state.updateFailed();
  assert(!state.restartPending());
  assert(!state.blocksRequests());

  // An unmounted SPIFFS cannot serve static files during an upload.
  state.filesystemUnmounting();
  assert(state.filesystemUnavailable());
  assert(state.blocksRequests());
  assert(!state.restartPending());

  // A failed upload may remount the filesystem and resume service.
  state.updateFailed();
  assert(!state.restartPending());
  assert(!state.blocksRequests());

  // A completed SPIFFS upload stays isolated through controlled restart.
  state.filesystemUnmounting();
  state.restartScheduled();
  assert(state.restartPending());
  assert(state.blocksRequests());
}
