#include <assert.h>
#include <initializer_list>

#include "../src/core/bt_audio_route_state.h"

int main() {
  const auto startup = btAudioRouteTarget(false, false, 0, 44100);
  assert(startup.radioOutput && !startup.btOutput);

  const auto connected = btAudioRouteTarget(true, true, 44100, 32000);
  assert(!connected.radioOutput && connected.btOutput);
  assert(connected.outputRate == 44100);

  const auto paused = btAudioRouteTarget(true, true, 44100, 32000);
  assert(paused.btOutput && !paused.radioOutput);  // No PCM means silence, not RADIO.

  const auto rateChanged = btAudioRouteTarget(true, true, 48000, 32000);
  assert(rateChanged.outputRate == 48000);

  const auto rxStopped = btAudioRouteTarget(true, false, 44100, 32000);
  assert(!rxStopped.radioOutput && !rxStopped.btOutput);
  assert(rxStopped.outputRate == 0);

  const auto disconnected = btAudioRouteTarget(false, false, 0, 32000);
  assert(disconnected.radioOutput && !disconnected.btOutput);
  assert(disconnected.outputRate == 32000);

  const auto manualRadio = btAudioRouteTarget(false, true, 44100, 32000);
  assert(manualRadio.radioOutput && !manualRadio.btOutput);

  for (bool selected : {false, true}) {
    for (bool running : {false, true}) {
      const auto state = btAudioRouteTarget(selected, running, 44100, 32000);
      assert(!(state.radioOutput && state.btOutput));
    }
  }
}
