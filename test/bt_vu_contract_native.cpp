#include "../src/core/bt_link_protocol.h"
#include "../src/core/source_manager_state.h"
#include "../src/core/source_vu_state.h"
#include "../src/core/web_status_view.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

struct LinkHarness {
  std::vector<std::string> sent;
  BtLinkProtocol link;

  LinkHarness() : link(&LinkHarness::send, nullptr, this) {}

  static void send(void* context, const char* command) {
    static_cast<LinkHarness*>(context)->sent.emplace_back(command);
  }

  void line(const char* text, uint32_t nowMs) {
    for (const char* byte = text; *byte != '\0'; ++byte)
      link.feed(*byte, nowMs);
    link.feed('\r', nowMs);
    link.feed('\n', nowMs);
  }

  void connectedSnapshot(BtPlayback playback, uint32_t nowMs) {
    line("STATUS_BEGIN", nowMs++);
    line("PROTO 2", nowMs++);
    line("FW_VERSION 0.6.7-dev", nowMs++);
    line("CAPS A2DP AVRCP VU", nowMs++);
    line("OTA_STATE NOT_PENDING", nowMs++);
    line("CONNECTED", nowMs++);
    line("DEVICE VU contract phone", nowMs++);
    line("SAMPLE_RATE 48000", nowMs++);
    switch (playback) {
      case BtPlayback::Playing: line("PLAYING", nowMs++); break;
      case BtPlayback::Paused: line("PAUSED", nowMs++); break;
      case BtPlayback::Stopped: line("STOPPED", nowMs++); break;
    }
    line("STATUS_END", nowMs);
  }
};

struct WebClientView {
  std::string activeSource;
  std::string transport;
  bool btConnected = false;
  bool vuEnabled = false;
  unsigned statusRevision = 0;
  unsigned systemRevision = 0;
};

struct WebFanout {
  std::vector<WebClientView> clients;
  WebClientView current;
  unsigned statusRevision = 0;
  unsigned systemRevision = 0;

  void addClient() {
    clients.push_back(current);
  }

  void broadcastStatus(const SourceManagerState& source,
                       bool radioPlaying) {
    DisplaySourceView display{};
    source.displayView(display, radioPlaying);
    const WebStatusView status = selectWebStatusView(
        display, "Contract radio", "Radio metadata", "", "", "MP3", 192);
    current.activeSource = status.activeSource;
    current.transport = status.transport;
    current.btConnected = status.btConnected;
    current.statusRevision = ++statusRevision;
    for (WebClientView& client : clients) {
      client.activeSource = current.activeSource;
      client.transport = current.transport;
      client.btConnected = current.btConnected;
      client.statusRevision = current.statusRevision;
    }
  }

  void broadcastVuSetting(bool enabled) {
    current.vuEnabled = enabled;
    current.systemRevision = ++systemRevision;
    for (WebClientView& client : clients) {
      client.vuEnabled = enabled;
      client.systemRevision = current.systemRevision;
    }
  }

  void assertConsistent() const {
    for (const WebClientView& client : clients) {
      assert(client.activeSource == current.activeSource);
      assert(client.transport == current.transport);
      assert(client.btConnected == current.btConnected);
      assert(client.vuEnabled == current.vuEnabled);
      assert(client.statusRevision == current.statusRevision);
      assert(client.systemRevision == current.systemRevision);
    }
  }
};

bool webStatusChanged(const SourceUpdate& update) {
  return update.activeChanged || update.stationChanged || update.titleChanged ||
         update.audioInfoChanged || update.btConnected ||
         update.btDisconnected;
}

SourceVuResult currentVu(const SourceManagerState& source, bool radioPlaying,
                         uint16_t radioLevels, uint32_t nowMs) {
  uint16_t left = 0;
  uint16_t right = 0;
  uint32_t lastDataMs = 0;
  source.bluetoothRawVu(left, right, lastDataMs);
  return sourceVuSelect(
      source.active() == ActiveSource::Bluetooth, radioPlaying, radioLevels,
      source.bluetoothPlaying(), left, right, lastDataMs, nowMs, 100);
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void auditProductionWiring() {
  const std::string source = readFile("src/core/source_manager.cpp");
  assert(source.find("sourceState.bluetoothRawVu(vuLeft, vuRight, lastDataMs)") !=
         std::string::npos);
  assert(source.find("const SourceVuResult result = sourceVuSelect(") !=
         std::string::npos);
  assert(source.find("netserver.requestOnChange(WEBSTATUS, 0)") !=
         std::string::npos);

  const std::string commands = readFile("src/core/commandhandler.cpp");
  const size_t vuCommand = commands.find("strEquals(command, \"vumeter\")");
  const size_t vuBroadcast = commands.find("requestOnChange(GETSYSTEM, 0)",
                                           vuCommand);
  assert(vuCommand != std::string::npos && vuBroadcast != std::string::npos);

  const std::string server = readFile("src/core/netserver.cpp");
  assert(server.find("\"{\\\"sst\\\":%d,\\\"vu\\\":%d,\\\"canVu\\\":%d") !=
         std::string::npos);
  const size_t broadcastBranch = server.find("if (clientId == 0)");
  assert(broadcastBranch != std::string::npos);
  assert(server.find("securityWebSocketTextAll(wsBuf)", broadcastBranch) !=
         std::string::npos);

  const std::string browser = readFile("web-src/voxone.js");
  assert(browser.find("if (message.vu === 0 || message.vu === 1)") !=
         std::string::npos);
  assert(browser.find("send(\"vumeter\", requested ? 1 : 0)") !=
         std::string::npos);
}

}  // namespace

int main() {
  auditProductionWiring();

  LinkHarness link;
  SourceManagerState source;
  WebFanout web;
  link.link.begin(0);
  web.addClient();
  web.addClient();
  web.broadcastVuSetting(true);
  web.assertConsistent();

  // The real UART parser, SourceManager and VU selection path carry fresh,
  // independently scaled BT samples only while A2DP is PLAYING.
  link.connectedSnapshot(BtPlayback::Playing, 10);
  SourceUpdate update = source.observe(link.link.state());
  assert(update.reason == SourceChangeReason::BtConnect);
  assert(webStatusChanged(update));
  web.broadcastStatus(source, false);
  link.line("VU 16384 8192", 30);
  update = source.observe(link.link.state());
  assert(!webStatusChanged(update));
  SourceVuResult vu = currentVu(source, false, 0xffff, 30);
  assert(vu.playing);
  assert((vu.levels >> 8) == 85);
  assert((vu.levels & 0xff) == 65);
  assert(web.current.activeSource == "bt" &&
         web.current.transport == "playing" && web.current.btConnected);
  web.assertConsistent();

  // Repeated frames do not roll back state or produce redundant WebStatus.
  const unsigned playingRevision = web.statusRevision;
  link.line("PLAYING", 31);
  update = source.observe(link.link.state());
  assert(!webStatusChanged(update));
  link.line("VU 16384 8192", 32);
  update = source.observe(link.link.state());
  assert(!webStatusChanged(update));
  assert(web.statusRevision == playingRevision);

  // PAUSED clears the sample. A delayed VU frame is ignored and cannot be
  // resurrected by a later PLAYING frame before a genuinely fresh sample.
  link.line("PAUSED", 40);
  update = source.observe(link.link.state());
  assert(update.titleChanged && webStatusChanged(update));
  web.broadcastStatus(source, false);
  vu = currentVu(source, false, 0xffff, 40);
  assert(!vu.playing && vu.levels == 0);
  link.line("VU 30000 29000", 41);
  update = source.observe(link.link.state());
  assert(!webStatusChanged(update));
  link.line("PAUSED", 42);
  update = source.observe(link.link.state());
  assert(!webStatusChanged(update));
  link.line("PLAYING", 43);
  update = source.observe(link.link.state());
  assert(update.titleChanged && webStatusChanged(update));
  web.broadcastStatus(source, false);
  vu = currentVu(source, false, 0xffff, 43);
  assert(vu.playing && vu.levels == 0);
  link.line("VU 12000 3000", 44);
  source.observe(link.link.state());
  vu = currentVu(source, false, 0xffff, 44);
  assert(vu.playing && vu.levels != 0);

  // STOPPED follows the same invalidation contract.
  link.line("STOPPED", 50);
  update = source.observe(link.link.state());
  assert(update.titleChanged && webStatusChanged(update));
  web.broadcastStatus(source, false);
  vu = currentVu(source, false, 0xffff, 50);
  assert(!vu.playing && vu.levels == 0);
  link.line("VU 32768 32768", 51);
  source.observe(link.link.state());
  vu = currentVu(source, false, 0xffff, 51);
  assert(!vu.playing && vu.levels == 0);

  // Disconnect clears BT state and immediately selects RADIO's own levels.
  link.line("DISCONNECTED", 60);
  update = source.observe(link.link.state());
  assert(update.reason == SourceChangeReason::BtDisconnect);
  web.broadcastStatus(source, true);
  const uint16_t radioLevels = static_cast<uint16_t>((80 << 8) | 20);
  vu = currentVu(source, true, radioLevels, 60);
  assert(vu.playing);
  assert((vu.levels >> 8) == 20 && (vu.levels & 0xff) == 80);
  assert(web.current.activeSource == "radio" &&
         web.current.transport == "playing" && !web.current.btConnected);

  // Reconnect starts with blank BT VU and returns only after a fresh sample.
  link.connectedSnapshot(BtPlayback::Playing, 70);
  update = source.observe(link.link.state());
  assert(update.reason == SourceChangeReason::BtConnect);
  web.broadcastStatus(source, false);
  vu = currentVu(source, false, radioLevels, 80);
  assert(vu.playing && vu.levels == 0);
  link.line("VU 6000 14000", 81);
  source.observe(link.link.state());
  const SourceVuResult reconnected = currentVu(source, false, radioLevels, 81);
  assert(reconnected.playing && reconnected.levels != 0);

  // Manual source switches never carry the other source's meter value.
  update = source.select(ActiveSource::Radio, link.link.state());
  assert(update.reason == SourceChangeReason::Manual);
  web.broadcastStatus(source, true);
  vu = currentVu(source, true, radioLevels, 82);
  assert((vu.levels >> 8) == 20 && (vu.levels & 0xff) == 80);
  update = source.select(ActiveSource::Bluetooth, link.link.state());
  assert(update.reason == SourceChangeReason::Manual);
  web.broadcastStatus(source, false);
  vu = currentVu(source, false, radioLevels, 82);
  assert(vu.levels == reconnected.levels);

  // A newly connected browser receives the current snapshots; GETSYSTEM
  // broadcasts update every client to one revision and one VU setting.
  web.addClient();
  web.assertConsistent();
  web.broadcastVuSetting(false);
  web.assertConsistent();
  assert(web.clients.size() == 3);
  for (const WebClientView& client : web.clients) {
    assert(!client.vuEnabled);
    assert(client.activeSource == "bt");
    assert(client.transport == "playing");
  }
}
