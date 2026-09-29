#include "source_manager.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE

#include "bt_link.h"
#include "display.h"
#include "player.h"
#include "serialcli.h"
#include "source_manager_state.h"

namespace {
SourceManagerState sourceState;
portMUX_TYPE sourceMux = portMUX_INITIALIZER_UNLOCKED;

// Only the display task writes this copy. Widgets copy its text in setText().
struct DisplayText {
  char peerName[sizeof(BtLinkState::peerName)];
  char artist[sizeof(BtLinkState::artist)];
  char title[sizeof(BtLinkState::title)];
};
DisplayText displayText{};

const char* sourceName(ActiveSource source) {
  return source == ActiveSource::Bluetooth ? "BT" : "RADIO";
}

const char* sourceReason(SourceChangeReason reason) {
  switch (reason) {
    case SourceChangeReason::Manual: return "manual";
    case SourceChangeReason::BtConnect: return "bt-connect";
    case SourceChangeReason::BtDisconnect: return "bt-disconnect";
    case SourceChangeReason::BtOffline: return "bt-offline";
    default: return "unknown";
  }
}

void refreshDisplay(const SourceUpdate& update) {
#if VOXONE_HAS_DISPLAY
  if (update.stationChanged) display.putRequest(NEWSTATION);
  if (update.titleChanged) display.putRequest(NEWTITLE);
#else
  (void)update;
#endif
}
}  // namespace

void sourceManagerBegin() {
  serialCli.printf("##[SOURCE]# active=RADIO\n");
}

void sourceManagerLoop() {
  portENTER_CRITICAL(&sourceMux);
  const SourceUpdate update = sourceState.observe(btLink.state());
  const ActiveSource active = sourceState.active();
  portEXIT_CRITICAL(&sourceMux);
  if (update.activeChanged)
    serialCli.printf("##[SOURCE]# active=%s reason=%s\n",
                     sourceName(active), sourceReason(update.reason));
#if VOXONE_HAS_DISPLAY
  if (update.activeChanged && active == ActiveSource::Radio &&
      display.mode() == BT_TRANSPORT)
    display.putRequest(NEWMODE, PLAYER);
#endif
  refreshDisplay(update);
}

bool bluetoothSourceSelected() {
  portENTER_CRITICAL(&sourceMux);
  const bool selected = sourceState.active() == ActiveSource::Bluetooth;
  portEXIT_CRITICAL(&sourceMux);
  return selected;
}

bool bluetoothTransportAvailable() {
  portENTER_CRITICAL(&sourceMux);
  const bool available = sourceState.canControlBluetooth();
  portEXIT_CRITICAL(&sourceMux);
  return available;
}

void sourceManagerTransport(BtTransportInput input) {
  DisplaySourceView source{};
  portENTER_CRITICAL(&sourceMux);
  sourceState.displayView(source);
  const BtTransportAction action = btTransportAction(input, source);
  portEXIT_CRITICAL(&sourceMux);
  switch (action) {
    case BtTransportAction::Previous: btLink.prev(); break;
    case BtTransportAction::Next: btLink.next(); break;
    case BtTransportAction::Play: btLink.play(); break;
    case BtTransportAction::Pause: btLink.pause(); break;
    case BtTransportAction::None: break;
  }
}

void cycleNextSource() {
  portENTER_CRITICAL(&sourceMux);
  const SourceUpdate update = sourceState.cycle(btLink.state());
  const ActiveSource active = sourceState.active();
  portEXIT_CRITICAL(&sourceMux);
  if (!update.activeChanged) return;
  serialCli.printf("##[SOURCE]# active=%s reason=manual\n", sourceName(active));
  refreshDisplay(update);
}

bool getDisplaySourceView(DisplaySourceView& view) {
  const bool radioPlaying = player.isRunning();
  portENTER_CRITICAL(&sourceMux);
  sourceState.displayView(view, radioPlaying);
  if (view.kind == DisplaySourceKind::Bluetooth) {
    memcpy(displayText.peerName, view.peerName, sizeof(displayText.peerName));
    memcpy(displayText.artist, view.artist, sizeof(displayText.artist));
    memcpy(displayText.title, view.title, sizeof(displayText.title));
    view.peerName = displayText.peerName;
    view.artist = displayText.artist;
    view.title = displayText.title;
  }
  portEXIT_CRITICAL(&sourceMux);
  return true;
}

#endif
