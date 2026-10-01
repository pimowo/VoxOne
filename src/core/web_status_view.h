#ifndef VOXONE_WEB_STATUS_VIEW_H
#define VOXONE_WEB_STATUS_VIEW_H

#include "display.h"
#include <stdint.h>
#include <string.h>

struct WebStatusView {
  const char* source;
  const char* name;
  const char* metadata;
  const char* artist;
  const char* title;
  const char* codec;
  const char* playback;
  uint16_t bitrate;
  uint32_t sampleRate;
  bool btConnected;
};

inline WebStatusView selectWebStatusView(const DisplaySourceView& source,
                                         const char* radioStation,
                                         const char* radioMetadata,
                                         const char* radioCodec,
                                         uint16_t radioBitrate) {
  if (source.kind == DisplaySourceKind::Bluetooth) {
    return {"BT", source.connected ? source.peerName : "Bluetooth", "",
            source.connected ? source.artist : "",
            source.connected ? source.title : "", "",
            source.connected ? displayPlaybackLabel(source.playback) : "",
            0, source.connected ? source.sampleRate : 0, source.connected};
  }
  return {"WEB", radioStation, radioMetadata, "", "",
          strcmp(radioCodec, "bitrate") == 0 ? "" : radioCodec,
          displayPlaybackLabel(source.playback), radioBitrate, 0, false};
}

#endif
