#ifndef VOXONE_WEB_STATUS_VIEW_H
#define VOXONE_WEB_STATUS_VIEW_H

#include "display.h"
#include <stdint.h>
#include <string.h>

struct WebStatusView {
  const char* source;
  const char* activeSource;
  const char* name;
  const char* metadata;
  const char* artist;
  const char* title;
  const char* codec;
  const char* playback;
  const char* transport;
  uint16_t bitrate;
  uint32_t sampleRate;
  bool btConnected;
};

inline const char* webTransportState(DisplayPlaybackState playback) {
  switch (playback) {
    case DisplayPlaybackState::Playing: return "playing";
    case DisplayPlaybackState::Paused: return "paused";
    case DisplayPlaybackState::Stopped: return "stopped";
    case DisplayPlaybackState::None: return "unavailable";
  }
  return "unavailable";
}

inline WebStatusView selectWebStatusView(const DisplaySourceView& source,
                                         const char* radioStation,
                                         const char* radioMetadata,
                                         const char* radioArtist,
                                         const char* radioTitle,
                                         const char* radioCodec,
                                         uint16_t radioBitrate) {
  if (source.kind == DisplaySourceKind::Bluetooth) {
    return {"BT", "bt", source.connected ? source.peerName : "Bluetooth", "",
            source.connected ? source.artist : "",
            source.connected ? source.title : "", "",
            source.connected ? displayPlaybackLabel(source.playback) : "",
            source.connected ? webTransportState(source.playback) : "unavailable",
            0, source.connected ? source.sampleRate : 0, source.connected};
  }
  return {"WEB", "radio", radioStation, radioMetadata, radioArtist, radioTitle,
          strcmp(radioCodec, "bitrate") == 0 ? "" : radioCodec,
          displayPlaybackLabel(source.playback), webTransportState(source.playback),
          radioBitrate, 0, false};
}

#endif
