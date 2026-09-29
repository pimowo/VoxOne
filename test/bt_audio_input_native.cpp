#include "../src/core/bt_audio_input_state.h"

#include <cassert>

int main() {
  BtLinkState link{};
  assert(btAudioDesiredRate(link) == 0);

  link.runtimeAvailable = true;
  link.sampleRate = 44100;
  assert(btAudioDesiredRate(link) == 0);

  link.connected = true;
  assert(btAudioDesiredRate(link) == 44100);
  link.playback = BtPlayback::Paused;
  assert(btAudioDesiredRate(link) == 44100);

  link.sampleRate = 48000;
  assert(btAudioDesiredRate(link) == 48000);
  link.sampleRate = 0;
  assert(btAudioDesiredRate(link) == 0);
  link.sampleRate = 96000;
  assert(btAudioDesiredRate(link) == 0);

  link.sampleRate = 44100;
  link.connected = false;
  assert(btAudioDesiredRate(link) == 0);
  link.connected = true;
  link.runtimeAvailable = false;
  assert(btAudioDesiredRate(link) == 0);
}
