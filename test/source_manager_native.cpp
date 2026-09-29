#include "../src/core/source_manager_state.h"

#include <cassert>
#include <cstring>

int main() {
  SourceManagerState sources;
  BtLinkState bt{};
  DisplaySourceView view{};

  // A: boot starts on radio.
  assert(sources.active() == ActiveSource::Radio);
  sources.displayView(view);
  assert(view.kind == DisplaySourceKind::Radio);

  // B: an online module without a phone does not auto-select BT.
  assert(!sources.cycle(bt).activeChanged);
  bt.runtimeAvailable = true;
  assert(!sources.observe(bt).activeChanged);
  assert(sources.active() == ActiveSource::Radio);

  // C: only a disconnected -> connected edge selects BT automatically.
  bt.connected = true;
  std::strcpy(bt.peerName, "Telefon");
  std::strcpy(bt.artist, "Żółć");
  std::strcpy(bt.title, "Utwór");
  bt.playback = BtPlayback::Playing;
  SourceUpdate update = sources.observe(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::BtConnect);
  assert(update.stationChanged && update.titleChanged);
  assert(sources.active() == ActiveSource::Bluetooth);
  sources.displayView(view);
  assert(view.kind == DisplaySourceKind::Bluetooth && view.connected);
  assert(std::strcmp(view.peerName, "Telefon") == 0);
  assert(std::strcmp(view.artist, "Żółć") == 0);
  assert(std::strcmp(view.title, "Utwór") == 0);

  // D: manual BT -> RADIO stays RADIO while the phone remains connected.
  update = sources.cycle(bt);
  assert(update.reason == SourceChangeReason::Manual);
  assert(sources.active() == ActiveSource::Radio);
  for (int i = 0; i < 10; ++i) {
    assert(!sources.observe(bt).activeChanged);
    assert(sources.active() == ActiveSource::Radio);
  }

  // E: disconnect while RADIO does not switch sources.
  bt.connected = false;
  bt.peerName[0] = bt.artist[0] = bt.title[0] = '\0';
  bt.playback = BtPlayback::Stopped;
  assert(!sources.observe(bt).activeChanged);
  assert(sources.active() == ActiveSource::Radio);

  // F: a later, real connect edge can select BT again.
  bt.connected = true;
  std::strcpy(bt.peerName, "Inny telefon");
  update = sources.observe(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::BtConnect);
  assert(sources.active() == ActiveSource::Bluetooth);

  // Changed BT fields request only the relevant partial LCD update.
  std::strcpy(bt.peerName, "Telefon 2");
  update = sources.observe(bt);
  assert(update.stationChanged && !update.titleChanged);
  std::strcpy(bt.artist, "Nowy artysta");
  update = sources.observe(bt);
  assert(!update.stationChanged && update.titleChanged);
  std::strcpy(bt.title, "Nowy utwór");
  update = sources.observe(bt);
  assert(!update.stationChanged && update.titleChanged);
  bt.playback = BtPlayback::Paused;
  update = sources.observe(bt);
  assert(!update.stationChanged && update.titleChanged);

  // G: disconnect while BT is active returns to RADIO.
  bt.connected = false;
  bt.peerName[0] = bt.artist[0] = bt.title[0] = '\0';
  bt.playback = BtPlayback::Stopped;
  update = sources.observe(bt);
  assert(update.stationChanged && update.titleChanged);
  assert(update.activeChanged && update.reason == SourceChangeReason::BtDisconnect);
  assert(sources.active() == ActiveSource::Radio);
  for (int i = 0; i < 10; ++i) {
    assert(!sources.observe(bt).activeChanged);
    assert(sources.active() == ActiveSource::Radio);
  }

  // H: manual selection works while disconnected and remains selected.
  update = sources.cycle(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::Manual);
  assert(sources.active() == ActiveSource::Bluetooth);
  for (int i = 0; i < 10; ++i) {
    assert(!sources.observe(bt).activeChanged);
    assert(sources.active() == ActiveSource::Bluetooth);
  }
  sources.displayView(view);
  assert(view.kind == DisplaySourceKind::Bluetooth && !view.connected);

  // I: loss of the module has priority over phone disconnect.
  bt.runtimeAvailable = false;
  update = sources.observe(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::BtOffline);
  assert(update.stationChanged && update.titleChanged);
  assert(sources.active() == ActiveSource::Radio);
  assert(!sources.cycle(bt).activeChanged);
  sources.displayView(view);
  assert(view.kind == DisplaySourceKind::Radio);
}
