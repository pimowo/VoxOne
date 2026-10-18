#include "../src/core/bt_volume.h"
#include "../src/core/source_manager_state.h"

#include <cassert>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

BtVolumeSessionEvent event(BtVolumeSessionEventKind kind,
                           uint32_t epoch, uint32_t generation,
                           uint32_t sequence, uint8_t absolute,
                           uint32_t commandSequence = 0,
                           bool fromStatus = false) {
  BtVolumeSessionEvent result;
  result.kind = kind;
  result.session = {epoch, generation};
  result.sequence = sequence;
  result.commandSequence = commandSequence;
  result.absoluteVolume = absolute;
  result.fromStatus = fromStatus;
  return result;
}

struct LinkHarness {
  std::vector<std::string> sent;
  BtLinkProtocol link;
  LinkHarness() : link(&LinkHarness::send, nullptr, this) {}
  static void send(void* context, const char* command) {
    static_cast<LinkHarness*>(context)->sent.emplace_back(command);
  }
  void line(const char* value, uint32_t now) {
    for (const char* it = value; *it; ++it) link.feed(*it, now);
    link.feed('\n', now);
  }
};

}  // namespace

int main() {
  assert(btUserToAbsolute(0) == 0 && btUserToAbsolute(100) == 127);
  assert(btAbsoluteToUser(0) == 0 && btAbsoluteToUser(127) == 100);
  assert(btUserToAbsolute(255) == 127 && btAbsoluteToUser(255) == 100);
  const uint8_t expectedAbsolute[] =
      {0, 10, 18, 32, 46, 58, 70, 82, 94, 105, 116, 127};
  const uint8_t expectedUser[] =
      {0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
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
    if (distance > maxAbsoluteRoundTripError)
      maxAbsoluteRoundTripError = distance;
  }
  assert(maxAbsoluteRoundTripError <= 1);

  BtVolumeSessionPolicy policy;
  uint8_t accepted = 25;
  assert(policy.receive(event(BtVolumeSessionEventKind::Begin, 11, 1, 0, 0),
                        25, true, accepted) == BtVolumeDecision::Ignored);
  assert(policy.active() && !policy.snapshotSeen());
  assert(policy.phase() == BtVolumeSessionPhase::AwaitingPeerBaseline);
  assert(policy.receive(event(BtVolumeSessionEventKind::Snapshot, 11, 1, 1, 90),
                        25, true, accepted) ==
         BtVolumeDecision::SnapshotAccepted);
  assert(accepted == 25 && policy.snapshotSeen() &&
         !policy.peerBaselineSeen());

  assert(policy.receive(event(BtVolumeSessionEventKind::PeerVolume, 11, 1, 2, 58),
                        25, true, accepted) ==
         BtVolumeDecision::PeerBaselineAccepted);
  assert(accepted == 25 && policy.peerBaselineSeen());
  assert(policy.phase() == BtVolumeSessionPhase::BaselineEstablished);
  assert(policy.receive(event(BtVolumeSessionEventKind::PeerVolume, 11, 1, 2, 70),
                        25, true, accepted) == BtVolumeDecision::Ignored);
  assert(policy.receive(event(BtVolumeSessionEventKind::PeerVolume, 11, 1, 3, 58),
                        25, true, accepted) == BtVolumeDecision::Ignored);
  assert(policy.phase() == BtVolumeSessionPhase::Active);

  BtVolumeSessionCommand command;
  assert(policy.prepareLocalCommand(28, command));
  assert(command.session.epoch == 11 && command.session.generation == 1);
  assert(command.commandSequence != 0 &&
         command.absoluteVolume == btUserToAbsolute(28));
  assert(policy.pending());
  assert(policy.receive(event(BtVolumeSessionEventKind::LocalAck, 11, 1, 4,
                              command.absoluteVolume,
                              command.commandSequence + 1),
                        28, true, accepted) == BtVolumeDecision::Ignored);
  assert(policy.pending());
  assert(policy.receive(event(BtVolumeSessionEventKind::LocalAck, 11, 1, 5,
                              command.absoluteVolume,
                              command.commandSequence),
                        28, true, accepted) ==
         BtVolumeDecision::LocalAckAccepted);
  assert(!policy.pending() &&
         policy.phase() == BtVolumeSessionPhase::Active);

  assert(policy.receive(event(BtVolumeSessionEventKind::PeerVolume, 11, 1, 6, 70),
                        28, false, accepted) == BtVolumeDecision::Ignored);
  assert(accepted == btAbsoluteToUser(70));
  assert(policy.receive(event(BtVolumeSessionEventKind::PeerVolume, 11, 1, 6, 82),
                        28, true, accepted) == BtVolumeDecision::Ignored);

  assert(policy.receive(event(BtVolumeSessionEventKind::End, 11, 1, 0, 0),
                        28, true, accepted) == BtVolumeDecision::Ignored);
  assert(!policy.active() &&
         policy.phase() == BtVolumeSessionPhase::Invalidated);
  assert(policy.receive(event(BtVolumeSessionEventKind::Begin, 11, 2, 0, 0),
                        28, true, accepted) == BtVolumeDecision::Ignored);
  assert(policy.receive(event(BtVolumeSessionEventKind::Snapshot, 11, 2, 1, 70),
                        28, true, accepted) ==
         BtVolumeDecision::SnapshotAccepted);
  assert(policy.receive(event(BtVolumeSessionEventKind::PeerVolume, 11, 1, 99, 127),
                        28, true, accepted) == BtVolumeDecision::Ignored);
  assert(policy.receive(event(BtVolumeSessionEventKind::LocalAck, 11, 1, 100, 127, 7),
                        28, true, accepted) == BtVolumeDecision::Ignored);
  assert(policy.receive(event(BtVolumeSessionEventKind::End, 11, 1, 0, 0),
                        28, true, accepted) == BtVolumeDecision::Ignored);
  assert(policy.active() && policy.session().generation == 2);

  assert(policy.receive(event(BtVolumeSessionEventKind::Begin, 12, 1, 0, 0,
                              0, true),
                        28, true, accepted) == BtVolumeDecision::Ignored);
  assert(policy.session().epoch == 12 && policy.session().generation == 1);
  assert(policy.receive(event(BtVolumeSessionEventKind::Unknown, 12, 1, 1, 100),
                        28, true, accepted) == BtVolumeDecision::Ignored);
  assert(!policy.snapshotSeen());
  assert(policy.receive(event(BtVolumeSessionEventKind::PeerVolume, 12, 1, 2, 100),
                        28, true, accepted) ==
         BtVolumeDecision::PeerBaselineAccepted);
  assert(policy.snapshotSeen() && policy.peerBaselineSeen());

  // A restarted MAIN can attach to the active BT session from STATUS without
  // emitting a command or treating the current value as a peer change.
  BtVolumeSessionPolicy restartedMain;
  accepted = 31;
  assert(restartedMain.receive(
             event(BtVolumeSessionEventKind::Begin, 12, 1, 0, 0, 0, true),
             31, true, accepted) == BtVolumeDecision::Ignored);
  assert(restartedMain.receive(
             event(BtVolumeSessionEventKind::Snapshot, 12, 1, 2, 100),
             31, true, accepted) == BtVolumeDecision::SnapshotAccepted);
  assert(accepted == 31 && !restartedMain.pending());

  // UART loss invalidates local state. A fresh handshake may restore the same
  // BT-owned session; delayed pre-loss events still lose on sequence ordering.
  restartedMain.disconnect();
  assert(restartedMain.receive(
             event(BtVolumeSessionEventKind::Begin, 12, 1, 0, 0, 0, true),
             31, true, accepted) == BtVolumeDecision::Ignored);

  // Capability is a separate authority. BEGIN/SNAPSHOT/PEER cannot activate
  // the policy or call the production-equivalent Player/Config sink without it.
  BtVolumeSessionGate gate;
  unsigned playerSetUserVolumeCalls = 0;
  unsigned configWrites = 0;
  auto applyThroughGate = [&](const BtVolumeSessionEvent& input,
                              bool btSelected) {
    uint8_t value = 31;
    const BtVolumeDecision decision =
        gate.receive(input, 31, btSelected, value);
    if (decision == BtVolumeDecision::PeerVolumeAccepted) {
      ++playerSetUserVolumeCalls;
      ++configWrites;
    }
    return decision;
  };
  gate.setAuthorized(false);
  assert(applyThroughGate(
             event(BtVolumeSessionEventKind::Begin, 20, 1, 0, 0), true) ==
         BtVolumeDecision::Ignored);
  assert(applyThroughGate(
             event(BtVolumeSessionEventKind::Snapshot, 20, 1, 1, 40), true) ==
         BtVolumeDecision::Ignored);
  assert(applyThroughGate(
             event(BtVolumeSessionEventKind::PeerVolume, 20, 1, 2, 80), true) ==
         BtVolumeDecision::Ignored);
  assert(!gate.active() && playerSetUserVolumeCalls == 0 && configWrites == 0);

  gate.setAuthorized(true);
  assert(gate.phase() == BtVolumeSessionPhase::AwaitingSession);
  assert(applyThroughGate(
             event(BtVolumeSessionEventKind::Begin, 20, 1, 0, 0, 0, true),
             true) == BtVolumeDecision::Ignored);
  assert(applyThroughGate(
             event(BtVolumeSessionEventKind::Snapshot, 20, 1, 1, 40, 0, true),
             true) == BtVolumeDecision::SnapshotAccepted);
  assert(playerSetUserVolumeCalls == 0 && configWrites == 0);
  assert(applyThroughGate(
             event(BtVolumeSessionEventKind::PeerVolume, 20, 1, 2, 60), false) ==
         BtVolumeDecision::PeerBaselineAccepted);
  assert(playerSetUserVolumeCalls == 0 && configWrites == 0);
  assert(applyThroughGate(
             event(BtVolumeSessionEventKind::PeerVolume, 20, 1, 3, 80), true) ==
         BtVolumeDecision::PeerVolumeAccepted);
  assert(playerSetUserVolumeCalls == 1 && configWrites == 1);
  BtVolumeSessionCommand gatedCommand;
  assert(gate.prepareLocalCommand(35, gatedCommand));
  assert(gatedCommand.session.epoch == 20 &&
         gatedCommand.session.generation == 1 &&
         gatedCommand.absoluteVolume == btUserToAbsolute(35));
  assert(applyThroughGate(
             event(BtVolumeSessionEventKind::LocalAck, 20, 1, 4,
                   gatedCommand.absoluteVolume,
                   gatedCommand.commandSequence),
             true) == BtVolumeDecision::LocalAckAccepted);
  assert(playerSetUserVolumeCalls == 1 && configWrites == 1 && !gate.pending());
  assert(applyThroughGate(
             event(BtVolumeSessionEventKind::LocalAck, 20, 1, 4,
                   gatedCommand.absoluteVolume,
                   gatedCommand.commandSequence),
             true) == BtVolumeDecision::Ignored);
  gate.setAuthorized(false);  // Capability/runtime/A2DP trust loss.
  assert(applyThroughGate(
             event(BtVolumeSessionEventKind::Begin, 20, 1, 0, 0), true) ==
         BtVolumeDecision::Ignored);
  assert(applyThroughGate(
             event(BtVolumeSessionEventKind::PeerVolume, 20, 1, 5, 90), true) ==
         BtVolumeDecision::Ignored);
  assert(!gate.active() && playerSetUserVolumeCalls == 1 && configWrites == 1);

  // Physical HIL regression: the sink starts at zero, then LG supplies its
  // automatic Absolute Volume during connection setup. Neither value is a
  // user action, so MAIN keeps its configured volume and performs no write.
  BtVolumeSessionGate initialization;
  initialization.setAuthorized(true);
  unsigned initializationPlayerCalls = 0;
  unsigned initializationConfigWrites = 0;
  uint8_t initializationUser = 20;
  auto applyInitialization = [&](const BtVolumeSessionEvent& input) {
    uint8_t value = initializationUser;
    const BtVolumeDecision decision = initialization.receive(
        input, initializationUser, true, value);
    if (decision == BtVolumeDecision::PeerVolumeAccepted) {
      initializationUser = value;
      ++initializationPlayerCalls;
      ++initializationConfigWrites;
    }
    return decision;
  };
  assert(applyInitialization(
             event(BtVolumeSessionEventKind::Begin, 30, 1, 0, 0)) ==
         BtVolumeDecision::Ignored);
  assert(applyInitialization(
             event(BtVolumeSessionEventKind::Snapshot, 30, 1, 1, 0)) ==
         BtVolumeDecision::SnapshotAccepted);
  assert(applyInitialization(
             event(BtVolumeSessionEventKind::PeerVolume, 30, 1, 2, 25)) ==
         BtVolumeDecision::PeerBaselineAccepted);
  assert(initializationUser == 20 && initializationPlayerCalls == 0 &&
         initializationConfigWrites == 0);
  assert(initialization.phase() ==
         BtVolumeSessionPhase::BaselineEstablished);

  // Once the baseline is known, a later distinct peer value can represent a
  // remote adjustment. AVRCP cannot prove its physical origin, but it is no
  // longer the ambiguous first value of the session.
  assert(applyInitialization(
             event(BtVolumeSessionEventKind::PeerVolume, 30, 1, 3, 21)) ==
         BtVolumeDecision::PeerVolumeAccepted);
  assert(initializationUser == btAbsoluteToUser(21) &&
         initializationPlayerCalls == 1 && initializationConfigWrites == 1);
  assert(initialization.phase() == BtVolumeSessionPhase::Active);
  assert(applyInitialization(
             event(BtVolumeSessionEventKind::PeerVolume, 30, 1, 3, 80)) ==
         BtVolumeDecision::Ignored);  // duplicate sequence
  assert(initializationPlayerCalls == 1 && initializationConfigWrites == 1);

  // A snapshot that already contains a non-zero sink value is still only a
  // local library snapshot. With no peer callback it cannot alter MAIN.
  BtVolumeSessionGate snapshotOnly;
  snapshotOnly.setAuthorized(true);
  uint8_t snapshotUser = 20;
  assert(snapshotOnly.receive(
             event(BtVolumeSessionEventKind::Begin, 31, 1, 0, 0),
             snapshotUser, true, snapshotUser) == BtVolumeDecision::Ignored);
  assert(snapshotOnly.receive(
             event(BtVolumeSessionEventKind::Snapshot, 31, 1, 1, 25),
             snapshotUser, true, snapshotUser) ==
         BtVolumeDecision::SnapshotAccepted);
  assert(snapshotUser == 20 && !snapshotOnly.peerBaselineSeen() &&
         snapshotOnly.phase() ==
             BtVolumeSessionPhase::AwaitingPeerBaseline);

  // An explicit local action remains available before peer baseline. The
  // matching LOCAL_ACK proves only local VoxOneBT application, but safely
  // activates the session because the originating user intent is known.
  BtVolumeSessionCommand earlyLocal;
  assert(snapshotOnly.prepareLocalCommand(24, earlyLocal));
  assert(earlyLocal.absoluteVolume == btUserToAbsolute(24));
  assert(snapshotOnly.receive(
             event(BtVolumeSessionEventKind::PeerVolume, 31, 1, 2, 25),
             24, true, snapshotUser) ==
         BtVolumeDecision::PeerBaselineAccepted);
  assert(snapshotUser == 20);
  assert(snapshotOnly.receive(
             event(BtVolumeSessionEventKind::LocalAck, 31, 1, 3,
                   earlyLocal.absoluteVolume, earlyLocal.commandSequence),
             24, true, snapshotUser) ==
         BtVolumeDecision::LocalAckAccepted);
  assert(snapshotOnly.phase() == BtVolumeSessionPhase::Active &&
         !snapshotOnly.pending());

  // Reconnect creates a new baseline boundary. Delayed callbacks from the old
  // generation cannot activate or mutate the new session.
  assert(initialization.receive(
             event(BtVolumeSessionEventKind::End, 30, 1, 0, 0),
             initializationUser, true, accepted) == BtVolumeDecision::Ignored);
  assert(initialization.receive(
             event(BtVolumeSessionEventKind::Begin, 30, 2, 0, 0),
             initializationUser, true, accepted) == BtVolumeDecision::Ignored);
  assert(initialization.receive(
             event(BtVolumeSessionEventKind::Snapshot, 30, 2, 1, 21),
             initializationUser, true, accepted) ==
         BtVolumeDecision::SnapshotAccepted);
  assert(initialization.receive(
             event(BtVolumeSessionEventKind::PeerVolume, 30, 1, 99, 127),
             initializationUser, true, accepted) == BtVolumeDecision::Ignored);
  assert(initialization.phase() ==
         BtVolumeSessionPhase::AwaitingPeerBaseline);

  assert(restartedMain.receive(
             event(BtVolumeSessionEventKind::Snapshot, 12, 1, 4, 100),
             31, true, accepted) == BtVolumeDecision::SnapshotAccepted);
  assert(restartedMain.receive(
             event(BtVolumeSessionEventKind::PeerVolume, 12, 1, 3, 127),
             31, true, accepted) == BtVolumeDecision::Ignored);

  assert(restartedMain.receive(
             event(BtVolumeSessionEventKind::Begin, 12, 2, 0, 0),
             31, true, accepted) == BtVolumeDecision::Ignored);
  assert(restartedMain.receive(
             event(BtVolumeSessionEventKind::Snapshot, 12, 2, 1, 100),
             31, true, accepted) == BtVolumeDecision::SnapshotAccepted);
  // A delayed BEGIN cannot roll the policy back to an older generation or
  // boot epoch; consequently its following PEER frame cannot touch Config.
  assert(restartedMain.receive(
             event(BtVolumeSessionEventKind::Begin, 12, 1, 0, 0),
             31, true, accepted) == BtVolumeDecision::Ignored);
  assert(restartedMain.session().generation == 2);
  assert(restartedMain.receive(
             event(BtVolumeSessionEventKind::Begin, 11, 99, 0, 0),
             31, true, accepted) == BtVolumeDecision::Ignored);
  assert(restartedMain.session().epoch == 12);
  assert(restartedMain.receive(
             event(BtVolumeSessionEventKind::PeerVolume, 12, 1, 99, 127),
             31, true, accepted) == BtVolumeDecision::Ignored);

  LinkHarness legacy;
  legacy.link.begin(0);
  legacy.line("PROTO 2", 1);
  legacy.line("STATUS_BEGIN", 2);
  legacy.line("CAPS A2DP AVRCP ABSVOL", 3);
  legacy.line("CONNECTED", 4);
  legacy.line("VOLUME 100", 5);
  legacy.line("STATUS_END", 6);
  SourceManagerState source;
  const SourceUpdate connected = source.observe(legacy.link.state());
  assert(connected.btConnected && !connected.volumeCallback);
  assert(!legacy.link.volumeSessionCapable());
  assert(legacy.sent.size() == 2);
  legacy.line("VOLUME 90", 7);
  assert(source.observe(legacy.link.state()).volumeCallback);
  assert(legacy.sent.size() == 2);
  assert(legacy.link.setVolume(btUserToAbsolute(30)));
  assert(legacy.sent.size() == 3 && legacy.sent.back() == "SET_VOLUME 46");

  // Exercise the real UART writer for an explicit local action before peer
  // baseline. Connection setup itself emits no SET_VOLUME command.
  LinkHarness sessionWriter;
  sessionWriter.link.begin(0);
  sessionWriter.line("STATUS_BEGIN", 1);
  sessionWriter.line("PROTO 2", 2);
  sessionWriter.line("FW_VERSION 0.6.7-dev", 3);
  sessionWriter.line("CAPS A2DP AVRCP ABSVOL VOL_SESSION_1", 4);
  sessionWriter.line("OTA_STATE NOT_PENDING", 5);
  sessionWriter.line("CONNECTED", 6);
  sessionWriter.line("VOL_SESSION_BEGIN 70 1", 7);
  sessionWriter.line("VOL_SNAPSHOT 70 1 1 0", 8);
  sessionWriter.line("STATUS_END", 9);
  assert(sessionWriter.link.volumeSessionCapable());
  assert(sessionWriter.sent.size() == 2);  // GET_STATUS, GET_DIAG only.
  BtVolumeSessionGate writerGate;
  writerGate.setAuthorized(true);
  BtVolumeSessionEvent writerEvent;
  while (sessionWriter.link.takeVolumeSessionEvent(writerEvent)) {
    uint8_t ignoredUser = 20;
    writerGate.receive(writerEvent, 20, true, ignoredUser);
  }
  BtVolumeSessionCommand writerCommand;
  assert(writerGate.prepareLocalCommand(21, writerCommand));
  assert(sessionWriter.link.setSessionVolume(writerCommand));
  assert(sessionWriter.sent.back() ==
         "SET_VOLUME_SESSION 70 1 1 34");
  for (const std::string& sent : sessionWriter.sent)
    assert(sent.rfind("SET_VOLUME ", 0) != 0);

  const std::string manager = readFile("src/core/source_manager.cpp");
  const std::string commandHandler = readFile("src/core/commandhandler.cpp");
  const std::string temporary = readFile("src/core/temporary_audio_state.h");
  assert(manager.find("retryDue(") == std::string::npos);
  assert(manager.find("needsUserCommand(") == std::string::npos);
  assert(manager.find("if (update.btConnected)") == std::string::npos);
  assert(manager.find("sourceManagerSetBluetoothVolume(") != std::string::npos);
  assert(commandHandler.find("sourceManagerSetBluetoothVolume(") !=
         std::string::npos);
  assert(manager.find("setMuted(") == std::string::npos);
  assert(temporary.find("volume") == std::string::npos);
}
