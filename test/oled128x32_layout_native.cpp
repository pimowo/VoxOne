#include "../src/displays/oled128x32_layout.h"
#include "../src/displays/oled128x32_player.h"
#include "../src/displays/conf/player128x32conf.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

using namespace voxone::oled128x32;

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void assertVolume(uint8_t value, bool muted, const char* expected) {
  char text[5]{};
  formatVolume(text, sizeof(text), value, muted);
  assert(std::strcmp(text, expected) == 0);
  assert(playerVolumeContentLeft(displayVolume(value), muted) >=
         kPlayerVolume.left);
  assert(playerVolumeContentLeft(displayVolume(value), muted) +
             playerVolumeContentWidth(displayVolume(value), muted) <=
         kPlayerVolume.left + kPlayerVolume.width);
}

void assertVolumeDialog(uint8_t value, bool muted, const char* expected) {
  char text[5]{};
  formatVolumePercent(text, sizeof(text), value, muted);
  assert(std::strcmp(text, expected) == 0);
  assert(std::strlen(text) <= kVolumePrimaryMaxCharacters);
  assert(std::strlen(text) * kVolumePrimaryGlyphWidth <=
         kVolumePrimary.width);
}

void assertSource(const char* label, const char* expected) {
  char text[9]{};
  formatSource(text, sizeof(text), label);
  assert(std::strcmp(text, expected) == 0);
  assert(std::strlen(text) <= kPlayerSourceMaxCharacters);
  assert(std::strlen(text) * kPlayerGlyphWidth <= kPlayerSource.width);
}
}  // namespace

int main() {
  assert(kWidth == 128 && kHeight == 32 && kRowHeight == 8);
  assert(fits(kPlayerStation));
  assert(fits(kPlayerStatus));
  assert(kPlayerStation.top == 0 && kPlayerStation.height == 24);
  assert(kPlayerStatus.top == 24 && kPlayerStatus.height == 8);
  assert(player128x32::station.widget.textsize == 2);
  assert(player128x32::station.widget.top == 0);
  assert(player128x32::station.width == 128);
  assert(player128x32::station.startscrolldelay == 5000);

  assert(contains(kPlayerStatus, kPlayerPlayback));
  assert(contains(kPlayerStatus, kPlayerSource));
  assert(contains(kPlayerStatus, kPlayerVolume));
  assert(contains(kPlayerStatus, kPlayerWifi));
  assert(kPlayerPlayback.left + kPlayerPlayback.width <= kPlayerSource.left);
  assert(kPlayerSource.left + kPlayerSource.width <= kPlayerVolume.left);
  assert(kPlayerVolume.left + kPlayerVolume.width <= kPlayerWifi.left);
  assert(kPlayerPlayback.left == 0 && kPlayerPlayback.width == 10);
  assert(kPlayerSource.left == 10 && kPlayerSource.width == 42);
  assert(kPlayerVolume.left == 52 && kPlayerVolume.width == 56);
  assert(kPlayerWifi.left == 108 && kPlayerWifi.width == 20);
  assert(playbackIcon(true) == PlaybackIcon::Play);
  assert(playbackIcon(false) == PlaybackIcon::Stop);
  assert(contains(kPlayerPlayback, kPlayerPlay));
  assert(contains(kPlayerPlayback, kPlayerStop));
  assert(contains(kPlayerWifi, kPlayerWifiDisconnected));
  for (uint8_t i = 0; i < 4; ++i)
    assert(contains(kPlayerWifi, playerWifiBar(i)));

  assertVolume(0, false, "0");
  assertVolume(1, false, "1");
  assertVolume(20, false, "20");
  assertVolume(99, false, "99");
  assertVolume(100, false, "100");
  assertVolume(20, true, "MUTE");
  assertVolume(255, false, "100");
  assertVolumeDialog(0, false, "0%");
  assertVolumeDialog(20, false, "20%");
  assertVolumeDialog(99, false, "99%");
  assertVolumeDialog(100, false, "100%");
  assertVolumeDialog(20, true, "MUTE");

  assertSource("WEB", "[WEB]");
  assertSource("BT", "[BT]");
  assertSource("DLNA", "[DLNA]");
  assertSource("AUX", "[AUX]");
  assertSource("SPDIF", "[SPDIF]");
  assertSource("TTS", "[TTS]");

  assert(wifiBars(false, -40) == 0);
  assert(wifiBars(false, -90) == 0);
  assert(wifiBars(true, -49) == 4);
  assert(wifiBars(true, -50) == 4);
  assert(wifiBars(true, -60) == 3);
  assert(wifiBars(true, -70) == 2);
  assert(wifiBars(true, -80) == 1);
  assert(wifiBars(true, -81) == 0);
  assert(wifiBars(true, 0) == 0);

  // START remains frozen; STATIONS and VOL keep their existing fonts.
  assert(startup128x32::title.top == 0 &&
         startup128x32::progress.top == 9 &&
         startup128x32::ssid.top == 24);
  assert(stations128x32::selected.widget.textsize == 2);
  assert(stations128x32::selected.widget.top == 0);
  assert(stations128x32::header.top == 0 &&
         stations128x32::playing.top == 24 &&
         stations128x32::counter.top == 24);
  assert(fits(kVolumePrimary));
  assert(fits(kVolumeFooter));
  assert(player128x32::volumeNumber.textsize == 2 &&
         player128x32::volumeNumber.top == 0 &&
         player128x32::volumeIp.top == 24);
  assert(!fits(Bounds{0, 25, 128, 8}));
  assert(!fits(Bounds{121, 0, 8, 8}));

  const std::string display = readFile("src/core/display.cpp");
  const size_t compactBegin = display.find("class Oled128x32PlayerStatusWidget");
  const size_t compactEnd = display.find("class Oled128x64PlayerStatusWidget");
  assert(compactBegin != std::string::npos && compactEnd > compactBegin);
  const std::string compact = display.substr(compactBegin,
                                             compactEnd - compactBegin);
  assert(compact.find("fillTriangle") != std::string::npos);
  assert(compact.find("fillRect") != std::string::npos);
  assert(compact.find("formatVolume") != std::string::npos);
  assert(compact.find("formatVolumePercent") == std::string::npos);
  assert(compact.find("formatSource") != std::string::npos);
  assert(compact.find("displaySourceLabel(view.source)") != std::string::npos);
  assert(compact.find("source_ == view.source") != std::string::npos);
  assert(compact.find("wifiBars") != std::string::npos);
  assert(compact.find("network.status == CONNECTED") != std::string::npos);
  assert(compact.find("view.playback == DisplayPlaybackState::Playing") !=
         std::string::npos);
  assert(compact.find("WiFi.RSSI()") == std::string::npos);
  assert(compact.find("dsp.print(\"[]\")") == std::string::npos);
  assert(compact.find("\"V%u\"") == std::string::npos);
  assert(display.find("player128x32::artist") == std::string::npos);
  assert(display.find("player128x32::hiddenMetadata") != std::string::npos);
  assert(display.find("if (!smallOledActive() || !oled128x32Active())") !=
         std::string::npos);
  assert(display.find("player128x32::title") == std::string::npos);
  assert(display.find("new ScrollWidget(\"*\", player128x64::title") !=
         std::string::npos);

  const size_t volumeMode = display.find("if (newmode == VOL)");
  const size_t lostMode = display.find("if (newmode == LOST)", volumeMode);
  assert(volumeMode != std::string::npos && lostMode > volumeMode);
  const std::string compactVolume = display.substr(volumeMode,
                                                    lostMode - volumeMode);
  assert(compactVolume.find("formatVolumePercent") != std::string::npos);
  assert(compactVolume.find("_showDialog(level)") != std::string::npos);
  assert(compactVolume.find("_nums->setText(\"\")") != std::string::npos);

  assert(display.find(
      "if (!oled128x32Active()) _plheader->setText(\"WEB - STACJA\")") !=
      std::string::npos);
  assert(display.find(
      "if (!oled128x32Active()) pages[PG_PLAYLIST]->addWidget(_plheader)") !=
      std::string::npos);
}
