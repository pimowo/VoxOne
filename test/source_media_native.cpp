#include "../src/core/source_media_request.h"
#include "../src/core/source_media_state.h"
#include "../src/core/source_manager_state.h"

#include <cassert>
#include <cstring>

int main() {
  using voxone::source_media::RequestStorage;
  using voxone::source_media::Source;

  RequestStorage storage{};
  const char command[] =
      "http://ha.local/media.mp3?token=a=b&title=Drzwi%20otwarte"
      "&artist=Home%20Assistant&return_to_previous=true";
  assert(voxone::source_media::parseRequest(
      command, sizeof(command) - 1, storage));
  const auto request = storage.view();
  assert(std::strcmp(request.url,
                     "http://ha.local/media.mp3?token=a=b") == 0);
  assert(std::strcmp(request.title, "Drzwi otwarte") == 0);
  assert(std::strcmp(request.artist, "Home Assistant") == 0);
  assert(request.source == Source::HomeAssistant);
  assert(request.returnToPrevious);

  const char stay[] =
      "https://ha.local/media.flac&title=Album&artist=HA"
      "&return_to_previous=false";
  assert(voxone::source_media::parseRequest(stay, sizeof(stay) - 1,
                                            storage));
  assert(!storage.view().returnToPrevious);
  assert(!voxone::source_media::parseRequest(
      "ftp://ha.local/a.mp3", sizeof("ftp://ha.local/a.mp3") - 1,
      storage));

  SourceMediaState lifecycle;
  const uint32_t first = lifecycle.begin();
  assert(first != 0 && lifecycle.active() && lifecycle.busy());
  assert(lifecycle.signal(first));
  assert(lifecycle.terminal());
  assert(lifecycle.finish(first));
  assert(!lifecycle.active() && !lifecycle.busy());

  SourceManagerState sources;
  sources.recordRadioCommand(true, 3);
  auto update = sources.beginHomeAssistant("Drzwi otwarte", "HA");
  assert(update.activeChanged);
  assert(sources.active() == ActiveSource::SOURCE_HA);
  DisplaySourceView view{};
  sources.displayView(view, true);
  assert(view.kind == DisplaySourceKind::HomeAssistant);
  assert(view.playback == DisplayPlaybackState::Playing);
  assert(std::strcmp(view.title, "Drzwi otwarte") == 0);
  assert(std::strcmp(view.artist, "HA") == 0);

  update = sources.endMedia();
  assert(!update.activeChanged && update.titleChanged);
  assert(sources.active() == ActiveSource::SOURCE_HA);
  sources.displayView(view, false);
  assert(view.playback == DisplayPlaybackState::Stopped);
  assert(std::strcmp(view.title, "Drzwi otwarte") == 0);

  BtSourceSnapshot bt{};
  update = sources.select(ActiveSource::Radio, bt);
  assert(update.activeChanged);
  assert(sources.active() == ActiveSource::Radio);
  assert(sources.radioResumeAllowed());
}
