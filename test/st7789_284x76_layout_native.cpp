#include "../src/core/st7789_284x76_layout.h"
#include "../src/core/player_display_view.h"
#include "../src/core/update_bar_render_state.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

static std::string readSource(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

static bool textFits(const st7789_284x76::Rect& slot, const char* text) {
  return std::strlen(text) * 6 <= slot.width;
}

int main() {
  using namespace st7789_284x76;

  assert(width == 284 && height == 76);
  assert(dividerTop < statusTop);
  assert(inside(playback) && inside(source) && inside(volume));
  assert(inside(audio) && inside(bluetooth) && inside(wifi));
  assert(separatedHorizontally(playback, source));
  assert(separatedHorizontally(source, volume));
  assert(separatedHorizontally(volume, audio));
  assert(separatedHorizontally(audio, bluetooth));
  assert(separatedHorizontally(bluetooth, wifi));

  const DisplaySourceKind sources[] = {
      DisplaySourceKind::Radio, DisplaySourceKind::Bluetooth,
      DisplaySourceKind::Dlna, DisplaySourceKind::Aux,
      DisplaySourceKind::Spdif, DisplaySourceKind::Tts};
  for (const auto kind : sources) {
    char label[10];
    std::snprintf(label, sizeof(label), "[%s]", displaySourceLabel(kind));
    assert(textFits(source, label));
  }

  char volumeText[12];
  assert(textFits(volume, displaySt7789VolumeText(
      0, false, volumeText, sizeof(volumeText))));
  assert(textFits(volume, displaySt7789VolumeText(
      100, false, volumeText, sizeof(volumeText))));
  assert(textFits(volume, displaySt7789VolumeText(
      100, true, volumeText, sizeof(volumeText))));

  DisplaySourceView radio{};
  radio.kind = DisplaySourceKind::Radio;
  radio.playback = DisplayPlaybackState::Playing;
  char audioText[20];
  formatDisplayAudioInfo(audioText, sizeof(audioText), radio, 128, BF_MP3);
  assert(std::strcmp(audioText, "MP3 128") == 0 && textFits(audio, audioText));
  formatDisplayAudioInfo(audioText, sizeof(audioText), radio, 65535, BF_FLAC);
  assert(std::strcmp(audioText, "FLAC 65535") == 0 && textFits(audio, audioText));
  formatDisplayAudioInfo(audioText, sizeof(audioText), radio, 0, BF_UNKNOWN);
  assert(audioText[0] == '\0');

  DisplaySourceView bluetoothSource{};
  bluetoothSource.kind = DisplaySourceKind::Bluetooth;
  bluetoothSource.sampleRate = 44100;
  formatDisplayAudioInfo(audioText, sizeof(audioText), bluetoothSource, 0, BF_UNKNOWN);
  assert(std::strcmp(audioText, "44.1 kHz") == 0 && textFits(audio, audioText));
  bluetoothSource.sampleRate = 0;
  formatDisplayAudioInfo(audioText, sizeof(audioText), bluetoothSource, 0, BF_UNKNOWN);
  assert(audioText[0] == '\0');

  assert(playerWifiLevel(-49) == 4);
  assert(playerWifiLevel(-50) == 4);
  assert(playerWifiLevel(-60) == 3);
  assert(playerWifiLevel(-70) == 2);
  assert(playerWifiLevel(-80) == 1);
  assert(playerWifiLevel(-81) == 0);
  assert(playerWifiLevel(0) == 0);

  UpdateBarRenderState bar(updateBarInteriorWidth,
                           updateIndeterminateWidth,
                           updateIndeterminateStep);
  auto delta = bar.apply({false, 0});
  assert(delta.reset && delta.fillWidth == updateIndeterminateWidth);
  delta = bar.step();
  assert(delta.clearWidth == updateIndeterminateWidth &&
         delta.fillX == updateIndeterminateStep);
  delta = bar.apply({true, 0});
  assert(delta.reset && delta.fillWidth == 0);
  delta = bar.apply({true, 50});
  assert(delta.fillWidth == updateBarInteriorWidth / 2);
  delta = bar.apply({true, 100});
  assert(delta.fillX + delta.fillWidth == updateBarInteriorWidth);

  const std::string display = readSource("src/core/display.cpp");
  const std::string config = readSource("src/displays/conf/displayST7789_76conf.h");
  assert(display.find("class St7789PlayerStatusWidget") != std::string::npos);
  assert(display.find("class St7789UpdateProgressWidget") != std::string::npos);
  assert(display.find("_pager->setPage(_st7789UpdatePage, true)") != std::string::npos);
  assert(display.find("RSSI %ddBm") == std::string::npos);
  assert(display.find("_st7789Clock") == std::string::npos);
  assert(display.find("view.btConnected") != std::string::npos);
  assert(display.find("displaySourceLabel(view.source)") != std::string::npos);
  assert(config.find("playlistPlayingConf PROGMEM = { 4, 60, 1, WA_LEFT }") !=
         std::string::npos);
  // Preserve the accepted ST7789 row-scrolling timing and one-pixel step.
  assert(config.find("st7789StationConf PROGMEM = {{ TFT_FRAMEWDT, 2, 2, WA_LEFT }, 140, MAX_WIDTH, 5000, 1, 9 }") !=
         std::string::npos);
  assert(config.find("title1Conf     PROGMEM = {{ TFT_FRAMEWDT, 21, 2, WA_LEFT }, 140, MAX_WIDTH, 5000, 1, 9 }") !=
         std::string::npos);
  assert(config.find("title2Conf     PROGMEM = {{ TFT_FRAMEWDT, 40, 2, WA_LEFT }, 140, MAX_WIDTH, 5000, 1, 9 }") !=
         std::string::npos);
}
