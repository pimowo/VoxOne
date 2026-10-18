#ifndef VOXONE_BT_VOLUME_H
#define VOXONE_BT_VOLUME_H

#include <stdint.h>
#include <math.h>

#include "bt_link_protocol.h"

inline uint8_t btUserToAbsolute(uint8_t user) {
  if (user > 100) user = 100;
  return static_cast<uint8_t>(lroundf(127.0f * powf(user / 100.0f, 0.85f)));
}

inline uint8_t btAbsoluteToUser(uint8_t absolute) {
  if (absolute > 127) absolute = 127;
  return static_cast<uint8_t>(lroundf(100.0f * powf(absolute / 127.0f, 1.0f / 0.85f)));
}

enum class BtVolumeDecision : uint8_t {
  Ignored,
  SnapshotAccepted,
  PeerBaselineAccepted,
  PeerVolumeAccepted,
  LocalAckAccepted,
};

enum class BtVolumeSessionPhase : uint8_t {
  AwaitingSession,
  AwaitingPeerBaseline,
  BaselineEstablished,
  Active,
  Invalidated,
};

// Session-scoped policy for VOL_SESSION_1. Legacy VOLUME frames are never
// treated as session-safe input by this class.
class BtVolumeSessionPolicy {
 public:
  void awaitSession() {
    if (!active_) phase_ = BtVolumeSessionPhase::AwaitingSession;
  }

  void disconnect() {
    active_ = false;
    snapshotSeen_ = false;
    peerVolumeKnown_ = false;
    pendingCommand_ = false;
    session_ = BtVolumeSessionId{};
    lastEventSequence_ = 0;
    phase_ = BtVolumeSessionPhase::Invalidated;
  }

  BtVolumeDecision receive(const BtVolumeSessionEvent& event,
                           uint8_t currentUserVolume, bool allowPeerUpdate,
                           uint8_t& acceptedUserVolume) {
    if (event.kind == BtVolumeSessionEventKind::Begin) {
      if (active_ && sameBtVolumeSession(session_, event.session))
        return BtVolumeDecision::Ignored;
      if (knownSession_) {
        if (event.session.epoch == lastSession_.epoch) {
          if (event.session.generation < lastSession_.generation ||
              (event.session.generation == lastSession_.generation &&
               !event.fromStatus))
            return BtVolumeDecision::Ignored;
        } else if (!event.fromStatus) {
          // A different boot epoch is trusted only when recovered from a
          // complete status snapshot, never from a delayed asynchronous line.
          return BtVolumeDecision::Ignored;
        }
      }
      disconnect();
      active_ = true;
      session_ = event.session;
      lastSession_ = event.session;
      knownSession_ = true;
      phase_ = BtVolumeSessionPhase::AwaitingPeerBaseline;
      return BtVolumeDecision::Ignored;
    }
    if (event.kind == BtVolumeSessionEventKind::End) {
      if (active_ && sameBtVolumeSession(session_, event.session)) disconnect();
      return BtVolumeDecision::Ignored;
    }
    if (!active_ || !sameBtVolumeSession(session_, event.session) ||
        event.sequence == 0 || event.sequence <= lastEventSequence_)
      return BtVolumeDecision::Ignored;

    lastEventSequence_ = event.sequence;
    if (event.kind == BtVolumeSessionEventKind::Snapshot) {
      snapshotSeen_ = true;
      // A snapshot is the sink library's current local value. It establishes
      // ordering, but it is not evidence that the peer supplied a baseline or
      // that a user changed the peer's volume.
      return BtVolumeDecision::SnapshotAccepted;
    }
    if (event.kind == BtVolumeSessionEventKind::LocalAck) {
      if (pendingCommand_ && event.commandSequence == pendingCommandSequence_ &&
          event.absoluteVolume == pendingAbsolute_) {
        pendingCommand_ = false;
        peerVolumeKnown_ = true;
        peerAbsolute_ = event.absoluteVolume;
        phase_ = BtVolumeSessionPhase::Active;
        return BtVolumeDecision::LocalAckAccepted;
      }
      return BtVolumeDecision::Ignored;
    }
    if (event.kind != BtVolumeSessionEventKind::PeerVolume)
      return BtVolumeDecision::Ignored;

    // AVRCP does not identify whether the first SET_ABSOLUTE_VOLUME came from
    // connection initialization or an actual key press. Fail safe: the first
    // valid peer value in every session is only a baseline and never changes
    // MAIN userVolume. A local command remains usable while this baseline is
    // pending; its matching LOCAL_ACK makes that explicit local intent active.
    if (!peerVolumeKnown_) {
      snapshotSeen_ = true;
      peerVolumeKnown_ = true;
      peerAbsolute_ = event.absoluteVolume;
      phase_ = BtVolumeSessionPhase::BaselineEstablished;
      return BtVolumeDecision::PeerBaselineAccepted;
    }
    if (pendingCommand_) return BtVolumeDecision::Ignored;
    if (peerAbsolute_ == event.absoluteVolume) {
      if (phase_ == BtVolumeSessionPhase::BaselineEstablished)
        phase_ = BtVolumeSessionPhase::Active;
      return BtVolumeDecision::Ignored;
    }
    peerVolumeKnown_ = true;
    peerAbsolute_ = event.absoluteVolume;
    phase_ = BtVolumeSessionPhase::Active;
    acceptedUserVolume = btAbsoluteToUser(event.absoluteVolume);
    if (!allowPeerUpdate || acceptedUserVolume == currentUserVolume)
      return BtVolumeDecision::Ignored;
    return BtVolumeDecision::PeerVolumeAccepted;
  }

  bool prepareLocalCommand(uint8_t user, BtVolumeSessionCommand& command) {
    if (!active_) return false;
    ++lastCommandSequence_;
    if (lastCommandSequence_ == 0) ++lastCommandSequence_;
    command.session = session_;
    command.commandSequence = lastCommandSequence_;
    command.absoluteVolume = btUserToAbsolute(user);
    pendingCommand_ = true;
    pendingCommandSequence_ = command.commandSequence;
    pendingAbsolute_ = command.absoluteVolume;
    return true;
  }

  void localCommandFailed(uint32_t commandSequence) {
    if (pendingCommand_ && pendingCommandSequence_ == commandSequence)
      pendingCommand_ = false;
  }

  bool active() const { return active_; }
  bool snapshotSeen() const { return snapshotSeen_; }
  bool peerBaselineSeen() const { return peerVolumeKnown_; }
  bool pending() const { return pendingCommand_; }
  BtVolumeSessionId session() const { return session_; }
  BtVolumeSessionPhase phase() const { return phase_; }

 private:
  BtVolumeSessionId session_{};
  BtVolumeSessionId lastSession_{};
  uint32_t lastEventSequence_ = 0;
  uint32_t lastCommandSequence_ = 0;
  uint32_t pendingCommandSequence_ = 0;
  uint8_t peerAbsolute_ = 0;
  uint8_t pendingAbsolute_ = 0;
  bool active_ = false;
  bool knownSession_ = false;
  bool snapshotSeen_ = false;
  bool peerVolumeKnown_ = false;
  bool pendingCommand_ = false;
  BtVolumeSessionPhase phase_ = BtVolumeSessionPhase::AwaitingSession;
};

// A protocol frame is never sufficient to authorize itself. SourceManager
// controls this gate from the completed UART status handshake.
class BtVolumeSessionGate {
 public:
  void setAuthorized(bool authorized) {
    if (authorized_ == authorized) return;
    authorized_ = authorized;
    if (authorized_)
      policy_.awaitSession();
    else
      policy_.disconnect();
  }

  bool authorized() const { return authorized_; }

  BtVolumeDecision receive(const BtVolumeSessionEvent& event,
                           uint8_t currentUserVolume, bool allowPeerUpdate,
                           uint8_t& acceptedUserVolume) {
    if (!authorized_) return BtVolumeDecision::Ignored;
    return policy_.receive(event, currentUserVolume, allowPeerUpdate,
                           acceptedUserVolume);
  }

  bool prepareLocalCommand(uint8_t user, BtVolumeSessionCommand& command) {
    return authorized_ && policy_.prepareLocalCommand(user, command);
  }

  void localCommandFailed(uint32_t commandSequence) {
    policy_.localCommandFailed(commandSequence);
  }

  bool active() const { return authorized_ && policy_.active(); }
  bool snapshotSeen() const { return policy_.snapshotSeen(); }
  bool peerBaselineSeen() const { return policy_.peerBaselineSeen(); }
  bool pending() const { return policy_.pending(); }
  BtVolumeSessionId session() const { return policy_.session(); }
  BtVolumeSessionPhase phase() const { return policy_.phase(); }

 private:
  BtVolumeSessionPolicy policy_{};
  bool authorized_ = false;
};

#endif
