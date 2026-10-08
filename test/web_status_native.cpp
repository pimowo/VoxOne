#include "../src/core/source_manager_state.h"
#include "../src/core/web_status_view.h"
#include "../src/core/web_transport.h"
#include "../src/core/bt_transport.h"

#include <cassert>
#include <cstring>

int main() {
  SourceManagerState sources;
  DisplaySourceView display{};
  BtLinkState bt{};

  sources.displayView(display, true);
  WebStatusView status = selectWebStatusView(display, "Radio A", "Artysta - Utwór",
                                            "Artysta", "Utwór",
                                            "MP3", 320);
  assert(std::strcmp(status.source, "WEB") == 0);
  assert(std::strcmp(status.activeSource, "radio") == 0);
  assert(std::strcmp(status.transport, "playing") == 0);
  assert(std::strcmp(status.name, "Radio A") == 0);
  assert(std::strcmp(status.metadata, "Artysta - Utwór") == 0);
  assert(std::strcmp(status.artist, "Artysta") == 0);
  assert(std::strcmp(status.title, "Utwór") == 0);
  assert(std::strcmp(status.playback, "PLAY") == 0);
  assert(std::strcmp(status.codec, "MP3") == 0 && status.bitrate == 320);
  assert(webTransportAction(false, "prev") == WebTransportAction::RadioPrevious);
  assert(webTransportAction(false, "toggle") == WebTransportAction::RadioToggle);
  assert(webTransportAction(false, "next") == WebTransportAction::RadioNext);

  bt.runtimeAvailable = true;
  bt.connected = true;
  bt.playback = BtPlayback::Playing;
  bt.sampleRate = 44100;
  std::strcpy(bt.peerName, "Telefon");
  std::strcpy(bt.artist, "BT Artysta");
  std::strcpy(bt.title, "BT Utwór");
  sources.observe(bt);  // RADIO -> BT; report remote playback.
  sources.displayView(display);
  status = selectWebStatusView(display, "Radio A", "Artysta - Utwór", "Artysta", "Utwór", "MP3", 320);
  assert(std::strcmp(status.source, "BT") == 0);
  assert(std::strcmp(status.activeSource, "bt") == 0);
  assert(std::strcmp(status.transport, "playing") == 0);
  assert(std::strcmp(status.name, "Telefon") == 0);
  assert(std::strcmp(status.artist, "BT Artysta") == 0);
  assert(std::strcmp(status.title, "BT Utwór") == 0);
  assert(std::strcmp(status.playback, "PLAY") == 0);
  assert(status.sampleRate == 44100 && status.btConnected);
  assert(status.metadata[0] == '\0' && status.codec[0] == '\0' && status.bitrate == 0);
  assert(webTransportAction(true, "prev") == WebTransportAction::BluetoothPrevious);
  assert(webTransportAction(true, "toggle") == WebTransportAction::BluetoothToggle);
  assert(webTransportAction(true, "next") == WebTransportAction::BluetoothNext);
  assert(btTransportAction(BtTransportInput::Toggle, display) == BtTransportAction::Pause);
  sources.displayView(display);
  status = selectWebStatusView(display, "Radio A", "Artysta - Utwór", "Artysta", "Utwór", "MP3", 320);
  assert(std::strcmp(status.playback, "PLAY") == 0);
  assert(btTransportAction(BtTransportInput::Toggle, display) == BtTransportAction::Pause);
  bt.playback = BtPlayback::Paused;
  sources.observe(bt);
  sources.displayView(display);
  status = selectWebStatusView(display, "Radio A", "Artysta - Utwór", "Artysta", "Utwór", "MP3", 320);
  assert(std::strcmp(status.playback, "PAUZA") == 0);
  assert(std::strcmp(status.transport, "paused") == 0);

  bt.connected = false;
  sources.observe(bt);  // BT -> RADIO; current radio data returns immediately.
  sources.displayView(display, false);
  status = selectWebStatusView(display, "Radio B", "Nowe metadata", "", "", "AAC", 128);
  assert(std::strcmp(status.source, "WEB") == 0);
  assert(std::strcmp(status.name, "Radio B") == 0);
  assert(std::strcmp(status.metadata, "Nowe metadata") == 0);
  assert(status.artist[0] == '\0' && status.title[0] == '\0');
  assert(std::strcmp(status.codec, "AAC") == 0 && status.bitrate == 128);
  assert(std::strcmp(status.playback, "STOP") == 0);
  assert(std::strcmp(status.transport, "stopped") == 0);

  sources.cycle(bt);  // Manual BT selection while the phone is disconnected.
  sources.displayView(display);
  status = selectWebStatusView(display, "Radio B", "Nowe metadata", "", "", "AAC", 128);
  assert(std::strcmp(status.source, "BT") == 0 && !status.btConnected);
  assert(std::strcmp(status.name, "Bluetooth") == 0);
  assert(status.metadata[0] == '\0' && status.artist[0] == '\0' &&
         status.title[0] == '\0' && status.playback[0] == '\0');
  assert(std::strcmp(status.transport, "unavailable") == 0);
  assert(status.codec[0] == '\0' && status.bitrate == 0 && status.sampleRate == 0);
  assert(webTransportAction(true, "invalid") == WebTransportAction::None);
}
