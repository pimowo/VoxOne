#include "../src/core/bt_volume.h"
#include "../src/core/source_manager_state.h"

#include <cassert>
#include <string>
#include <vector>

struct VolumeHarness {
  std::vector<std::string> sent;
  BtLinkProtocol link;
  SourceManagerState sources;
  BtVolumeSync sync;
  uint8_t userVolume = 25;  // Shared VoxOne volume, analogous to config.userVolume.
  unsigned volumeSaves = 0;

  VolumeHarness() : link(&VolumeHarness::send, nullptr, this) {}

  static void send(void* context, const char* command) {
    static_cast<VolumeHarness*>(context)->sent.emplace_back(command);
  }

  void line(const char* value, uint32_t now) {
    for (const char* it = value; *it; ++it) link.feed(*it, now);
    link.feed('\n', now);
  }

  SourceUpdate observe(uint32_t now) {
    const SourceUpdate update = sources.observe(link.state());
    if (update.btDisconnected) sync.disconnect();
    if (update.btConnected) {
      assert(link.setVolume(btUserToAbsolute(userVolume)));
      sync.connect(userVolume, now);
    }
    if (update.volumeCallback &&
        sync.acceptPhoneVolume(update.absoluteVolume, now)) {
      const uint8_t fromPhone = btAbsoluteToUser(update.absoluteVolume);
      if (fromPhone != userVolume) {
        userVolume = fromPhone;
        ++volumeSaves;
      }
    }
    if (sync.needsUserCommand(userVolume)) {
      assert(link.setVolume(btUserToAbsolute(userVolume)));
      sync.sentUserCommand(userVolume);
    }
    uint8_t retryAbsolute = 0;
    if (sync.retryDue(now, retryAbsolute))
      assert(link.setVolume(retryAbsolute));
    return update;
  }

  bool turnBt(int8_t delta) {
    if (!sources.canControlBluetooth()) return false;
    int next = static_cast<int>(userVolume) + delta;
    if (next < 0) next = 0;
    if (next > 100) next = 100;
    if (next == userVolume) return false;
    if (!link.setVolume(btUserToAbsolute(static_cast<uint8_t>(next)))) return false;
    userVolume = static_cast<uint8_t>(next);
    ++volumeSaves;
    sync.sentUserCommand(userVolume);
    return true;
  }
};

int main() {
  assert(btUserToAbsolute(0) == 0 && btUserToAbsolute(100) == 127);
  assert(btAbsoluteToUser(0) == 0 && btAbsoluteToUser(127) == 100);
  assert(btUserToAbsolute(255) == 127 && btAbsoluteToUser(255) == 100);
  const uint8_t expectedAbsolute[] = {0, 10, 18, 32, 46, 58, 70, 82, 94, 105, 116, 127};
  const uint8_t expectedUser[] = {0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
  for (size_t i = 0; i < sizeof(expectedUser); ++i)
    assert(btUserToAbsolute(expectedUser[i]) == expectedAbsolute[i]);

  int maxUserRoundTripError = 0;
  for (int user = 0; user <= 100; ++user) {
    const uint8_t absolute = btUserToAbsolute(static_cast<uint8_t>(user));
    if (user > 0)
      assert(absolute >= btUserToAbsolute(static_cast<uint8_t>(user - 1)));
    const int error = static_cast<int>(btAbsoluteToUser(absolute)) - user;
    const int distance = error < 0 ? -error : error;
    if (distance > maxUserRoundTripError) maxUserRoundTripError = distance;
  }
  assert(maxUserRoundTripError == 0);

  int maxAbsoluteRoundTripError = 0;
  for (int absolute = 0; absolute <= 127; ++absolute) {
    const uint8_t user = btAbsoluteToUser(static_cast<uint8_t>(absolute));
    if (absolute > 0)
      assert(user >= btAbsoluteToUser(static_cast<uint8_t>(absolute - 1)));
    const int error = static_cast<int>(btUserToAbsolute(user)) - absolute;
    const int distance = error < 0 ? -error : error;
    if (distance > maxAbsoluteRoundTripError) maxAbsoluteRoundTripError = distance;
  }
  assert(maxAbsoluteRoundTripError <= 1);

  VolumeHarness vox;
  vox.link.begin(0);
  vox.line("PROTO 2", 1);
  vox.line("STATUS_BEGIN", 2);
  vox.line("CONNECTED", 3);
  vox.line("VOLUME 100", 4);  // Initial phone value must not replace VoxOne 25.
  vox.line("STATUS_END", 5);
  const SourceUpdate connected = vox.observe(5);
  assert(connected.btConnected && connected.activeChanged);
  assert(!connected.volumeCallback && vox.userVolume == 25);
  assert(vox.sent.back() == "SET_VOLUME 39");
  assert(vox.volumeSaves == 0);

  // A late initial phone value in the next loop is not the new master.
  const size_t sentBeforeLateInitial = vox.sent.size();
  vox.line("VOLUME 100", 6);
  vox.observe(6);
  assert(vox.userVolume == 25 && vox.volumeSaves == 0);
  assert(vox.sent.size() == sentBeforeLateInitial);
  assert(vox.sync.pending());

  // Local shared volume advances on every detent without waiting for callbacks.
  assert(vox.turnBt(1) && vox.userVolume == 26);
  assert(vox.turnBt(1) && vox.userVolume == 27);
  assert(vox.turnBt(1) && vox.userVolume == 28);
  assert(vox.sent[vox.sent.size() - 3] == "SET_VOLUME 40");
  assert(vox.sent[vox.sent.size() - 2] == "SET_VOLUME 42");
  assert(vox.sent.back() == "SET_VOLUME 43");
  assert(vox.volumeSaves == 3);
  vox.observe(9);
  assert(vox.userVolume == 28 && vox.volumeSaves == 3);

  vox.line("VOLUME 43", 10);  // Confirmation of the last local target.
  vox.observe(10);
  assert(vox.userVolume == 28 && !vox.sync.pending());

  const size_t sentBeforePhoneCallback = vox.sent.size();
  vox.line("VOLUME 58", 11);  // Phone volume maps to VoxOne user 40.
  const SourceUpdate phone = vox.observe(11);
  assert(phone.volumeCallback && phone.absoluteVolume == 58);
  assert(vox.userVolume == 40 && vox.volumeSaves == 4);
  assert(vox.sent.size() == sentBeforePhoneCallback);

  // Switching to RADIO retains the same master value.
  vox.sources.cycle(vox.link.state());
  assert(vox.sources.active() == ActiveSource::Radio && vox.userVolume == 40);
  vox.userVolume = 20;  // Existing radio Player path changes shared volume.
  ++vox.volumeSaves;
  vox.observe(12);
  assert(vox.sent.back() == "SET_VOLUME 32");
  const size_t sentAfterRadioChange = vox.sent.size();
  vox.observe(13);
  assert(vox.sent.size() == sentAfterRadioChange);

  vox.line("DISCONNECTED", 14);
  vox.observe(14);
  vox.sources.cycle(vox.link.state());  // BT selected without a phone.
  assert(vox.sources.active() == ActiveSource::Bluetooth);
  const size_t sentBeforeDisconnectedTurn = vox.sent.size();
  assert(!vox.turnBt(1));
  assert(vox.userVolume == 20 && vox.sent.size() == sentBeforeDisconnectedTurn);

  vox.line("CONNECTED", 16);
  const SourceUpdate reconnected = vox.observe(16);
  assert(reconnected.btConnected && !reconnected.volumeCallback);
  assert(vox.userVolume == 20);
  assert(vox.sent.back() == "SET_VOLUME 32");

  // No confirmation: scheduled retries at +300, +800 and +1500 ms only.
  const size_t sentOnReconnect = vox.sent.size();
  vox.observe(315);
  assert(vox.sent.size() == sentOnReconnect);
  vox.observe(316);
  assert(vox.sent.size() == sentOnReconnect + 1 && vox.sent.back() == "SET_VOLUME 32");
  vox.observe(815);
  assert(vox.sent.size() == sentOnReconnect + 1);
  vox.observe(816);
  assert(vox.sent.size() == sentOnReconnect + 2 && vox.sent.back() == "SET_VOLUME 32");
  vox.observe(1515);
  assert(vox.sent.size() == sentOnReconnect + 2);
  vox.observe(1516);
  assert(vox.sent.size() == sentOnReconnect + 3 && vox.sent.back() == "SET_VOLUME 32");
  assert(vox.sync.pending());
  vox.observe(1816);  // Finite confirmation window after the fourth send.
  assert(!vox.sync.pending() && vox.sent.size() == sentOnReconnect + 3);
  vox.observe(10000);
  assert(vox.sent.size() == sentOnReconnect + 3);
  vox.line("VOLUME 58", 10001);
  vox.observe(10001);
  assert(vox.userVolume == 40 && vox.sent.size() == sentOnReconnect + 3);

  // A stale phone callback leaves pending active; a later retry can confirm it.
  VolumeHarness stale;
  stale.link.begin(0);
  stale.line("PROTO 2", 1);
  stale.line("STATUS_BEGIN", 2);
  stale.line("CONNECTED", 3);
  stale.line("STATUS_END", 5);
  stale.observe(5);
  const size_t staleInitialSend = stale.sent.size();
  stale.line("VOLUME 100", 100);
  stale.observe(100);
  assert(stale.userVolume == 25 && stale.sync.pending());
  assert(stale.sent.size() == staleInitialSend);
  stale.observe(305);
  assert(stale.sent.size() == staleInitialSend + 1);
  assert(stale.sent.back() == "SET_VOLUME 39");
  stale.line("VOLUME 39", 400);
  stale.observe(400);
  assert(!stale.sync.pending() && stale.userVolume == 25);
  stale.observe(2000);
  assert(stale.sent.size() == staleInitialSend + 1);

  // Encoder commands replace the target without resetting the retry schedule.
  VolumeHarness turns;
  turns.link.begin(0);
  turns.line("PROTO 2", 1);
  turns.line("STATUS_BEGIN", 2);
  turns.line("CONNECTED", 3);
  turns.line("STATUS_END", 5);
  turns.observe(5);
  assert(turns.turnBt(1) && turns.turnBt(1) && turns.turnBt(1));
  assert(turns.userVolume == 28);
  turns.observe(305);
  assert(turns.sent.back() == "SET_VOLUME 43");
  turns.observe(805);
  turns.observe(1505);
  const size_t turnSends = turns.sent.size();
  turns.observe(1805);
  turns.observe(10000);
  assert(turns.sent.size() == turnSends && !turns.sync.pending());

  // Disconnect cancels every remaining retry.
  VolumeHarness dropped;
  dropped.link.begin(0);
  dropped.line("PROTO 2", 1);
  dropped.line("STATUS_BEGIN", 2);
  dropped.line("CONNECTED", 3);
  dropped.line("STATUS_END", 5);
  dropped.observe(5);
  dropped.line("DISCONNECTED", 100);
  dropped.observe(100);
  assert(!dropped.sync.pending());
  const size_t sendsBeforeDisconnectWait = dropped.sent.size();
  dropped.observe(305);
  dropped.observe(805);
  dropped.observe(1505);
  assert(dropped.sent.size() == sendsBeforeDisconnectWait);
}
