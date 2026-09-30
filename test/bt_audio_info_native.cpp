#include "../src/core/display_audio_info.h"
#include "../src/core/source_manager_state.h"

#include <cassert>
#include <cstring>

static void expectRadio(const DisplaySourceView& view, uint16_t bitrate,
                        BitrateFormat format, const char* text) {
  const DisplayAudioInfo info = selectDisplayAudioInfo(view, bitrate, format);
  assert(!info.bluetooth);
  assert(info.radioBitrate == bitrate);
  assert(info.radioFormat == format);
  char buffer[20];
  formatDisplayAudioInfo(buffer, sizeof(buffer), view, bitrate, format);
  assert(std::strcmp(buffer, text) == 0);
}

static void expectBluetooth(const DisplaySourceView& view, const char* top,
                            const char* text) {
  const DisplayAudioInfo info = selectDisplayAudioInfo(view, 320, BF_MP3);
  assert(info.bluetooth);
  assert(std::strcmp(info.top, top) == 0);
  assert(std::strcmp(info.bottom, top[0] ? "kHz" : "") == 0);
  assert(info.radioBitrate == 0);
  char buffer[20];
  formatDisplayAudioInfo(buffer, sizeof(buffer), view, 320, BF_MP3);
  assert(std::strcmp(buffer, text) == 0);
}

int main() {
  SourceManagerState sources;
  BtLinkState bt{};
  DisplaySourceView view{};

  sources.displayView(view);
  expectRadio(view, 320, BF_MP3, "320 MP3");
  expectRadio(view, 128, BF_AAC, "128 AAC");
  expectRadio(view, 999, BF_FLAC, "999 FLAC");
  expectRadio(view, 1000, BF_FLAC, "1000 FLAC");
  expectRadio(view, 1411, BF_FLAC, "1411 FLAC");

  bt.runtimeAvailable = true;
  bt.connected = true;
  bt.playback = BtPlayback::Playing;
  bt.sampleRate = 44100;
  SourceUpdate update = sources.observe(bt);  // RADIO -> BT.
  assert(update.activeChanged && update.audioInfoChanged);
  sources.displayView(view);
  assert(view.kind == DisplaySourceKind::Bluetooth);
  assert(view.sampleRate == 44100);
  expectBluetooth(view, "44.1", "44.1 kHz");

  bt.sampleRate = 48000;
  update = sources.observe(bt);
  assert(!update.activeChanged && update.audioInfoChanged);
  sources.displayView(view);
  expectBluetooth(view, "48", "48 kHz");

  bt.sampleRate = 0;
  update = sources.observe(bt);
  assert(update.audioInfoChanged);
  sources.displayView(view);
  expectBluetooth(view, "", "");  // No stale RADIO frame.
  bt.sampleRate = 96000;
  update = sources.observe(bt);
  assert(update.audioInfoChanged);
  sources.displayView(view);
  expectBluetooth(view, "", "");  // Unsupported sample rate stays blank.

  update = sources.cycle(bt);  // BT -> RADIO while connected.
  assert(update.activeChanged && update.audioInfoChanged);
  sources.displayView(view);
  expectRadio(view, 320, BF_MP3, "320 MP3");

  update = sources.cycle(bt);  // RADIO -> BT while connected.
  assert(update.activeChanged && update.audioInfoChanged);
  sources.displayView(view);
  expectBluetooth(view, "", "");

  bt.connected = false;
  bt.sampleRate = 0;
  update = sources.observe(bt);  // BT disconnect -> RADIO.
  assert(update.activeChanged && update.audioInfoChanged);
  sources.displayView(view);
  expectRadio(view, 128, BF_AAC, "128 AAC");
}
