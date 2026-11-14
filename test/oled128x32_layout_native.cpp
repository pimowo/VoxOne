#include "../src/displays/oled128x32_layout.h"
#include "../src/displays/oled128x32_player.h"
#include "../src/displays/conf/player128x32conf.h"
#include "../src/displays/fonts/voxone_boot_logo_oled_128x32.h"
#include "../src/displays/widgets/scroll_text_state.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace voxone::oled128x32;

namespace {
uint32_t crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t index = 0; index < size; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return crc ^ 0xFFFFFFFFU;
}

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
  assert(playerVolumeTextLeft(displayVolume(value), muted) >=
         kPlayerVolume.left);
  assert(playerVolumeTextLeft(displayVolume(value), muted) +
             playerVolumeTextWidth(displayVolume(value), muted) <=
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
  assert(std::strlen(text) * kGlyphWidth <= kPlayerSource.width);
}

void assertMetadata(const char* artist, const char* title,
                    const char* expected) {
  char text[kMetadataBufferSize]{};
  formatMetadata(text, sizeof(text), artist, title);
  assert(std::strcmp(text, expected) == 0);
}
}  // namespace

int main() {
  namespace branding = voxone::display_boot_branding::oled32;
  assert(kWidth == 128 && kHeight == 32 && kRowHeight == 8);
  assert(kStartupLogoBand.top == 0 && bottom(kStartupLogoBand) == 16);
  assert(kStartupProgressBand.top == 16 &&
         bottom(kStartupProgressBand) == 24);
  assert(kStartupSsidBand.top == 24 && bottom(kStartupSsidBand) == 32);
  assert(contains(kStartupLogoBand, kStartupLogo));
  assert(contains(kStartupProgressBand, kStartupProgress));
  assert(contains(kStartupSsidBand, kStartupSsid));
  assert(kStartupLogo.left == branding::kBootLogoLeft);
  assert(kStartupLogo.top == branding::kBootLogoTop);
  assert(kStartupLogo.width == branding::kBootLogoWidth);
  assert(kStartupLogo.height == branding::kBootLogoHeight);
  static_assert(branding::kBootLogoWidth == 75 &&
                    branding::kBootLogoHeight == 16 &&
                    branding::kBootLogoRowBytes == 10,
                "OLED 128x32 logo must be the proportional 1-bit wordmark");
  assert(crc32(branding::kBootLogoBitmap,
               sizeof(branding::kBootLogoBitmap)) == 0x43D1E50DU);

  // Simulate consecutive animation frames. They may update only y=16..23;
  // the bitmap logo in y=0..15 must remain byte-for-byte unchanged.
  std::vector<uint8_t> startup(kWidth * kHeight, 0);
  for (uint16_t y = 0; y < branding::kBootLogoHeight; ++y) {
    for (uint16_t x = 0; x < branding::kBootLogoWidth; ++x) {
      const uint8_t packed = branding::kBootLogoBitmap[
          y * branding::kBootLogoRowBytes + x / 8];
      if (packed & (0x80 >> (x % 8))) {
        startup[(branding::kBootLogoTop + y) * kWidth +
                branding::kBootLogoLeft + x] = 1;
      }
    }
  }
  const std::vector<uint8_t> logoFrame(
      startup.begin(), startup.begin() + kStartupProgressBand.top * kWidth);
  assert(std::count(logoFrame.begin(), logoFrame.end(), uint8_t{1}) > 0);
  for (uint8_t frame = 0; frame < 18; ++frame) {
    std::fill(startup.begin() + kStartupProgressBand.top * kWidth,
              startup.begin() + bottom(kStartupProgressBand) * kWidth, 0);
    const uint16_t pixel = kStartupProgressBand.top * kWidth +
        frame % kStartupProgressBand.width;
    startup[pixel] = 1;
    assert(std::equal(logoFrame.begin(), logoFrame.end(), startup.begin()));
  }
  assert(fits(kPlayerStation));
  assert(fits(kPlayerStationText));
  assert(fits(kPlayerArtist));
  assert(fits(kPlayerStatus));
  assert(kPlayerStation.top == 0 && kPlayerStation.height == 12);
  assert(kPlayerStationText.top == 2 && kPlayerStationText.height == 8);
  assert(kPlayerArtist.top == 14 && kPlayerArtist.height == 8);
  assert(verticalGap(kPlayerStation, kPlayerArtist) == 2);
  assert(verticalGap(kPlayerArtist, kPlayerStatus) == 2);
  assert(kPlayerStatus.top == 24 && kPlayerStatus.height == 8);
  assert(kStationScaleX == 2 && kStationScaleY == 1);
  assert(kStationCellWidth == 12 && kStationCellHeight == 8);
  assert(kArtistCellWidth == 6 && kArtistCellHeight == 8);
  assert(player128x32::station.widget.textsize == 1);
  assert(player128x32::station.widget.top == 2);
  assert(player128x32::station.width == 128);
  assert(player128x32::station.startscrolldelay == 5000);
  assert(player128x32::artist.widget.textsize == 1);
  assert(player128x32::artist.widget.top == 14);
  assert(player128x32::artist.width == 128);
  assert(player128x32::artist.startscrolldelay == 5000);

  assert(contains(kPlayerStatus, kPlayerPlayback));
  assert(contains(kPlayerStatus, kPlayerSource));
  assert(contains(kPlayerStatus, kPlayerSpeaker));
  assert(contains(kPlayerStatus, kPlayerVolume));
  assert(contains(kPlayerStatus, kPlayerBluetoothBand));
  assert(contains(kPlayerStatus, kPlayerWifi));
  assert(kPlayerPlayback.left + kPlayerPlayback.width <= kPlayerSource.left);
  assert(right(kPlayerSource) == kPlayerSpeaker.left);
  assert(right(kPlayerSpeaker) < kPlayerVolume.left);
  assert(right(kPlayerVolume) == kPlayerBluetoothBand.left);
  assert(right(kPlayerBluetoothBand) == kPlayerWifi.left);
  assert(kPlayerPlayback.left == 0 && kPlayerPlayback.width == 10);
  assert(kPlayerSource.left == 10 && kPlayerSource.width == 45);
  assert(kPlayerSpeaker.left == 55 && kPlayerSpeaker.width == 7);
  assert(kPlayerVolume.left == 65 && kPlayerVolume.width == 18);
  assert(kPlayerBluetoothBand.left == 83 &&
         kPlayerBluetoothBand.width == 33);
  assert(kPlayerWifi.left == 116 && kPlayerWifi.width == 12);
  assert(playbackIcon(true, false, false) == PlaybackIcon::Play);
  assert(playbackIcon(false, true, false) == PlaybackIcon::Pause);
  assert(playbackIcon(false, false, true) == PlaybackIcon::Stop);
  assert(playbackIcon(false, false, false) == PlaybackIcon::None);
  assert(bluetoothVisible(true));
  assert(!bluetoothVisible(false));
  assert(contains(kPlayerPlayback, kPlayerPlay));
  assert(contains(kPlayerPlayback, kPlayerPause));
  assert(contains(kPlayerPlayback, kPlayerStop));
  assert(contains(kPlayerWifi, kPlayerWifiDisconnected));
  for (uint8_t i = 0; i < 4; ++i)
    assert(contains(kPlayerWifi, playerWifiBar(i)));

  assert(kBtTransportArtist.top == 0 && kBtTransportArtist.height == 12);
  assert(kBtTransportArtistText.top == 2 &&
         kBtTransportArtistText.height == 8);
  assert(contains(kBtTransportArtist, kBtTransportArtistText));
  assert(kBtTransportTitle.top == 14 && kBtTransportTitle.height == 8);
  assert(verticalGap(kBtTransportArtist, kBtTransportTitle) == 2);
  assert(kBtTransportControls.top == 24 &&
         kBtTransportControls.height == 8);
  assert(verticalGap(kBtTransportTitle, kBtTransportControls) == 2);
  assert(contains(kBtTransportControls, kBtTransportPrevious));
  assert(contains(kBtTransportControls, kBtTransportPlayback));
  assert(contains(kBtTransportControls, kBtTransportNext));
  assert(kBtTransportPrevious.left == 0 && kBtTransportPrevious.width == 6);
  assert(kBtTransportNext.left == 121 && kBtTransportNext.width == 6);
  assert(right(kBtTransportPrevious) <= kBtTransportPlayback.left);
  assert(right(kBtTransportPlayback) <= kBtTransportNext.left);
  assert(centeredTextLeft(kBtTransportPlayback, 4) == 52);
  assert(centeredTextLeft(kBtTransportPlayback, 5) == 49);
  assert(btTransport128x32::artist.widget.top == 2);
  assert(btTransport128x32::artist.width == 128);
  assert(btTransport128x32::title.widget.top == 14);
  assert(btTransport128x32::title.width == 128);
  assert(scrollTextNeedsScrolling(
      scrollTextPixelWidth(11, kStationCellWidth),
      btTransport128x32::artist.width,
      kStationCellWidth, kStationCellHeight));
  assert(scrollTextNeedsScrolling(
      scrollTextPixelWidth(22, kArtistCellWidth),
      btTransport128x32::title.width,
      kArtistCellWidth, kArtistCellHeight));
  assert(std::strcmp(btTransportPlaybackLabel(PlaybackIcon::Play), "PLAY") == 0);
  assert(std::strcmp(btTransportPlaybackLabel(PlaybackIcon::Pause), "PAUSE") == 0);
  assert(std::strcmp(btTransportPlaybackLabel(PlaybackIcon::Stop), "STOP") == 0);
  assert(std::strcmp(btTransportPlaybackLabel(PlaybackIcon::None), "") == 0);

  assertVolume(0, false, "0");
  assertVolume(1, false, "1");
  assertVolume(20, false, "20");
  assertVolume(99, false, "99");
  assertVolume(100, false, "100");
  assertVolume(20, true, "X");
  assertVolume(255, false, "100");
  assertVolumeDialog(0, false, "0%");
  assertVolumeDialog(20, false, "20%");
  assertVolumeDialog(99, false, "99%");
  assertVolumeDialog(100, false, "100%");
  assertVolumeDialog(20, true, "MUTE");
  char lineOut[5]{};
  formatVolume(lineOut, sizeof(lineOut), 80, false, true);
  assert(std::strcmp(lineOut, "LINE") == 0);
  formatVolumePercent(lineOut, sizeof(lineOut), 80, false, true);
  assert(std::strcmp(lineOut, "LINE") == 0);

  assertSource("WEB", "[WEB]");
  assertSource("BT", "[BT]");
  assertSource("DLNA", "[DLNA]");
  assertSource("AUX", "[AUX]");
  assertSource("SPDIF", "[SPDIF]");
  assertSource("TTS", "[TTS]");
  assertMetadata("Artysta", "Utwor", "Artysta - Utwor");
  assertMetadata("Artysta", "", "Artysta");
  assertMetadata("", "Utwor", "Utwor");
  assertMetadata("", "", "");
  assertMetadata(nullptr, "Utwor", "Utwor");
  assertMetadata("Artysta", nullptr, "Artysta");
  assertMetadata("Zażółć", "Gęślą jaźń", "Zażółć - Gęślą jaźń");
  char longMetadata[kMetadataBufferSize]{};
  formatMetadata(longMetadata, sizeof(longMetadata),
                 "Bardzo dluga nazwa wykonawcy",
                 "Jeszcze dluzszy tytul utworu do przewijania");
  assert(std::strstr(longMetadata, " - ") != nullptr);
  assert(std::strlen(longMetadata) * kArtistCellWidth >
         player128x32::artist.width);
  assert(scrollTextNeedsScrolling(
      scrollTextPixelWidth(std::strlen(longMetadata), kArtistCellWidth),
      player128x32::artist.width, kArtistCellWidth, kArtistCellHeight));
  char source[9]{};
  formatSource(source, sizeof(source), "WEB");
  assert(centeredTextLeft(kPlayerSource, std::strlen(source)) == 17);
  formatSource(source, sizeof(source), "SPDIF");
  assert(centeredTextLeft(kPlayerSource, std::strlen(source)) == 11);

  assert(playerVolumeTextLeft(0, false) == 71);
  assert(playerBluetoothTextLeft(77) == 90);
  assert(playerVolumeTextLeft(100, false) == 65);
  assert(playerBluetoothTextLeft(83) == 93);

  assert(wifiBars(false, -40) == 0);
  assert(wifiBars(false, -90) == 0);
  assert(wifiBars(true, -49) == 4);
  assert(wifiBars(true, -50) == 4);
  assert(wifiBars(true, -60) == 3);
  assert(wifiBars(true, -70) == 2);
  assert(wifiBars(true, -80) == 1);
  assert(wifiBars(true, -81) == 0);
  assert(wifiBars(true, 0) == 0);

  char ssid[22]{};
  formatStartupSsid(ssid, sizeof(ssid), "");
  assert(std::strcmp(ssid, "(brak SSID)") == 0);
  formatStartupSsid(ssid, sizeof(ssid), "VoxOne-Test");
  assert(std::strcmp(ssid, "VoxOne-Test") == 0);
  formatStartupSsid(ssid, sizeof(ssid),
                    "12345678901234567890123456789012");
  assert(std::strcmp(ssid, "123456789012345678...") == 0);
  assert(std::strlen(ssid) == kStartupVisibleChars);
  formatStartupSsid(ssid, sizeof(ssid), "Zażółć-gęślą-jaźń-dom");
  assert(std::strstr(ssid, "...") != nullptr);

  // STATIONS uses all four 8 px bands: header, a 2x selected item and footer.
  assert(startup128x32::progress.top == 16 &&
         startup128x32::ssid.top == 24);
  assert(stations128x32::selected.widget.textsize == 2);
  assert(stations128x32::selected.widget.top == 8);
  assert(stations128x32::header.top == 0 &&
         stations128x32::playing.top == 24 &&
         stations128x32::counter.top == 24);
  assert(stations128x32::playing.align == WA_RIGHT);
  assert(stations128x32::counter.align == WA_CENTER);
  assert(fits(kVolumePrimary));
  assert(fits(kVolumeFooter));
  assert(player128x32::volumeNumber.textsize == 2 &&
         player128x32::volumeNumber.top == 0 &&
         player128x32::volumeIp.top == 24);
  assert(!fits(Bounds{0, 25, 128, 8}));
  assert(!fits(Bounds{121, 0, 8, 8}));

  const std::string display = readFile("src/core/display.cpp");
  const std::string widgets = readFile("src/displays/widgets/widgets.cpp");
  const size_t compactBegin = display.find("class Oled128x32PlayerStatusWidget");
  const size_t compactEnd = display.find("class Oled128x64PlayerStatusWidget");
  assert(compactBegin != std::string::npos && compactEnd > compactBegin);
  const std::string compact = display.substr(compactBegin,
                                             compactEnd - compactBegin);
  assert(compact.find("fillTriangle") != std::string::npos);
  assert(compact.find("fillRect") != std::string::npos);
  assert(compact.find("formatVolume") != std::string::npos);
  assert(compact.find("formatVolumePercent") == std::string::npos);
  assert(compact.find("drawSourceIconAndLabel") != std::string::npos);
  assert(compact.find("view.source") != std::string::npos);
  assert(compact.find("source_ == view.source") != std::string::npos);
  assert(compact.find("wifiBars") != std::string::npos);
  assert(compact.find("network.status == CONNECTED") != std::string::npos);
  assert(compact.find("view.playback == DisplayPlaybackState::Playing") !=
         std::string::npos);
  assert(compact.find("DisplayPlaybackState::Paused") != std::string::npos);
  assert(compact.find("DisplayPlaybackState::Stopped") != std::string::npos);
  assert(compact.find("bluetoothVisible(view.btConnected)") !=
         std::string::npos);
  assert(compact.find("dsp.print(\"BT\")") != std::string::npos);
  assert(compact.find("WiFi.RSSI()") == std::string::npos);
  assert(compact.find("dsp.print(\"[]\")") == std::string::npos);
  assert(compact.find("\"V%u\"") == std::string::npos);
  assert(display.find("player128x32::artist") != std::string::npos);
  assert(display.find("class Oled128x32StartupLogoWidget") !=
         std::string::npos);
  assert(display.find("voxone_boot_logo_oled_128x32.h") !=
         std::string::npos);
  assert(display.find("display_boot_branding::oled32") !=
         std::string::npos);
  assert(display.find("dsp.drawBitmap(kBootLogoLeft, kBootLogoTop,") !=
         std::string::npos);
  const size_t boot32Begin = display.find(
      "if (smallOledActive() && oled128x32Active())");
  const size_t boot32End = display.find(
      "else if (smallOledActive())", boot32Begin);
  assert(boot32Begin != std::string::npos && boot32End > boot32Begin);
  const std::string boot32 = display.substr(boot32Begin,
                                            boot32End - boot32Begin);
  const size_t progressOrder = boot32.find("new ProgressWidget");
  const size_t logoOrder = boot32.find("new Oled128x32StartupLogoWidget");
  const size_t ssidOrder = boot32.find("startup128x32::ssid");
  assert(progressOrder < logoOrder && logoOrder < ssidOrder);
  assert(display.find("void loop() override { _draw(); }") !=
         std::string::npos);
  assert(display.find("formatMetadata(") != std::string::npos);
  assert(display.find("_title1->setText(metadata)") != std::string::npos);
  assert(display.find("player128x32::hiddenMetadata") == std::string::npos);
  assert(display.find("kStationScaleX") != std::string::npos);
  assert(display.find("pages[PG_PLAYER]->addWidget(_title1)") !=
         std::string::npos);
  assert(display.find("if (_title1) rows[count++] = playerScrollBinding(_title1)") !=
         std::string::npos);
  assert(widgets.find("dsp.setTextSize(_textScaleX, _textScaleY)") !=
         std::string::npos);
  assert(widgets.find("_charWidth = static_cast<uint8_t>(CHARWIDTH * scaleX)") !=
         std::string::npos);
  assert(display.find("player128x32::title") == std::string::npos);
  assert(display.find("new ScrollWidget(\"*\", player128x64::title") !=
         std::string::npos);
  assert(display.find("btTransport128x32::artist") != std::string::npos);
  assert(display.find("btTransport128x32::title") != std::string::npos);
  assert(display.find("_oledBtTransportArtist->setTextScale(") !=
         std::string::npos);
  assert(display.find("_oledBtTransportArtist->setText(view.artist)") !=
         std::string::npos);
  assert(display.find("_oledBtTransportTitle->setText(view.title)") !=
         std::string::npos);
  assert(display.find("new Oled128x32BtTransportWidget") !=
         std::string::npos);
  const size_t transportBegin = display.find(
      "class Oled128x32BtTransportWidget");
  const size_t transportEnd = display.find(
      "class Oled128x64PlayerStatusWidget", transportBegin);
  assert(transportBegin != std::string::npos && transportEnd > transportBegin);
  const std::string transport = display.substr(
      transportBegin, transportEnd - transportBegin);
  assert(transport.find("dsp.print(\"<\")") != std::string::npos);
  assert(transport.find("dsp.print(\">\")") != std::string::npos);
  assert(transport.find("btTransportPlaybackLabel(icon)") !=
         std::string::npos);
  assert(transport.find("displayPlaybackLabel") == std::string::npos);
  assert(transport.find("PREV") == std::string::npos);
  assert(transport.find("NEXT") == std::string::npos);
  assert(transport.find("formatVolume") == std::string::npos);
  assert(transport.find("dsp.print(\"BT\")") == std::string::npos);
  assert(transport.find("wifiBars") == std::string::npos);
  assert(display.find("moveBluetoothTransportSelection") ==
         std::string::npos);
  assert(display.find("selectedBluetoothTransportInput") ==
         std::string::npos);

  const size_t volumeMode = display.find("if (newmode == VOL)");
  const size_t lostMode = display.find("if (newmode == LOST)", volumeMode);
  assert(volumeMode != std::string::npos && lostMode > volumeMode);
  const std::string compactVolume = display.substr(volumeMode,
                                                    lostMode - volumeMode);
  assert(compactVolume.find("formatVolumePercent") != std::string::npos);
  assert(compactVolume.find("_showDialog(\"\")") != std::string::npos);
  assert(compactVolume.find("_nums->setText(level)") != std::string::npos);

  assert(display.find("\"WEB - STACJE\"") !=
         std::string::npos);
  assert(display.find("pages[PG_PLAYLIST]->addWidget(_stationCarousel)") !=
         std::string::npos);
}
