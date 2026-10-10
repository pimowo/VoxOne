#include "options.h"
#include "player_display_view.h"
#include "config.h"
#include "player.h"
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
#include "source_manager.h"
#endif

void capturePlayerDisplayView(PlayerDisplayView& out, int rssi) {
  DisplaySourceView source{};
  if (!getDisplaySourceView || !getDisplaySourceView(source)) {
    source.kind = DisplaySourceKind::Radio;
    source.playback = player.isRunning() ? DisplayPlaybackState::Playing
                                         : DisplayPlaybackState::Stopped;
  }
  bool btConnected = false;
#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
  btConnected = bluetoothPhysicallyConnected();
#endif
  makePlayerDisplayView(out, source, config.station.name, config.station.title,
                        config.station.metadataMode == STATION_META_SWAP,
                        config.userVolume, player.isMuted(), btConnected, rssi,
                        config.station.bitrate, config.configFmt);
}
