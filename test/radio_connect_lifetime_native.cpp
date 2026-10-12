#include "../src/core/radio_connect_lifetime.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

using voxone::radio::ConnectAttemptLifetime;
using voxone::radio::ConnectAttemptState;
using voxone::radio::ConnectCompletion;
using voxone::radio::connectWaitExpired;

namespace {

struct CleanupProbe {
  int calls = 0;

  void operator()() {
    ++calls;
  }
};

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
  // Success publishes only after the worker-owned cleanup has run once.
  ConnectAttemptLifetime attempt;
  assert(attempt.begin());
  CleanupProbe successCleanup;
  CleanupProbe successReject;
  assert(attempt.complete(true, [&successCleanup]() { successCleanup(); },
                          [&successReject]() { successReject(); }) ==
         ConnectCompletion::Accepted);
  assert(successCleanup.calls == 1 && successReject.calls == 0);
  assert(attempt.succeeded() && !attempt.active());

  // A completed connect error is distinct from an in-progress connection.
  assert(attempt.begin());
  CleanupProbe failureCleanup;
  CleanupProbe failureReject;
  assert(attempt.complete(false, [&failureCleanup]() { failureCleanup(); },
                          [&failureReject]() { failureReject(); }) ==
         ConnectCompletion::Rejected);
  assert(failureCleanup.calls == 1 && failureReject.calls == 0);
  assert(attempt.state() == ConnectAttemptState::Failed);

  // Timeout is a logical cancellation. The caller never deletes the worker.
  assert(attempt.begin());
  assert(attempt.requestCancel());
  assert(attempt.cancellationRequested() && attempt.active());
  CleanupProbe timeoutCleanup;
  CleanupProbe timeoutReject;
  assert(attempt.complete(false, [&timeoutCleanup]() { timeoutCleanup(); },
                          [&timeoutReject]() { timeoutReject(); }) ==
         ConnectCompletion::Rejected);
  assert(timeoutCleanup.calls == 1 && timeoutReject.calls == 1);
  assert(attempt.state() == ConnectAttemptState::Canceled);

  // A delayed socket success after timeout is rejected and cannot be used.
  assert(attempt.begin());
  assert(attempt.requestCancel());
  CleanupProbe delayedCleanup;
  CleanupProbe delayedReject;
  assert(attempt.complete(true, [&delayedCleanup]() { delayedCleanup(); },
                          [&delayedReject]() { delayedReject(); }) ==
         ConnectCompletion::Rejected);
  assert(delayedCleanup.calls == 1 && delayedReject.calls == 1);
  assert(!attempt.succeeded());

  // STOP during connect uses the same cancellation transition.
  assert(attempt.begin());
  assert(attempt.requestCancel());
  CleanupProbe stopCleanup;
  CleanupProbe stopReject;
  attempt.complete(true, [&stopCleanup]() { stopCleanup(); },
                   [&stopReject]() { stopReject(); });
  assert(stopCleanup.calls == 1 && stopReject.calls == 1);
  assert(attempt.state() == ConnectAttemptState::Canceled);

  // A station change cancels the old worker before a new attempt is admitted.
  assert(attempt.begin());
  assert(attempt.requestCancel());
  assert(!attempt.begin());
  CleanupProbe stationCleanup;
  attempt.complete(false, [&stationCleanup]() { stationCleanup(); }, []() {});
  assert(stationCleanup.calls == 1);
  assert(attempt.begin());
  CleanupProbe replacementCleanup;
  attempt.complete(true, [&replacementCleanup]() { replacementCleanup(); },
                   []() {});
  assert(replacementCleanup.calls == 1);

  // STOP can also win after the worker entered finalization; the caller does
  // not wait and the worker performs the late-success rejection itself.
  assert(attempt.begin());
  CleanupProbe finalizingCleanup;
  CleanupProbe finalizingReject;
  assert(attempt.complete(true, [&]() {
           finalizingCleanup();
           assert(attempt.finalizing());
           assert(attempt.requestCancel());
         }, [&]() { finalizingReject(); }) == ConnectCompletion::Rejected);
  assert(finalizingCleanup.calls == 1 && finalizingReject.calls == 1);
  assert(attempt.state() == ConnectAttemptState::Canceled);

  // Reconnect cycles remain bounded to one active worker and clean once each.
  int reconnectCleanups = 0;
  for (int cycle = 0; cycle < 1000; ++cycle) {
    assert(attempt.begin());
    assert(!attempt.begin());
    const bool connected = (cycle % 3) != 0;
    attempt.complete(connected, [&reconnectCleanups]() {
      ++reconnectCleanups;
    }, []() {});
    assert(!attempt.active());
  }
  assert(reconnectCleanups == 1000);

  // Re-entering completion cannot double-clean a released parameter block.
  CleanupProbe duplicateCleanup;
  assert(attempt.complete(true, [&duplicateCleanup]() { duplicateCleanup(); },
                          []() {}) == ConnectCompletion::Invalid);
  assert(duplicateCleanup.calls == 0);

  // Unsigned elapsed time is deterministic across millis() wrap.
  const uint32_t nearWrap = std::numeric_limits<uint32_t>::max() - 100U;
  assert(!connectWaitExpired(nearWrap, 49U, 150U));
  assert(connectWaitExpired(nearWrap, 50U, 150U));
  assert(!connectWaitExpired(1000U, 6700U, 5700U));
  assert(connectWaitExpired(1000U, 6701U, 5700U));

  // Production integration keeps worker ownership and cancellation wired to
  // the actual Audio and Player paths; no caller-side task deletion remains.
  const std::string audio = readFile("src/audioI2S/Audio.cpp");
  const std::string player = readFile("src/core/player.cpp");
  assert(audio.find("connectClient->connect(params->hostwoext, params->port,") !=
         std::string::npos);
  assert(audio.find("m_connectAttempt.complete(") != std::string::npos);
  assert(audio.find("vTaskDelete(_connectTaskHandle)") == std::string::npos);
  assert(audio.find("Connection attempt canceled or timed out") !=
         std::string::npos);
  assert(player.find("case PR_STOP:\n    case PR_BURL:") != std::string::npos);
  assert(player.find("case PR_PLAY:") != std::string::npos);
  assert(player.find("cancelPendingConnection();") != std::string::npos);

  return 0;
}
