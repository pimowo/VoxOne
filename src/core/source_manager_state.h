#ifndef VOXONE_SOURCE_MANAGER_STATE_H
#define VOXONE_SOURCE_MANAGER_STATE_H

#include <stdint.h>
#include <string.h>

#include "bt_link_protocol.h"
#include "display.h"

enum class ActiveSource : uint8_t {
  Radio,
  Bluetooth,
  SOURCE_HA,
  SOURCE_DLNA,
  Aux,
};
enum class SourceChangeReason : uint8_t {
  None, Manual, BtConnect, BtDisconnect, BtOffline
};

inline const char* displayBtMetadata(const char* value) {
  if (value == nullptr) return "";
  constexpr char placeholder[] = "Not Provided";
  size_t i = 0;
  for (; placeholder[i] != '\0' && value[i] != '\0'; ++i) {
    char actual = value[i];
    char expected = placeholder[i];
    if (actual >= 'A' && actual <= 'Z') actual += 'a' - 'A';
    if (expected >= 'A' && expected <= 'Z') expected += 'a' - 'A';
    if (actual != expected) return value;
  }
  return placeholder[i] == '\0' && value[i] == '\0' ? "" : value;
}

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

inline bool temporaryBtLossRequiresRadioStop(const SourceUpdate& update,
                                             bool temporaryBusy) {
  return temporaryBusy && update.activeChanged &&
         (update.reason == SourceChangeReason::BtDisconnect ||
          update.reason == SourceChangeReason::BtOffline);
}

struct RadioSourceActions {
  bool suspend = false;
  bool userStop = false;
  bool resume = false;
};

// Source selection and the user's radio PLAY/STOP intent. Physical playback
// remains with Player; a source change must not overwrite this intent.
class SourceManagerState {
 public:
  ActiveSource active() const {
    return temporaryActive_ ? temporarySource_ : active_;
  }
  bool radioPlayIntent() const { return radioPlayIntent_; }
  void recordRadioCommand(bool play, uint16_t station = 0) {
    radioPlayIntent_ = play;
    pendingRadioStation_ = play ? station : 0;
    pendingRadioStop_ = !play;
  }
  void radioPlayConsumed() { pendingRadioStation_ = 0; }
  void radioStopConsumed() { pendingRadioStop_ = false; }
  void playlistSelectionCleared() {
    // A committed empty playlist is not a user transport command. Clear the
    // old running intent, but never overwrite a newer accepted PLAY.
    if (pendingRadioStation_ == 0) radioPlayIntent_ = false;
  }
  uint16_t radioStationForResume(uint16_t lastStation) const {
    return pendingRadioStation_ != 0 ? pendingRadioStation_ : lastStation;
  }
  bool radioResumeAllowed() const {
    return active_ == ActiveSource::Radio && radioPlayIntent_;
  }
  SourceUpdate beginTemporary(ActiveSource source, const char* title,
                              const char* artist) {
    SourceUpdate update;
    if (source != ActiveSource::SOURCE_HA &&
        source != ActiveSource::SOURCE_DLNA) return update;
    update.activeChanged = !temporaryActive_ || temporarySource_ != source;
    update.stationChanged = update.activeChanged ||
                            strcmp(temporaryTitle_, title ? title : "") != 0;
    update.titleChanged = update.activeChanged ||
                          strcmp(temporaryArtist_, artist ? artist : "") != 0 ||
                          update.stationChanged;
    update.audioInfoChanged = update.activeChanged;
    temporaryActive_ = true;
    temporarySource_ = source;
    copyText(temporaryTitle_, sizeof(temporaryTitle_),
             title && title[0]
                 ? title
                 : (source == ActiveSource::SOURCE_DLNA ? "DLNA"
                                                        : "Home Assistant"));
    copyText(temporaryArtist_, sizeof(temporaryArtist_), artist);
    return update;
  }
  SourceUpdate beginHomeAssistant(const char* title, const char* artist) {
    return beginTemporary(ActiveSource::SOURCE_HA, title, artist);
  }
  SourceUpdate beginDlna(const char* title, const char* artist) {
    return beginTemporary(ActiveSource::SOURCE_DLNA, title, artist);
  }
  SourceUpdate endTemporary() {
    if (!temporaryActive_) return {};
    temporaryActive_ = false;
    temporarySource_ = ActiveSource::Radio;
    temporaryTitle_[0] = '\0';
    temporaryArtist_[0] = '\0';
    SourceUpdate update;
    update.activeChanged = true;
    update.stationChanged = true;
    update.titleChanged = true;
    update.audioInfoChanged = true;
    return update;
  }
  SourceUpdate endHomeAssistant() {
    return temporarySource_ == ActiveSource::SOURCE_HA ? endTemporary()
                                                       : SourceUpdate{};
  }
  bool homeAssistantActive() const {
    return temporaryActive_ && temporarySource_ == ActiveSource::SOURCE_HA;
  }
  bool dlnaActive() const {
    return temporaryActive_ && temporarySource_ == ActiveSource::SOURCE_DLNA;
  }
  RadioSourceActions radioActions(const SourceUpdate& update,
                                  bool radioPhysicallyActive) const {
    if (!update.activeChanged) return {};
    RadioSourceActions actions;
    actions.userStop = pendingRadioStop_;
    actions.suspend = radioPhysicallyActive && !actions.userStop;
    const bool enteringRadio = update.reason == SourceChangeReason::Manual ||
                               update.reason == SourceChangeReason::BtDisconnect ||
                               update.reason == SourceChangeReason::BtOffline;
    actions.resume = enteringRadio && radioResumeAllowed();
    return actions;
  }
  bool bluetoothPhysicallyConnected() const { return observedConnected_; }
  bool bluetoothPlaying() const {
    return bluetoothAudioOutputAllowed();
  }
  bool bluetoothAudioOutputAllowed() const {
    return active_ == ActiveSource::Bluetooth && connected_ &&
           playback_ == BtPlayback::Playing;
  }
  void bluetoothRawVu(uint16_t& left, uint16_t& right,
                      uint32_t& lastDataMs) const {
    left = rawVuLeft_;
    right = rawVuRight_;
    lastDataMs = rawVuLastMs_;
  }

  SourceUpdate cycle(const BtSourceSnapshot& bt) {
    return select(active_ == ActiveSource::Radio ? ActiveSource::Bluetooth
                                                  : ActiveSource::Radio,
                  bt);
  }

  SourceUpdate cycle(const BtLinkState& bt) {
    return cycle(makeBtSourceSnapshot(bt));
  }

  SourceUpdate select(ActiveSource source, const BtSourceSnapshot& bt) {
    if (source == ActiveSource::Bluetooth && !bt.runtimeAvailable) return {};
    if (active_ == source && !temporaryActive_) return {};
    if (active_ != source) {
      active_ = source;
      if (source == ActiveSource::Bluetooth) remember(bt);
    }
    SourceUpdate update;
    update.activeChanged = true;
    update.stationChanged = true;
    update.titleChanged = true;
    update.audioInfoChanged = true;
    update.reason = SourceChangeReason::Manual;
    return update;
  }

  SourceUpdate select(ActiveSource source, const BtLinkState& bt) {
    return select(source, makeBtSourceSnapshot(bt));
  }

  SourceUpdate observe(const BtSourceSnapshot& bt) {
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

  SourceUpdate observe(const BtLinkState& bt) {
    return observe(makeBtSourceSnapshot(bt));
  }

  void displayView(DisplaySourceView& view, bool radioPlaying = false,
                   bool localPlaybackAvailable = true) const {
    if (temporaryActive_) {
      view.kind = temporarySource_ == ActiveSource::SOURCE_DLNA
                      ? DisplaySourceKind::Dlna
                      : DisplaySourceKind::HomeAssistant;
      view.connected = true;
      view.playback = radioPlaying ? DisplayPlaybackState::Playing
                                   : DisplayPlaybackState::Stopped;
      view.peerName = temporarySource_ == ActiveSource::SOURCE_DLNA
                          ? "DLNA" : "Home Assistant";
      view.artist = temporaryArtist_;
      view.title = temporaryTitle_;
      view.sampleRate = 0;
      return;
    }
    view.kind = active_ == ActiveSource::Bluetooth
                    ? DisplaySourceKind::Bluetooth
                : active_ == ActiveSource::SOURCE_HA
                    ? DisplaySourceKind::HomeAssistant
                : active_ == ActiveSource::SOURCE_DLNA
                    ? DisplaySourceKind::Dlna
                : active_ == ActiveSource::Aux
                    ? DisplaySourceKind::Aux
                    : DisplaySourceKind::Radio;
    view.connected = active_ == ActiveSource::Bluetooth && connected_;
    if (active_ == ActiveSource::Radio) {
      view.playback = radioPlaying ? DisplayPlaybackState::Playing
                                   : DisplayPlaybackState::Stopped;
      view.peerName = "";
      view.artist = "";
      view.title = "";
    } else if (active_ == ActiveSource::SOURCE_HA) {
      view.connected = true;
      view.playback = DisplayPlaybackState::Stopped;
      view.peerName = "Home Assistant";
      view.artist = "";
      view.title = "Home Assistant";
    } else if (active_ == ActiveSource::SOURCE_DLNA) {
      view.connected = true;
      view.playback = DisplayPlaybackState::Stopped;
      view.peerName = "DLNA";
      view.artist = "";
      view.title = "";
    } else if (active_ == ActiveSource::Aux) {
      view.connected = true;
      view.playback = DisplayPlaybackState::Stopped;
      view.peerName = "AUX";
      view.artist = "";
      view.title = "";
    } else if (!connected_) {
      view.playback = DisplayPlaybackState::None;
      view.peerName = "Bluetooth";
      view.artist = "Oczekuję na połączenie...";
      view.title = "";
    } else {
      if (!localPlaybackAvailable)
        view.playback = DisplayPlaybackState::Stopped;
      else if (playback_ == BtPlayback::Playing)
        view.playback = DisplayPlaybackState::Playing;
      else if (playback_ == BtPlayback::Paused)
        view.playback = DisplayPlaybackState::Paused;
      else
        view.playback = DisplayPlaybackState::Stopped;
      view.peerName = peerName_[0] != '\0' ? peerName_ : "Bluetooth";
      view.artist = displayBtMetadata(artist_);
      view.title = displayBtMetadata(title_);
    }
    view.sampleRate = active_ == ActiveSource::Bluetooth && connected_
                          ? sampleRate_ : 0;
  }

  bool canControlBluetooth() const {
    return active_ == ActiveSource::Bluetooth && connected_;
  }

  bool localPlayerOutputAllowed() const {
    return temporaryActive_ || active_ == ActiveSource::Radio;
  }

  void stopForUpdate(const BtSourceSnapshot& bt) {
    temporaryActive_ = false;
    temporarySource_ = ActiveSource::Radio;
    temporaryTitle_[0] = '\0';
    temporaryArtist_[0] = '\0';
    active_ = ActiveSource::Radio;
    radioPlayIntent_ = false;
    pendingRadioStation_ = 0;
    pendingRadioStop_ = false;
    rawVuLeft_ = rawVuRight_ = 0;
    rawVuLastMs_ = 0;
    observedConnected_ = bt.runtimeAvailable && bt.connected;
    observedVolumeRevision_ = bt.volumeRevision;
  }

  void stopForUpdate(const BtLinkState& bt) {
    stopForUpdate(makeBtSourceSnapshot(bt));
  }

 private:
  static void copyText(char* destination, size_t capacity, const char* source) {
    if (!capacity) return;
    if (!source) source = "";
    const size_t length = strlen(source) < capacity - 1
                              ? strlen(source) : capacity - 1;
    memcpy(destination, source, length);
    destination[length] = '\0';
  }
  void remember(const BtSourceSnapshot& bt) {
    connected_ = bt.connected;
    playback_ = bt.playback;
    sampleRate_ = bt.connected ? bt.sampleRate : 0;
    memcpy(peerName_, bt.peerName, sizeof(peerName_));
    memcpy(artist_, bt.artist, sizeof(artist_));
    memcpy(title_, bt.title, sizeof(title_));
  }

  ActiveSource active_ = ActiveSource::Radio;
  bool temporaryActive_ = false;
  ActiveSource temporarySource_ = ActiveSource::Radio;
  char temporaryTitle_[128]{};
  char temporaryArtist_[128]{};
  bool radioPlayIntent_ = false;
  uint16_t pendingRadioStation_ = 0;
  bool pendingRadioStop_ = false;
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
