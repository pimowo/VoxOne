#include "../src/core/mute_state.h"
#include "../src/core/source_manager_state.h"

#include <cassert>

int main() {
  MuteState mute;
  uint8_t userVolume = 27;
  SourceManagerState sources;
  DisplaySourceView view{};
  sources.displayView(view, true);
  const ActiveSource sourceBefore = sources.active();
  const DisplayPlaybackState playbackBefore = view.playback;

  assert(!mute.active() && mute.outputVolume(80) == 80);
  assert(mute.toggle());
  assert(userVolume == 27 && mute.outputVolume(80) == 0);
  assert(mute.outputSilent(userVolume));
  assert(!mute.toggle());
  assert(userVolume == 27 && mute.outputVolume(80) == 80);

  mute.set(true);
  userVolume = mute.stepUserVolume(userVolume, 1);
  assert(userVolume == 28 && !mute.active());
  userVolume = 27;
  mute.set(true);
  userVolume = mute.stepUserVolume(userVolume, -1);
  assert(userVolume == 26 && !mute.active());

  userVolume = 0;
  assert(mute.outputSilent(userVolume) && !mute.active());
  assert(displayVolumeMuted(userVolume, mute.active()));
  assert(mute.stepUserVolume(userVolume, 1) == 1);
  mute.set(true);
  assert(mute.outputSilent(50));
  assert(displayVolumeMuted(50, mute.active()));

  sources.displayView(view, true);
  assert(sources.active() == sourceBefore);
  assert(view.playback == playbackBefore);

  BtLinkState phone{};
  phone.runtimeAvailable = true;
  phone.connected = true;
  phone.playback = BtPlayback::Playing;
  sources.observe(phone);
  sources.displayView(view, true);
  mute.toggle();
  assert(sources.active() == ActiveSource::Bluetooth);
  assert(view.playback == DisplayPlaybackState::Playing);
  mute.set(true);
  sources.cycle(phone);
  assert(mute.active());
  sources.cycle(phone);
  assert(mute.active());
  assert(sources.bluetoothAudioOutputAllowed());
  sources.displayView(view, true);
  assert(view.playback == DisplayPlaybackState::Playing);
  sources.displayView(view, true);
  assert(view.playback == DisplayPlaybackState::Playing);
}
