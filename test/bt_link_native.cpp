#include "../src/core/bt_link_protocol.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

struct LinkHarness {
  std::vector<std::string> sent;
  std::vector<BtLinkEvent> events;
  BtLinkProtocol link;

  LinkHarness() : link(&LinkHarness::send, &LinkHarness::event, this) {}

  static void send(void* context, const char* command) {
    static_cast<LinkHarness*>(context)->sent.emplace_back(command);
  }
  static void event(void* context, BtLinkEvent value) {
    static_cast<LinkHarness*>(context)->events.push_back(value);
  }
  void line(const std::string& text, uint32_t nowMs) {
    for (char byte : text) link.feed(byte, nowMs);
    link.feed('\r', nowMs);
    link.feed('\n', nowMs);
  }
};

int main() {
  LinkHarness link;
  link.link.begin(0);
  assert(link.sent.size() == 1 && link.sent[0] == "GET_STATUS");
  assert(!link.link.state().runtimeAvailable);

  // Startup announcement cannot cause a GET_STATUS loop.
  link.line("READY", 10);
  link.line("PROTO 2", 11);
  link.line("FW_VERSION 0.6.1-dev", 12);
  link.line("BT_NAME VoxOneBT-EFF35A", 13);
  link.line("CAPS AVRCP,A2DP,VOLUME", 14);
  assert(link.sent.size() == 1);
  assert(!link.link.state().runtimeAvailable);

  // Snapshot boundaries and asynchronous events may be interleaved.
  link.line("STATUS_END", 15);
  assert(!link.link.state().runtimeAvailable);
  link.line("STATUS_BEGIN", 20);
  link.line("CONNECTED", 21);
  link.line("DEVICE Telefon Żółć", 22);
  link.line("PLAYING", 23);
  const std::string polish = "Zażółć gęślą jaźń — Łódź";
  link.line("ARTIST " + polish, 24);
  link.line("PONG", 25);
  link.line("TITLE Tytuł Śląsk", 26);
  link.line("ALBUM Płyta", 27);
  link.line("VOLUME 73", 28);
  link.line("SAMPLE_RATE 44100", 29);
  link.line("STATUS_END", 30);
  assert(link.link.state().runtimeAvailable);
  assert(link.link.state().protocolVersion == 2);
  assert(std::strcmp(link.link.state().firmwareVersion, "0.6.1-dev") == 0);
  assert(std::strcmp(link.link.state().btName, "VoxOneBT-EFF35A") == 0);
  assert(std::strcmp(link.link.state().capabilities, "AVRCP,A2DP,VOLUME") == 0);
  assert(link.link.state().connected);
  assert(link.link.state().playback == BtPlayback::Playing);
  assert(std::strcmp(link.link.state().peerName, "Telefon Żółć") == 0);
  assert(std::strcmp(link.link.state().artist, polish.c_str()) == 0);
  assert(link.link.state().volume == 73);
  assert(link.link.state().sampleRate == 44100);
  link.line("VU 12000 3000", 31);
  assert(link.link.state().rawVuLeft == 12000);
  assert(link.link.state().rawVuRight == 3000);
  assert(link.link.state().rawVuLastMs == 31);
  link.line("VU 32769 1", 32);
  link.line("VU 1 -1", 33);
  link.line("VU 1 2 3", 34);
  assert(link.link.state().rawVuLeft == 12000);
  assert(link.link.state().rawVuLastMs == 31);
  assert(link.events.size() == 1 && link.events[0] == BtLinkEvent::Online);
  assert(link.sent.size() == 2 && link.sent[1] == "GET_DIAG");
  assert(link.link.play());
  assert(link.link.pause());
  assert(link.link.next());
  assert(link.link.prev());
  assert(link.sent.size() == 6);
  assert(link.sent[2] == "PLAY" && link.sent[3] == "PAUSE");
  assert(link.sent[4] == "NEXT" && link.sent[5] == "PREV");
  assert(!link.link.setVolume(128));
  assert(link.link.setVolume(0));
  assert(link.sent.back() == "SET_VOLUME 0");
  assert(link.link.setVolume(127));
  assert(link.sent.back() == "SET_VOLUME 127");
  assert(link.link.state().volume == 73);  // Only a phone callback changes state.
  const size_t sentBeforeVolumeCallback = link.sent.size();
  const uint32_t revisionBeforeVolumeCallback = link.link.state().volumeRevision;
  link.line("VOLUME 73", 39);
  assert(link.link.state().volumeRevision == revisionBeforeVolumeCallback + 1);
  assert(link.sent.size() == sentBeforeVolumeCallback);

  link.line("PAUSED", 40);
  assert(link.link.state().playback == BtPlayback::Paused);
  assert(link.link.state().rawVuLeft == 0);
  const std::string longMetadata(192, 'A');
  link.line("ARTIST " + longMetadata, 41);
  assert(std::strcmp(link.link.state().artist, longMetadata.c_str()) == 0);
  link.line("ARTIST " + std::string(193, 'B'), 42);
  assert(std::strcmp(link.link.state().artist, longMetadata.c_str()) == 0);
  link.line("TITLE " + std::string(260, 'C'), 43);
  assert(std::strcmp(link.link.state().title, "Tytuł Śląsk") == 0);
  link.line("VOLUME -1", 44);
  link.line("SAMPLE_RATE nope", 45);
  link.line("NOT_A_MESSAGE", 46);
  assert(link.link.state().volume == 73);
  assert(link.link.state().sampleRate == 44100);
  link.line("TITLE Po przepełnieniu", 47);
  assert(std::strcmp(link.link.state().title, "Po przepełnieniu") == 0);

  link.line("DIAG_BEGIN", 50);
  link.line("RESET_REASON POWERON", 51);
  link.line("UPTIME 12345", 52);
  link.line("HEAP 67890", 53);
  link.line("MIN_HEAP 54321", 54);
  link.line("DIAG_END", 55);
  assert(link.events.back() == BtLinkEvent::Diagnostics);
  assert(std::strcmp(link.link.diagnostics().resetReason, "POWERON") == 0);
  assert(link.link.diagnostics().uptime == 12345);
  assert(link.link.diagnostics().heap == 67890);
  assert(link.link.diagnostics().minHeap == 54321);

  link.link.tick(5055);
  assert(link.sent.back() == "PING");
  link.line("PONG", 5100);
  link.link.tick(20099);
  assert(link.link.state().runtimeAvailable);
  link.link.tick(20100);
  assert(!link.link.state().runtimeAvailable);
  assert(link.events.back() == BtLinkEvent::Offline);
  assert(link.sent.back() == "GET_STATUS");
  const size_t eventCount = link.events.size();
  link.link.tick(20101);
  assert(link.events.size() == eventCount);

  link.line("READY", 20102);
  link.line("PROTO 2", 20103);
  link.line("FW_VERSION 0.6.1-dev", 20104);
  link.line("STATUS_BEGIN", 20105);
  link.line("DISCONNECTED", 20106);
  link.line("STOPPED", 20107);
  link.line("STATUS_END", 20108);
  assert(link.link.state().runtimeAvailable);
  assert(!link.link.state().connected);
  assert(link.link.state().peerName[0] == '\0');
  assert(link.link.state().artist[0] == '\0');
  assert(link.link.state().volume == -1);
  assert(link.link.state().rawVuLeft == 0);
  const size_t sentBeforeDisconnected = link.sent.size();
  assert(!link.link.play() && !link.link.pause());
  assert(!link.link.setVolume(50));
  assert(!link.link.next() && !link.link.prev());
  assert(link.sent.size() == sentBeforeDisconnected);
  assert(link.link.state().sampleRate == 0);
  assert(link.events.back() == BtLinkEvent::Online);

  link.line("CONNECTED", 20110);
  link.line("DEVICE Inny telefon", 20111);
  link.line("ARTIST Nowy utwór", 20112);
  link.line("VOLUME 27", 20113);
  link.line("DISCONNECTED", 20114);
  assert(!link.link.state().connected);
  assert(link.link.state().peerName[0] == '\0');
  assert(link.link.state().artist[0] == '\0');
  assert(link.link.state().playback == BtPlayback::Stopped);
  assert(link.link.state().volume == -1);

  // A later snapshot can span several main-loop iterations. Its cleared
  // session fields must not be consumed until STATUS_END arrives.
  LinkHarness fragmented;
  fragmented.link.begin(0);
  fragmented.line("PROTO 2", 1);
  fragmented.line("STATUS_BEGIN", 2);
  fragmented.line("CONNECTED", 3);
  fragmented.line("SAMPLE_RATE 44100", 4);
  fragmented.line("STATUS_END", 5);
  assert(fragmented.link.state().runtimeAvailable);
  assert(fragmented.link.state().connected);
  assert(!fragmented.link.hasIncompleteOnlineSnapshot());
  fragmented.line("STATUS_BEGIN", 6);
  assert(fragmented.link.hasIncompleteOnlineSnapshot());
  assert(!fragmented.link.play());
  assert(!fragmented.link.setVolume(50));
  assert(!fragmented.link.state().connected);
  assert(fragmented.link.state().sampleRate == 0);
  fragmented.line("CONNECTED", 7);
  fragmented.line("SAMPLE_RATE 44100", 8);
  assert(fragmented.link.hasIncompleteOnlineSnapshot());
  fragmented.line("STATUS_END", 9);
  assert(!fragmented.link.hasIncompleteOnlineSnapshot());
  assert(fragmented.link.state().connected);
  assert(fragmented.link.state().sampleRate == 44100);
  fragmented.link.suspend();
  assert(!fragmented.link.state().runtimeAvailable);
  assert(!fragmented.link.state().connected);
  assert(fragmented.events.back() == BtLinkEvent::Offline);
  assert(!fragmented.link.play());
  fragmented.link.begin(10);
  assert(!fragmented.link.state().runtimeAvailable);
  assert(fragmented.sent.back() == "GET_STATUS");

  // The currently checked-out VoxOneBT source is protocol v1. Its repeated
  // READY must neither mark v2 available nor flood GET_STATUS requests.
  LinkHarness v1;
  v1.link.begin(0);
  v1.line("PROTO 1", 10);
  v1.line("READY", 11);
  assert(v1.sent.size() == 1);
  assert(v1.events.size() == 1 &&
         v1.events[0] == BtLinkEvent::UnsupportedProtocol);
  v1.link.tick(2000);
  assert(v1.sent.size() == 2);
  v1.line("PROTO 1", 2001);
  v1.line("READY", 2002);
  assert(v1.sent.size() == 2);
  assert(v1.events.size() == 1);
  v1.link.tick(4000);
  assert(v1.sent.size() == 3);
  assert(!v1.link.state().runtimeAvailable);
}
