#ifndef VOXONE_SOURCE_MANAGER_STATE_H
#define VOXONE_SOURCE_MANAGER_STATE_H

#include <stdint.h>
#include <string.h>

#include "bt_link_protocol.h"
#include "display.h"

enum class ActiveSource : uint8_t { Radio, Bluetooth };
enum class SourceChangeReason : uint8_t {
  None, Manual, BtConnect, BtDisconnect, BtOffline
};

struct SourceUpdate {
  bool activeChanged = false;
  bool stationChanged = false;
  bool titleChanged = false;
  SourceChangeReason reason = SourceChangeReason::None;
};

// Logical source selection only. Radio playback and BT audio are untouched.
class SourceManagerState {
 public:
  ActiveSource active() const { return active_; }

  SourceUpdate cycle(const BtLinkState& bt) {
    SourceUpdate update;
    if (active_ == ActiveSource::Radio) {
      if (!bt.runtimeAvailable) return update;
      active_ = ActiveSource::Bluetooth;
      remember(bt);
    } else {
      active_ = ActiveSource::Radio;
    }
    update.activeChanged = true;
    update.stationChanged = true;
    update.titleChanged = true;
    update.reason = SourceChangeReason::Manual;
    return update;
  }

  SourceUpdate observe(const BtLinkState& bt) {
    SourceUpdate update;
    const bool connectedNow = bt.runtimeAvailable && bt.connected;
    const bool wasConnected = observedConnected_;
    observedConnected_ = connectedNow;

    if (active_ == ActiveSource::Bluetooth && !bt.runtimeAvailable) {
      active_ = ActiveSource::Radio;
      update.activeChanged = true;
      update.stationChanged = true;
      update.titleChanged = true;
      update.reason = SourceChangeReason::BtOffline;
      return update;
    }

    if (!wasConnected && connectedNow && active_ == ActiveSource::Radio) {
      active_ = ActiveSource::Bluetooth;
      remember(bt);
      update.activeChanged = true;
      update.stationChanged = true;
      update.titleChanged = true;
      update.reason = SourceChangeReason::BtConnect;
      return update;
    }

    if (wasConnected && !connectedNow && active_ == ActiveSource::Bluetooth) {
      active_ = ActiveSource::Radio;
      update.activeChanged = true;
      update.stationChanged = true;
      update.titleChanged = true;
      update.reason = SourceChangeReason::BtDisconnect;
      return update;
    }

    if (active_ != ActiveSource::Bluetooth) return update;
    update.stationChanged = bt.connected != connected_ ||
                            strcmp(bt.peerName, peerName_) != 0;
    update.titleChanged = bt.connected != connected_ ||
                          bt.playback != playback_ ||
                          strcmp(bt.artist, artist_) != 0 ||
                          strcmp(bt.title, title_) != 0;
    if (update.stationChanged || update.titleChanged) remember(bt);
    return update;
  }

  void displayView(DisplaySourceView& view) const {
    view.kind = active_ == ActiveSource::Bluetooth
                    ? DisplaySourceKind::Bluetooth : DisplaySourceKind::Radio;
    view.connected = active_ == ActiveSource::Bluetooth && connected_;
    view.peerName = active_ == ActiveSource::Bluetooth ? peerName_ : "";
    view.artist = active_ == ActiveSource::Bluetooth ? artist_ : "";
    view.title = active_ == ActiveSource::Bluetooth ? title_ : "";
  }

 private:
  void remember(const BtLinkState& bt) {
    connected_ = bt.connected;
    playback_ = bt.playback;
    memcpy(peerName_, bt.peerName, sizeof(peerName_));
    memcpy(artist_, bt.artist, sizeof(artist_));
    memcpy(title_, bt.title, sizeof(title_));
  }

  ActiveSource active_ = ActiveSource::Radio;
  bool observedConnected_ = false;
  bool connected_ = false;
  BtPlayback playback_ = BtPlayback::Stopped;
  char peerName_[sizeof(BtLinkState::peerName)]{};
  char artist_[sizeof(BtLinkState::artist)]{};
  char title_[sizeof(BtLinkState::title)]{};
};

#endif
