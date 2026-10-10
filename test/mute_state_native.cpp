#include "../src/core/mute_state.h"
#include "../src/core/source_manager_state.h"

#include <cassert>
#include <cstring>

int main() {
  char x0Volume[12];
  const auto x0Text = [&](uint8_t volume, bool muted) {
    return displayX0VolumeText(volume, muted, x0Volume, sizeof(x0Volume));
  };
  assert(std::strcmp(x0Text(27, false), "\023 27") == 0);
  assert(std::strcmp(x0Text(27, true), "MUTE") == 0);
  assert(std::strcmp(x0Text(28, true), "MUTE") == 0);
  assert(std::strcmp(x0Text(28, false), "\023 28") == 0);
  // The same current state is used after a refresh and on return from VOL.
  assert(std::strcmp(x0Text(28, true), "MUTE") == 0);
  assert(displayVolumeMuted(28, true));
  assert(std::strcmp(x0Text(28, false), "\023 28") == 0);
  assert(std::strcmp(x0Text(0, false), "\023 0") == 0);

  // A0 displays MUTE only for the explicit mute state; C0 uses that state
  // directly and otherwise prints the unchanged user volume.
  assert(!displayVolumeMuted(0, false));
  assert(displayVolumeMuted(0, true));
  assert(!displayVolumeMuted(50, false));
  assert(displayVolumeMuted(50, true));
  MuteState muteContract;
  muteContract.set(false);
  assert(muteContract.outputVolume(0) == 0);
  assert(muteContract.outputVolume(50) == 50);
  muteContract.set(true);
  assert(muteContract.outputVolume(0) == 0);
  assert(muteContract.outputVolume(50) == 0);
  assert(std::strcmp(x0Text(0, true), "MUTE") == 0);
  assert(std::strcmp(x0Text(50, false), "\023 50") == 0);
  assert(std::strcmp(x0Text(50, true), "MUTE") == 0);

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
  assert(!displayVolumeMuted(userVolume, mute.active()));
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
