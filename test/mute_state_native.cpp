#include "../src/core/mute_state.h"
#include "../src/core/source_manager_state.h"

#include <cassert>
#include <cstring>

int main() {
  char st7789Volume[12];
  const auto st7789Text = [&](uint8_t volume, bool muted) {
    return displaySt7789VolumeText(volume, muted, st7789Volume, sizeof(st7789Volume));
  };
  assert(std::strcmp(st7789Text(27, false), "\023 27") == 0);
  assert(std::strcmp(st7789Text(27, true), "MUTE") == 0);
  assert(std::strcmp(st7789Text(28, true), "MUTE") == 0);
  assert(std::strcmp(st7789Text(28, false), "\023 28") == 0);
  // The same current state is used after a refresh and on return from VOL.
  assert(std::strcmp(st7789Text(28, true), "MUTE") == 0);
  assert(displayVolumeMuted(28, true));
  assert(std::strcmp(st7789Text(28, false), "\023 28") == 0);
  assert(std::strcmp(st7789Text(0, false), "\023 0") == 0);

  // ST7796 and OLED 128x64 display MUTE only for the explicit mute state;
  // otherwise they print the unchanged user volume.
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
  assert(std::strcmp(st7789Text(0, true), "MUTE") == 0);
  assert(std::strcmp(st7789Text(50, false), "\023 50") == 0);
  assert(std::strcmp(st7789Text(50, true), "MUTE") == 0);

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
