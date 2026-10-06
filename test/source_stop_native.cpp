#include "../src/core/bt_transport.h"
#include "../src/core/dac_mute_state.h"
#include "../src/core/mute_state.h"
#include "../src/core/source_manager_state.h"
#include "../src/core/bt_runtime.h"

#include <cassert>

int main() {
  SourceManagerState source;
  DisplaySourceView view{};
  MuteState mute;
  mute.set(true);

  source.displayView(view);
  assert(source.active() == ActiveSource::Radio);
  assert(view.playback == DisplayPlaybackState::Stopped);
  assert(btEncoderClickAction(PLAYER, false) == BtEncoderClickAction::RadioToggle);
  assert(!dacXsmtHigh(dacPlaybackForSource(false, false, false,
                                         BtPlayback::Stopped), false));

  BtLinkState bt{};
  bt.runtimeAvailable = true;
  source.observe(bt);
  bt.connected = true;
  bt.playback = BtPlayback::Playing;
  bt.sampleRate = 44100;
  SourceUpdate update = source.observe(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::BtConnect);
  source.displayView(view);
  assert(view.playback == DisplayPlaybackState::Playing);
  assert(source.bluetoothAudioOutputAllowed());
  assert(dacXsmtHigh(dacPlaybackForSource(true, false,
                                        source.bluetoothAudioOutputAllowed(),
                                        bt.playback), false));

  update = source.cycle(bt);  // BT PLAY -> RADIO STOP.
  assert(update.activeChanged && source.active() == ActiveSource::Radio);
  source.displayView(view);
  assert(view.playback == DisplayPlaybackState::Stopped);
  assert(!source.bluetoothAudioOutputAllowed() && mute.active());
  for (int i = 0; i < 10; ++i)
    assert(!source.observe(bt).activeChanged);
  assert(source.active() == ActiveSource::Radio);

  update = source.cycle(bt);  // RADIO -> BT; phone continues playing.
  assert(update.activeChanged && source.active() == ActiveSource::Bluetooth);
  source.displayView(view);
  assert(view.playback == DisplayPlaybackState::Playing);
  assert(source.bluetoothAudioOutputAllowed() && mute.active());
  bt.playback = BtPlayback::Paused;
  source.observe(bt);
  assert(!source.bluetoothAudioOutputAllowed());
  source.displayView(view);
  assert(view.playback == DisplayPlaybackState::Paused);
  bt.playback = BtPlayback::Playing;  // Remote event restores the route.
  source.observe(bt);
  assert(source.bluetoothAudioOutputAllowed());
  source.displayView(view);
  assert(view.playback == DisplayPlaybackState::Playing);

  bt.connected = false;
  bt.playback = BtPlayback::Stopped;
  update = source.observe(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::BtDisconnect);
  source.displayView(view);
  assert(source.active() == ActiveSource::Radio);
  assert(view.playback == DisplayPlaybackState::Stopped);
  assert(mute.active());

  update = source.cycle(bt);  // Manual BT selection without a phone.
  assert(update.activeChanged && source.active() == ActiveSource::Bluetooth);
  source.displayView(view);
  assert(view.playback == DisplayPlaybackState::None);
  assert(!source.bluetoothAudioOutputAllowed());
  for (int i = 0; i < 10; ++i) {
    assert(!source.observe(bt).activeChanged);
    assert(source.active() == ActiveSource::Bluetooth);
  }
  bt.connected = true;
  bt.playback = BtPlayback::Playing;
  update = source.observe(bt);
  assert(update.btConnected && !update.activeChanged);
  source.displayView(view);
  assert(view.playback == DisplayPlaybackState::Playing);
  assert(source.bluetoothAudioOutputAllowed());

  // A lost backend is different from a disconnected phone: return to RADIO
  // in STOP, retain user MUTE, and keep the logical DAC output muted.
  bt.runtimeAvailable = false;
  update = source.observe(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::BtOffline);
  assert(source.active() == ActiveSource::Radio);
  source.displayView(view);
  assert(view.playback == DisplayPlaybackState::Stopped);
  assert(!source.bluetoothAudioOutputAllowed() && mute.active());
  assert(!dacXsmtHigh(dacPlaybackForSource(false, false, false,
                                         BtPlayback::Stopped), false));
  assert(!source.cycle(bt).activeChanged);  // Unavailable BT is skipped.
  assert(source.active() == ActiveSource::Radio);

  // Runtime OFF follows the same RADIO STOP fallback as a lost BT backend.
  BtRuntime runtime(true);
  assert(runtime.start());
  SourceManagerState disabledSource;
  BtLinkState connectedBt{};
  connectedBt.runtimeAvailable = true;
  connectedBt.connected = true;
  connectedBt.playback = BtPlayback::Playing;
  assert(disabledSource.observe(runtime.effectiveLinkState(connectedBt)).activeChanged);
  runtime.setEnabled(false);
  update = disabledSource.observe(runtime.effectiveLinkState(connectedBt));
  assert(update.activeChanged && update.reason == SourceChangeReason::BtOffline);
  disabledSource.displayView(view);
  assert(disabledSource.active() == ActiveSource::Radio);
  assert(view.playback == DisplayPlaybackState::Stopped && mute.active());
  assert(!dacXsmtHigh(dacPlaybackForSource(false, false, false,
                                         BtPlayback::Stopped), false));
}
