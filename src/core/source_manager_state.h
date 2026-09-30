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
  bool audioInfoChanged = false;
  bool volumeChanged = false;
  bool btConnected = false;
  bool btDisconnected = false;
  bool volumeCallback = false;
  uint8_t absoluteVolume = 0;
  SourceChangeReason reason = SourceChangeReason::None;
};

// Logical source selection only. Radio playback and BT audio are untouched.
class SourceManagerState {
 public:
  ActiveSource active() const { return active_; }
  bool bluetoothPlaying() const {
    return active_ == ActiveSource::Bluetooth && connected_ &&
           playback_ == BtPlayback::Playing;
  }
  void bluetoothRawVu(uint16_t& left, uint16_t& right,
                      uint32_t& lastDataMs) const {
    left = rawVuLeft_;
    right = rawVuRight_;
    lastDataMs = rawVuLastMs_;
  }

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
    update.audioInfoChanged = true;
    update.reason = SourceChangeReason::Manual;
    return update;
  }

  SourceUpdate observe(const BtLinkState& bt) {
    SourceUpdate update;
    rawVuLeft_ = bt.rawVuLeft;
    rawVuRight_ = bt.rawVuRight;
    rawVuLastMs_ = bt.rawVuLastMs;
    const bool connectedNow = bt.runtimeAvailable && bt.connected;
    const bool wasConnected = observedConnected_;
    observedConnected_ = connectedNow;

    update.btConnected = !wasConnected && connectedNow;
    update.btDisconnected = wasConnected && !connectedNow;
    update.volumeCallback = wasConnected && connectedNow &&
        bt.volume >= 0 && bt.volumeRevision != observedVolumeRevision_;
    if (update.volumeCallback)
      update.absoluteVolume = static_cast<uint8_t>(bt.volume);
    observedVolumeRevision_ = bt.volumeRevision;
    update.volumeChanged = update.volumeCallback;

    if (active_ == ActiveSource::Bluetooth && !bt.runtimeAvailable) {
      active_ = ActiveSource::Radio;
      update.activeChanged = true;
      update.stationChanged = true;
      update.titleChanged = true;
      update.audioInfoChanged = true;
      update.reason = SourceChangeReason::BtOffline;
      return update;
    }

    if (!wasConnected && connectedNow && active_ == ActiveSource::Radio) {
      active_ = ActiveSource::Bluetooth;
      remember(bt);
      update.activeChanged = true;
      update.stationChanged = true;
      update.titleChanged = true;
      update.audioInfoChanged = true;
      update.reason = SourceChangeReason::BtConnect;
      return update;
    }

    if (wasConnected && !connectedNow && active_ == ActiveSource::Bluetooth) {
      active_ = ActiveSource::Radio;
      update.activeChanged = true;
      update.stationChanged = true;
      update.titleChanged = true;
      update.audioInfoChanged = true;
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
    update.audioInfoChanged = bt.sampleRate != sampleRate_;
    if (update.stationChanged || update.titleChanged || update.audioInfoChanged)
      remember(bt);
    return update;
  }

  void displayView(DisplaySourceView& view, bool radioPlaying = false) const {
    view.kind = active_ == ActiveSource::Bluetooth
                    ? DisplaySourceKind::Bluetooth : DisplaySourceKind::Radio;
    view.connected = active_ == ActiveSource::Bluetooth && connected_;
    if (active_ == ActiveSource::Radio)
      view.playback = radioPlaying ? DisplayPlaybackState::Playing
                                   : DisplayPlaybackState::Stopped;
    else if (!connected_)
      view.playback = DisplayPlaybackState::Stopped;
    else if (playback_ == BtPlayback::Playing)
      view.playback = DisplayPlaybackState::Playing;
    else if (playback_ == BtPlayback::Paused)
      view.playback = DisplayPlaybackState::Paused;
    else
      view.playback = DisplayPlaybackState::Stopped;
    view.peerName = active_ == ActiveSource::Bluetooth ? peerName_ : "";
    view.artist = active_ == ActiveSource::Bluetooth ? artist_ : "";
    view.title = active_ == ActiveSource::Bluetooth ? title_ : "";
    view.sampleRate = active_ == ActiveSource::Bluetooth && connected_
                          ? sampleRate_ : 0;
  }

  bool canControlBluetooth() const {
    return active_ == ActiveSource::Bluetooth && connected_;
  }

 private:
  void remember(const BtLinkState& bt) {
    connected_ = bt.connected;
    playback_ = bt.playback;
    sampleRate_ = bt.connected ? bt.sampleRate : 0;
    memcpy(peerName_, bt.peerName, sizeof(peerName_));
    memcpy(artist_, bt.artist, sizeof(artist_));
    memcpy(title_, bt.title, sizeof(title_));
  }

  ActiveSource active_ = ActiveSource::Radio;
  bool observedConnected_ = false;
  bool connected_ = false;
  BtPlayback playback_ = BtPlayback::Stopped;
  uint32_t sampleRate_ = 0;
  uint32_t observedVolumeRevision_ = 0;
  uint16_t rawVuLeft_ = 0;
  uint16_t rawVuRight_ = 0;
  uint32_t rawVuLastMs_ = 0;
  char peerName_[sizeof(BtLinkState::peerName)]{};
  char artist_[sizeof(BtLinkState::artist)]{};
  char title_[sizeof(BtLinkState::title)]{};
};

#endif
