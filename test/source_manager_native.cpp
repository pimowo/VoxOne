#include "../src/core/source_manager_state.h"
#include "../src/core/bt_transport.h"

#include <cassert>
#include <cstring>
#include <initializer_list>

int main() {
  SourceManagerState sources;
  BtLinkState bt{};
  DisplaySourceView view{};

  // A: boot starts on radio.
  assert(sources.active() == ActiveSource::Radio);
  sources.displayView(view);
  assert(view.kind == DisplaySourceKind::Radio);
  assert(view.playback == DisplayPlaybackState::Stopped);
  assert(!displaySourceVuVisible(view));
  assert(!displayVuUnlocked(true, displaySourceVuVisible(view), 50));
  assert(std::strcmp(displayPlaybackLabel(view.playback), "STOP") == 0);
  sources.displayView(view, true);
  assert(view.playback == DisplayPlaybackState::Playing);
  assert(displaySourceVuVisible(view));
  for (uint8_t volume : {0, 1, 50}) {
    assert(displayVuUnlocked(true, displaySourceVuVisible(view), volume) ==
           (volume > 0));
    assert(!displayVuUnlocked(false, displaySourceVuVisible(view), volume));
  }
  assert(std::strcmp(displayPlaybackLabel(view.playback), "PLAY") == 0);
  assert(btTransportAction(BtTransportInput::Toggle, view) == BtTransportAction::None);

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
  bt.rawVuLeft = 12000;
  bt.rawVuRight = 3000;
  bt.rawVuLastMs = 100;
  SourceUpdate update = sources.observe(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::BtConnect);
  assert(update.stationChanged && update.titleChanged);
  assert(sources.active() == ActiveSource::Bluetooth);
  sources.displayView(view);
  assert(view.kind == DisplaySourceKind::Bluetooth && view.connected);
  assert(std::strcmp(view.peerName, "Telefon") == 0);
  assert(std::strcmp(view.artist, "Żółć") == 0);
  assert(std::strcmp(view.title, "Utwór") == 0);
  assert(view.playback == DisplayPlaybackState::Playing);
  assert(displaySourceVuVisible(view));
  for (uint8_t volume : {0, 1, 50}) {
    assert(displayVuUnlocked(true, displaySourceVuVisible(view), volume) ==
           (volume > 0));
    assert(!displayVuUnlocked(false, displaySourceVuVisible(view), volume));
  }
  uint16_t rawLeft = 0, rawRight = 0;
  uint32_t rawMs = 0;
  sources.bluetoothRawVu(rawLeft, rawRight, rawMs);
  assert(rawLeft == 12000 && rawRight == 3000 && rawMs == 100);
  assert(std::strcmp(displayPlaybackLabel(view.playback), "PLAY") == 0);
  assert(btTransportInputForRotation(-1) == BtTransportInput::Previous);
  assert(btTransportInputForRotation(1) == BtTransportInput::Next);
  assert(btTransportAction(BtTransportInput::Previous, view) == BtTransportAction::Previous);
  assert(btTransportAction(BtTransportInput::Next, view) == BtTransportAction::Next);
  assert(btTransportAction(BtTransportInput::Toggle, view) == BtTransportAction::Pause);

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
  sources.displayView(view);
  assert(view.playback == DisplayPlaybackState::Paused);
  assert(displaySourceVuVisible(view));
  assert(std::strcmp(displayPlaybackLabel(view.playback), "PAUZA") == 0);
  assert(btTransportAction(BtTransportInput::Toggle, view) == BtTransportAction::Play);
  bt.playback = BtPlayback::Stopped;
  update = sources.observe(bt);
  assert(update.titleChanged);
  sources.displayView(view);
  assert(view.playback == DisplayPlaybackState::Stopped);
  assert(!displaySourceVuVisible(view));
  assert(btTransportAction(BtTransportInput::Toggle, view) == BtTransportAction::Play);

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
  assert(std::strcmp(view.peerName, "Bluetooth") == 0);
  assert(std::strcmp(view.artist, "Oczekuję na połączenie...") == 0);
  assert(std::strcmp(view.title, "") == 0);
  assert(view.playback == DisplayPlaybackState::None);
  assert(std::strcmp(displayPlaybackLabel(view.playback), "") == 0);
  assert(btTransportAction(BtTransportInput::Toggle, view) == BtTransportAction::None);
  assert(btTransportAction(BtTransportInput::Next, view) == BtTransportAction::None);

  // Connected placeholder metadata is exposed as empty LCD fields.
  bt.connected = true;
  std::strcpy(bt.artist, "Not Provided");
  std::strcpy(bt.title, "NOT PROVIDED");
  update = sources.observe(bt);
  assert(!update.activeChanged && update.stationChanged && update.titleChanged);
  sources.displayView(view);
  assert(view.connected);
  assert(std::strcmp(view.artist, "") == 0);
  assert(std::strcmp(view.title, "") == 0);

  std::strcpy(bt.artist, "not provided");
  std::strcpy(bt.title, "Nowy utwór");
  sources.observe(bt);
  sources.displayView(view);
  assert(std::strcmp(view.artist, "") == 0);
  assert(std::strcmp(view.title, "Nowy utwór") == 0);

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
