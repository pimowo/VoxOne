#include "../src/core/source_manager_state.h"
#include "../src/core/bt_transport.h"
#include "../src/core/bt_runtime.h"
#include "../src/core/player_display_view.h"

#include <cassert>
#include <cstring>
#include <initializer_list>
#include <string>

int main() {
  static_assert(sizeof(BtSourceSnapshot) < sizeof(BtLinkState),
                "SourceManager snapshot must remain smaller than BtLinkState");
  PlayerDisplayView playerView{};
  DisplaySourceView raw{};
  raw.kind = DisplaySourceKind::Radio;
  raw.playback = DisplayPlaybackState::Playing;
  makePlayerDisplayView(playerView, raw, "Radio One", "Italove - Magic Night",
                        false, 0, false, false, -50, 320, BF_MP3);
  assert(std::strcmp(playerView.station, "Radio One") == 0);
  assert(std::strcmp(playerView.artist, "Italove") == 0);
  assert(std::strcmp(playerView.title, "Magic Night") == 0);
  makePlayerDisplayView(playerView, raw, "Radio One", "Radio One - Magic Night",
                        false, 0, false, false, -50, 320, BF_MP3);
  assert(std::strcmp(playerView.artist, "Magic Night") == 0 && playerView.title[0] == '\0');
  assert(playerView.source == DisplaySourceKind::Radio &&
         playerView.playback == DisplayPlaybackState::Playing);
  assert(playerView.userVolume == 0 && !playerView.muted);
  assert(playerView.wifiLevel == 4 && playerView.audioInfo.radioBitrate == 320 &&
         playerView.audioInfo.radioFormat == BF_MP3 &&
         std::strcmp(playerView.audioText, "MP3 320") == 0);
  makePlayerDisplayView(playerView, raw, "Radio One", "", false,
                        50, false, true, -50, 320, BF_MP3);
  assert(playerView.source == DisplaySourceKind::Radio && playerView.btConnected);
  raw.playback = DisplayPlaybackState::Stopped;
  makePlayerDisplayView(playerView, raw, "Radio One", "Radio One", false,
                        0, true, false, -81, 0, BF_UNKNOWN);
  assert(std::strcmp(playerView.station, "WEB Radio") == 0);
  assert(playerView.artist[0] == '\0' && playerView.title[0] == '\0');
  assert(playerView.userVolume == 0 && playerView.muted && playerView.wifiLevel == 0);
  makePlayerDisplayView(playerView, raw, "", "Magic Night", false,
                        50, false, false, -60, 0, BF_UNKNOWN);
  assert(std::strcmp(playerView.station, "WEB Radio") == 0);
  assert(std::strcmp(playerView.artist, "Magic Night") == 0 && playerView.title[0] == '\0');
  makePlayerDisplayView(playerView, raw, "Radio One", "Italove - ", false,
                        50, true, false, -70, 0, BF_UNKNOWN);
  assert(std::strcmp(playerView.artist, "Italove") == 0 && playerView.title[0] == '\0');
  makePlayerDisplayView(playerView, raw, "Radio One", "", false,
                        50, false, false, -80, 0, BF_UNKNOWN);
  assert(playerView.artist[0] == '\0' && playerView.title[0] == '\0');
  raw.kind = DisplaySourceKind::Bluetooth;
  raw.connected = true;
  raw.peerName = "Telefon";
  raw.artist = "";
  raw.title = "Magic Night";
  raw.sampleRate = 44100;
  makePlayerDisplayView(playerView, raw, "Radio One", "Radio metadata", false,
                        50, false, true, -51, 320, BF_MP3);
  assert(std::strcmp(playerView.station, "Telefon") == 0);
  assert(std::strcmp(playerView.artist, "Magic Night") == 0 && playerView.title[0] == '\0');
  assert(playerView.btConnected && playerView.wifiLevel == 3 &&
         playerView.audioInfo.bluetooth &&
         std::strcmp(playerView.audioText, "44.1 kHz") == 0);
  raw.peerName = "";
  makePlayerDisplayView(playerView, raw, "Radio One", "", false,
                        50, false, true, -61, 0, BF_UNKNOWN);
  assert(std::strcmp(playerView.station, "Bluetooth") == 0 && playerView.wifiLevel == 2);
  raw.connected = false;
  raw.peerName = "Stale peer";
  makePlayerDisplayView(playerView, raw, "Radio One", "", false,
                        50, false, false, -71, 0, BF_UNKNOWN);
  assert(std::strcmp(playerView.station, "Bluetooth") == 0 &&
         !playerView.btConnected && playerView.wifiLevel == 1);
  raw.kind = DisplaySourceKind::Dlna;
  makePlayerDisplayView(playerView, raw, "Radio One", "", false,
                        50, false, false, 0, 320, BF_MP3);
  assert(std::strcmp(playerView.station, "DLNA") == 0 && playerView.audioText[0] == '\0');
  raw.kind = DisplaySourceKind::Aux;
  makePlayerDisplayView(playerView, raw, "Radio One", "", false,
                        50, false, false, -49, 320, BF_MP3);
  assert(std::strcmp(playerView.station, "AUX") == 0 && playerView.audioText[0] == '\0');
  raw.kind = DisplaySourceKind::Local;
  makePlayerDisplayView(playerView, raw, "Radio One", "Stale radio metadata",
                        false, 50, false, false, -49, 320, BF_MP3);
  assert(std::strcmp(playerView.station, "SD") == 0 &&
         playerView.artist[0] == '\0' && playerView.title[0] == '\0' &&
         playerView.audioText[0] == '\0');
  assert(playerWifiLevel(-59) == 3 && playerWifiLevel(-69) == 2 &&
         playerWifiLevel(-79) == 1 && playerWifiLevel(-81) == 0);

  raw.kind = DisplaySourceKind::Bluetooth;
  raw.connected = true;
  raw.peerName = "Łódź";
  raw.artist = "Zażółć gęślą jaźń";
  raw.title = "Ścieżka źródłowa";
  makePlayerDisplayView(playerView, raw, "Radio One", "", false,
                        50, false, true, -50, 0, BF_UNKNOWN);
  assert(std::strcmp(playerView.station, "Łódź") == 0);
  assert(std::strcmp(playerView.artist, "Zażółć gęślą jaźń") == 0);
  assert(std::strcmp(playerView.title, "Ścieżka źródłowa") == 0);
  std::string boundaryTitle(168, 'a');
  boundaryTitle += "ą";
  raw.artist = "";
  raw.title = boundaryTitle.c_str();
  makePlayerDisplayView(playerView, raw, "", "", false,
                        50, false, true, -50, 0, BF_UNKNOWN);
  assert(std::strlen(playerView.artist) == 168);
  assert(static_cast<unsigned char>(playerView.artist[167]) == 'a');

  SourceManagerState sources;
  BtLinkState bt{};
  DisplaySourceView view{};

  // A: boot starts on radio.
  assert(sources.active() == ActiveSource::Radio);
  assert(!sources.bluetoothPhysicallyConnected());
  // Explicit WWW selection is idempotent and cannot select an offline module.
  SourceManagerState web;
  BtLinkState webBt{};
  assert(!web.select(ActiveSource::Bluetooth, webBt).activeChanged);
  webBt.runtimeAvailable = true;
  SourceUpdate webUpdate = web.select(ActiveSource::Bluetooth, webBt);
  assert(webUpdate.activeChanged && webUpdate.reason == SourceChangeReason::Manual);
  assert(web.active() == ActiveSource::Bluetooth);
  assert(!web.select(ActiveSource::Bluetooth, webBt).activeChanged);
  webUpdate = web.select(ActiveSource::Radio, webBt);
  assert(webUpdate.activeChanged && webUpdate.reason == SourceChangeReason::Manual);
  assert(web.active() == ActiveSource::Radio);
  assert(!web.select(ActiveSource::Radio, webBt).activeChanged);
  webUpdate = web.select(ActiveSource::SOURCE_HA, webBt);
  assert(webUpdate.activeChanged && web.active() == ActiveSource::SOURCE_HA);
  web.displayView(view);
  assert(view.kind == DisplaySourceKind::HomeAssistant &&
         view.playback == DisplayPlaybackState::Stopped);
  assert(web.localPlayerOutputAllowed());
  webUpdate = web.select(ActiveSource::SOURCE_DLNA, webBt);
  assert(webUpdate.activeChanged && web.active() == ActiveSource::SOURCE_DLNA);
  assert(web.localPlayerOutputAllowed());
  webUpdate = web.select(ActiveSource::Aux, webBt);
  assert(webUpdate.activeChanged && web.active() == ActiveSource::Aux);
  web.displayView(view);
  assert(view.kind == DisplaySourceKind::Aux && !web.localPlayerOutputAllowed());
  webUpdate = web.select(ActiveSource::SOURCE_LOCAL, webBt);
  assert(webUpdate.activeChanged && web.active() == ActiveSource::SOURCE_LOCAL);
  web.displayView(view);
  assert(view.kind == DisplaySourceKind::Local && view.connected &&
         view.playback == DisplayPlaybackState::Stopped &&
         std::strcmp(view.peerName, "SD / Local") == 0 &&
         web.localPlayerOutputAllowed());
  assert(web.select(ActiveSource::Radio, webBt).activeChanged);
  assert(web.localPlayerOutputAllowed());
  webBt.connected = true;
  assert(web.observe(webBt).reason == SourceChangeReason::BtConnect);
  assert(web.select(ActiveSource::Radio, webBt).reason == SourceChangeReason::Manual);
  for (int i = 0; i < 10; ++i)
    assert(!web.observe(webBt).activeChanged && web.active() == ActiveSource::Radio);
  sources.displayView(view);
  assert(view.kind == DisplaySourceKind::Radio);
  assert(std::strcmp(displayPlayerStationText(view, "Radio One"), "WEB Radio") == 0);
  assert(std::strcmp(displayPlayerStationText(view, ""), "WEB Radio") == 0);
  sources.displayView(view, true);
  assert(std::strcmp(displayPlayerStationText(view, "Radio One"), "Radio One") == 0);
  assert(std::strcmp(displayPlayerStationText(view, ""), "WEB Radio") == 0);
  view.kind = DisplaySourceKind::Bluetooth;
  view.connected = true;
  view.peerName = "Telefon";
  assert(std::strcmp(displayPlayerStationText(view, "Radio One"), "Telefon") == 0);
  view.peerName = "";
  assert(std::strcmp(displayPlayerStationText(view, "Radio One"), "Bluetooth") == 0);
  view.connected = false;
  view.peerName = "Stale peer";
  assert(std::strcmp(displayPlayerStationText(view, "Radio One"), "Bluetooth") == 0);
  view.kind = DisplaySourceKind::Dlna;
  assert(std::strcmp(displayPlayerStationText(view, "Radio One"), "DLNA") == 0);
  view.kind = DisplaySourceKind::Aux;
  assert(std::strcmp(displayPlayerStationText(view, "Radio One"), "AUX") == 0);
  view.kind = DisplaySourceKind::Local;
  assert(std::strcmp(displayPlayerStationText(view, "Radio One"), "SD") == 0);
  sources.displayView(view);
  assert(std::strcmp(displaySourceLabel(view.kind), "WEB") == 0);
  assert(std::strcmp(displaySourceLabel(DisplaySourceKind::Bluetooth), "BT") == 0);
  assert(std::strcmp(displaySourceLabel(DisplaySourceKind::HomeAssistant), "HA") == 0);
  assert(std::strcmp(displaySourceLabel(DisplaySourceKind::Dlna), "DLNA") == 0);
  assert(std::strcmp(displaySourceLabel(DisplaySourceKind::Aux), "AUX") == 0);
  assert(std::strcmp(displaySourceLabel(DisplaySourceKind::Spdif), "SPDIF") == 0);
  assert(std::strcmp(displaySourceLabel(DisplaySourceKind::Local), "SD") == 0);

  // Home Assistant is a normal selected source. Radio intent is retained so
  // an explicit return_to_previous request can select and resume it.
  SourceManagerState homeAssistant;
  homeAssistant.recordRadioCommand(true, 7);
  SourceUpdate haUpdate = homeAssistant.beginHomeAssistant(
      "Home Assistant", "Drzwi są otwarte");
  assert(haUpdate.activeChanged && haUpdate.stationChanged &&
         haUpdate.titleChanged && haUpdate.audioInfoChanged);
  assert(homeAssistant.active() == ActiveSource::SOURCE_HA);
  assert(!homeAssistant.radioResumeAllowed());
  homeAssistant.displayView(view, true);
  assert(view.kind == DisplaySourceKind::HomeAssistant);
  assert(view.playback == DisplayPlaybackState::Playing);
  assert(std::strcmp(view.title, "Home Assistant") == 0);
  assert(std::strcmp(view.artist, "Drzwi są otwarte") == 0);
  makePlayerDisplayView(playerView, view, "Radio One", "Radio metadata", false,
                        37, false, false, -55, 320, BF_MP3);
  assert(std::strcmp(playerView.station, "Home Assistant") == 0);
  assert(std::strcmp(playerView.artist, "Drzwi są otwarte") == 0);
  assert(std::strcmp(playerView.title, "Home Assistant") == 0);
  assert(std::strcmp(playerView.audioText, "N/A") == 0);
  haUpdate = homeAssistant.endHomeAssistant();
  assert(!haUpdate.activeChanged && haUpdate.titleChanged);
  assert(homeAssistant.active() == ActiveSource::SOURCE_HA);
  assert(!homeAssistant.radioResumeAllowed());
  assert(homeAssistant.select(ActiveSource::Radio, BtLinkState{}).activeChanged);
  assert(homeAssistant.radioResumeAllowed());

  SourceUpdate dlnaUpdate = homeAssistant.beginDlna("Koncert.flac", "audio/flac");
  assert(dlnaUpdate.activeChanged && homeAssistant.dlnaActive());
  assert(homeAssistant.active() == ActiveSource::SOURCE_DLNA);
  homeAssistant.displayView(view, true);
  assert(view.kind == DisplaySourceKind::Dlna);
  assert(std::strcmp(view.peerName, "DLNA") == 0);
  assert(std::strcmp(view.title, "Koncert.flac") == 0);
  assert(std::strcmp(view.artist, "audio/flac") == 0);
  makePlayerDisplayView(playerView, view, "Radio One", "Radio metadata", false,
                        37, false, false, -55, 320, BF_MP3);
  assert(std::strcmp(playerView.station, "DLNA") == 0);
  assert(std::strcmp(playerView.artist, "audio/flac") == 0);
  assert(std::strcmp(playerView.title, "Koncert.flac") == 0);
  assert(!homeAssistant.endMedia().activeChanged);
  assert(homeAssistant.active() == ActiveSource::SOURCE_DLNA);

  BtLinkState haBt{};
  haBt.runtimeAvailable = haBt.connected = true;
  haBt.playback = BtPlayback::Playing;
  SourceManagerState bluetoothThenHa;
  bluetoothThenHa.observe(haBt);
  assert(bluetoothThenHa.active() == ActiveSource::Bluetooth);
  bluetoothThenHa.beginHomeAssistant("HA", "Plik");
  assert(bluetoothThenHa.active() == ActiveSource::SOURCE_HA);
  bluetoothThenHa.endHomeAssistant();
  assert(bluetoothThenHa.active() == ActiveSource::SOURCE_HA);
  assert(!displayVolumeMuted(0));
  assert(!displayVolumeMuted(1));
  assert(displayVolumeMuted(50, true));
  assert(!displayVolumeFrameRed(27, false, false));
  assert(displayVolumeFrameRed(27, false, true));
  assert(!displayVolumeMuted(27, false));
  assert(displayVolumeFrameRed(27, true, false));
  assert(displayVolumeMuted(27, true));
  assert(!displayVolumeFrameRed(0, false, false));
  assert(!displayVolumeMuted(0, false));
  assert(!displaySlotVisible(displayLoudLabel(false)));
  assert(std::strcmp(displayLoudLabel(true), "LOUD") == 0);
  assert(displaySlotVisible(displayLoudLabel(true)));
  assert(!displaySlotVisible(displayDlnaModeLabel(DisplayDlnaMode::Unavailable)));
  assert(std::strcmp(displayDlnaModeLabel(DisplayDlnaMode::All), "ALL") == 0);
  assert(std::strcmp(displayDlnaModeLabel(DisplayDlnaMode::Random), "RND") == 0);
  assert(std::strcmp(displayDlnaModeLabel(DisplayDlnaMode::One), "ONE") == 0);
  // An update stops source selection without treating a still-connected phone
  // as a fresh connect when normal observation resumes.
  SourceManagerState updating;
  BtLinkState connected{};
  connected.runtimeAvailable = true;
  connected.connected = true;
  connected.rawVuLeft = 300;
  connected.rawVuRight = 220;
  connected.rawVuLastMs = 100;
  assert(updating.observe(connected).activeChanged);
  uint16_t vuLeft = 0, vuRight = 0;
  uint32_t vuTime = 0;
  updating.bluetoothRawVu(vuLeft, vuRight, vuTime);
  assert(vuLeft == 300 && vuRight == 220 && vuTime == 100);
  updating.recordRadioCommand(true, 7);
  assert(updating.radioPlayIntent());
  updating.stopForUpdate(connected);
  assert(updating.active() == ActiveSource::Radio);
  assert(!updating.radioPlayIntent() && !updating.radioResumeAllowed());
  assert(updating.radioStationForResume(3) == 3);
  updating.bluetoothRawVu(vuLeft, vuRight, vuTime);
  assert(vuLeft == 0 && vuRight == 0 && vuTime == 0);
  assert(!updating.observe(connected).activeChanged);
  assert(updating.active() == ActiveSource::Radio);
  sources.displayView(view);
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
  assert(!sources.bluetoothPhysicallyConnected());

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
  assert(sources.bluetoothPhysicallyConnected());
  sources.displayView(view);
  assert(view.kind == DisplaySourceKind::Bluetooth && view.connected);
  assert(std::strcmp(view.peerName, "Telefon") == 0);
  assert(std::strcmp(view.artist, "Żółć") == 0);
  assert(std::strcmp(view.title, "Utwór") == 0);
  assert(view.playback == DisplayPlaybackState::Playing);
  assert(sources.bluetoothAudioOutputAllowed());
  sources.displayView(view, false, false);
  assert(view.playback == DisplayPlaybackState::Stopped);
  sources.displayView(view);
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
  assert(btTransportAction(BtTransportInput::Previous, view) == BtTransportAction::Previous);
  assert(btTransportAction(BtTransportInput::Next, view) == BtTransportAction::Next);
  assert(btTransportAction(BtTransportInput::Toggle, view) == BtTransportAction::Pause);

  // D: manual BT -> RADIO stays RADIO while the phone remains connected.
  update = sources.cycle(bt);
  assert(update.reason == SourceChangeReason::Manual);
  assert(sources.active() == ActiveSource::Radio);
  assert(!sources.bluetoothAudioOutputAllowed());
  for (int i = 0; i < 10; ++i) {
    assert(!sources.observe(bt).activeChanged);
    assert(sources.active() == ActiveSource::Radio);
  }
  sources.displayView(view);
  assert(!view.connected && sources.bluetoothPhysicallyConnected());

  // E: disconnect while RADIO does not switch sources.
  bt.connected = false;
  bt.peerName[0] = bt.artist[0] = bt.title[0] = '\0';
  bt.playback = BtPlayback::Stopped;
  assert(!sources.observe(bt).activeChanged);
  assert(sources.active() == ActiveSource::Radio);
  assert(!sources.bluetoothPhysicallyConnected());

  // F: a later, real connect edge can select BT again.
  bt.connected = true;
  std::strcpy(bt.peerName, "Inny telefon");
  update = sources.observe(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::BtConnect);
  assert(sources.active() == ActiveSource::Bluetooth);
  assert(sources.bluetoothPhysicallyConnected());

  // Changed BT fields request only the relevant partial LCD update.
  sources.displayView(view);
  assert(view.playback == DisplayPlaybackState::Stopped);
  assert(!sources.bluetoothAudioOutputAllowed());
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
  assert(!sources.bluetoothAudioOutputAllowed());
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
  assert(!sources.bluetoothAudioOutputAllowed());
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

  std::strcpy(bt.artist, "Nowy artysta");
  update = sources.observe(bt);
  assert(update.titleChanged);
  sources.displayView(view);
  assert(std::strcmp(view.artist, "Nowy artysta") == 0 &&
         std::strcmp(view.title, "Nowy utwór") == 0);

  // Metadata clear while the phone stays connected must remove the old title.
  std::strcpy(bt.title, "");
  update = sources.observe(bt);
  assert(update.titleChanged);
  sources.displayView(view);
  assert(view.connected && std::strcmp(view.artist, "Nowy artysta") == 0 &&
         std::strcmp(view.title, "") == 0);
  std::strcpy(bt.artist, "");
  update = sources.observe(bt);
  assert(update.titleChanged);
  sources.displayView(view);
  assert(view.artist[0] == '\0' && view.title[0] == '\0');

  // I: loss of the module has priority over phone disconnect.
  bt.runtimeAvailable = false;
  update = sources.observe(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::BtOffline);
  assert(update.stationChanged && update.titleChanged);
  assert(sources.active() == ActiveSource::Radio);
  assert(!sources.bluetoothPhysicallyConnected());
  assert(!sources.cycle(bt).activeChanged);
  sources.displayView(view);
  assert(view.kind == DisplaySourceKind::Radio);

  // Runtime OFF masks a raw connection before Source Manager sees it.
  BtRuntime runtime(true);
  assert(runtime.start());
  SourceManagerState offSources;
  BtLinkState rawBt{};
  rawBt.runtimeAvailable = true;
  rawBt.connected = true;
  runtime.setEnabled(false);
  assert(!offSources.observe(runtime.effectiveLinkState(rawBt)).activeChanged);
  assert(offSources.active() == ActiveSource::Radio);
  assert(!offSources.cycle(runtime.effectiveLinkState(rawBt)).activeChanged);
  assert(offSources.active() == ActiveSource::Radio);

  runtime.setEnabled(true);
  update = offSources.observe(runtime.effectiveLinkState(rawBt));
  assert(update.activeChanged && update.reason == SourceChangeReason::BtConnect);
  assert(offSources.active() == ActiveSource::Bluetooth);
  runtime.setEnabled(false);
  update = offSources.observe(runtime.effectiveLinkState(rawBt));
  assert(update.activeChanged && update.reason == SourceChangeReason::BtOffline);
  assert(offSources.active() == ActiveSource::Radio);
  assert(!offSources.bluetoothAudioOutputAllowed());
  offSources.displayView(view);
  assert(view.kind == DisplaySourceKind::Radio &&
         view.playback == DisplayPlaybackState::Stopped);
  assert(!offSources.cycle(runtime.effectiveLinkState(rawBt)).activeChanged);
}
