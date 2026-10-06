#include "../src/core/bt_audio_route_state.h"
#include "../src/core/radio_source_policy.h"
#include "../src/core/source_manager_state.h"

#include <cassert>

namespace {
BtLinkState connectedPhone() {
  BtLinkState bt{};
  bt.runtimeAvailable = true;
  bt.connected = true;
  bt.playback = BtPlayback::Playing;
  bt.sampleRate = 44100;
  return bt;
}

BtAudioRouteTarget route(const SourceManagerState& source,
                         const BtLinkState& bt) {
  return btAudioRouteTarget(source.active() == ActiveSource::Bluetooth,
                            bt.playback == BtPlayback::Playing,
                            bt.runtimeAvailable && bt.connected,
                            bt.sampleRate, 48000);
}
}  // namespace

int main() {
  constexpr uint16_t station = 17;
  BtLinkState bt = connectedPhone();

  // RADIO PLAY -> auto BT -> disconnect resumes the same station.
  SourceManagerState playing;
  playing.recordRadioCommand(true, station);
  playing.radioPlayConsumed();
  SourceUpdate update = playing.observe(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::BtConnect);
  RadioSourceActions actions = playing.radioActions(update, true);
  assert(actions.suspend && !actions.userStop && !actions.resume);
  assert(playing.radioPlayIntent());
  assert(!radioStopUpdatesSmartStart(RadioStopReason::SourceSwitch, false));
  assert(route(playing, bt).btOutput);
  bt.connected = false;
  update = playing.observe(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::BtDisconnect);
  actions = playing.radioActions(update, false);
  assert(actions.resume && !actions.suspend && !actions.userStop);
  assert(playing.active() == ActiveSource::Radio);
  assert(playing.radioStationForResume(station) == station);
  assert(!route(playing, bt).btOutput && route(playing, bt).radioOutput);
  assert(!radioPlayPreparationUpdatesSmartStart(true, 1));
  // Wi-Fi loss uses the same suspend/resume policy, so the PLAY intent is
  // still available if BT is selected before Wi-Fi reconnects.
  assert(playing.radioPlayIntent());
  assert(radioWifiReconnectShouldPlay(true, true));
  assert(!radioWifiReconnectShouldPlay(true, false));
  assert(!radioWifiReconnectShouldPlay(false, true));

  // RADIO STOP -> auto BT -> disconnect leaves the radio stopped.
  bt.connected = true;
  SourceManagerState stopped;
  update = stopped.observe(bt);
  assert(update.reason == SourceChangeReason::BtConnect);
  assert(!stopped.radioActions(update, false).resume);
  bt.connected = false;
  update = stopped.observe(bt);
  assert(update.reason == SourceChangeReason::BtDisconnect);
  assert(!stopped.radioActions(update, false).resume);
  assert(stopped.active() == ActiveSource::Radio && !stopped.radioPlayIntent());

  // Manual RADIO -> BT has the same intent-based fallback.
  BtLinkState online{};
  online.runtimeAvailable = true;
  SourceManagerState manualPlay;
  manualPlay.recordRadioCommand(true, station);
  update = manualPlay.cycle(online);
  assert(update.reason == SourceChangeReason::Manual);
  assert(manualPlay.radioActions(update, true).suspend);
  bt = connectedPhone();
  assert(!manualPlay.observe(bt).activeChanged);
  bt.connected = false;
  update = manualPlay.observe(bt);
  assert(update.reason == SourceChangeReason::BtDisconnect);
  assert(manualPlay.radioActions(update, false).resume);
  assert(manualPlay.radioStationForResume(3) == station);

  SourceManagerState manualStop;
  update = manualStop.cycle(online);
  assert(update.reason == SourceChangeReason::Manual);
  bt.connected = true;
  assert(!manualStop.observe(bt).activeChanged);
  bt.connected = false;
  update = manualStop.observe(bt);
  assert(update.reason == SourceChangeReason::BtDisconnect);
  assert(!manualStop.radioActions(update, false).resume);

  // Manual RADIO override survives repeated observations and disconnect.
  SourceManagerState overrideSource;
  overrideSource.recordRadioCommand(true, station);
  bt = connectedPhone();
  assert(overrideSource.observe(bt).reason == SourceChangeReason::BtConnect);
  update = overrideSource.cycle(bt);
  assert(update.reason == SourceChangeReason::Manual);
  assert(overrideSource.radioActions(update, false).resume);
  for (int i = 0; i < 100; ++i) {
    update = overrideSource.observe(bt);
    assert(!update.activeChanged && overrideSource.active() == ActiveSource::Radio);
    assert(!overrideSource.radioActions(update, true).resume);
  }
  bt.connected = false;
  update = overrideSource.observe(bt);
  assert(update.btDisconnected && !update.activeChanged);
  assert(!overrideSource.radioActions(update, true).resume);
  bt.runtimeAvailable = false;
  update = overrideSource.observe(bt);
  assert(!update.activeChanged && update.reason == SourceChangeReason::None);
  assert(!overrideSource.radioActions(update, true).resume);
  bt.runtimeAvailable = true;
  bt.connected = true;
  update = overrideSource.observe(bt);
  assert(update.btConnected && update.reason == SourceChangeReason::BtConnect);
  assert(overrideSource.active() == ActiveSource::Bluetooth);
  assert(overrideSource.radioActions(update, true).suspend);

  // Module offline has the same fallback policy, including runtime OFF.
  bt.runtimeAvailable = false;
  update = overrideSource.observe(bt);
  assert(update.reason == SourceChangeReason::BtOffline);
  assert(overrideSource.radioActions(update, false).resume);
  assert(overrideSource.active() == ActiveSource::Radio);
  SourceManagerState offlineStop;
  bt = connectedPhone();
  assert(offlineStop.observe(bt).reason == SourceChangeReason::BtConnect);
  bt.runtimeAvailable = false;
  update = offlineStop.observe(bt);
  assert(update.reason == SourceChangeReason::BtOffline);
  assert(!offlineStop.radioActions(update, false).resume);
}
