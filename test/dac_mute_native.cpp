#include "../src/core/dac_mute_state.h"
#include "../src/core/display.h"
#include "../src/core/mute_state.h"

#include <cassert>

int main() {
  const auto frameRed = [](DacPlaybackState playback) {
    return displayVolumeFrameRed(27, false, !dacXsmtHigh(playback, false));
  };
  const auto sourcePlayback = [](bool btSelected, bool radioPlaying,
                                 bool btReady, BtPlayback btPlayback) {
    return dacPlaybackForSource(btSelected, radioPlaying, btReady, btPlayback);
  };
  assert(!frameRed(sourcePlayback(false, true, false, BtPlayback::Stopped)));
  assert(frameRed(sourcePlayback(false, false, false, BtPlayback::Stopped)));
  assert(frameRed(sourcePlayback(true, true, false, BtPlayback::Playing)));
  assert(frameRed(sourcePlayback(true, false, true, BtPlayback::Stopped)));
  assert(frameRed(sourcePlayback(true, false, true, BtPlayback::Paused)));
  assert(frameRed(sourcePlayback(true, false, true,
                                 static_cast<BtPlayback>(255))));
  assert(!frameRed(sourcePlayback(true, false, true, BtPlayback::Playing)));
  assert(displayVolumeFrameRed(27, true, false));
  assert(displayVolumeMuted(27, true));
  assert(displayVolumeFrameRed(0, false, false));
  assert(displayVolumeMuted(0, false));

  assert(dacXsmtHigh(DacPlaybackState::Playing, false));
  assert(!dacXsmtHigh(DacPlaybackState::Paused, false));
  assert(!dacXsmtHigh(DacPlaybackState::Stopped, false));
  assert(!dacXsmtHigh(DacPlaybackState::Playing, true));

  MuteState userMute;
  userMute.set(true);
  assert(dacXsmtHigh(DacPlaybackState::Playing, false));
  assert(userMute.outputSilent(27));
  assert(userMute.outputVolume(80) == 0);
  userMute.set(false);
  assert(dacXsmtHigh(DacPlaybackState::Playing, false));
  assert(!userMute.outputSilent(27));
}
