#include "../src/displays/oled128x64_layout.h"

#include <cassert>
#include <cstring>

using namespace voxone::oled128x64;

int main() {
  static_assert(kStartupLogoBand.top == 0 &&
                    bottom(kStartupLogoBand) == 32,
                "STARTUP logo band must be y=0..31");
  static_assert(kStartupProgressBand.top == 32 &&
                    bottom(kStartupProgressBand) == 48,
                "STARTUP progress band must be y=32..47");
  static_assert(kStartupSsidBand.top == 48 &&
                    bottom(kStartupSsidBand) == 64,
                "STARTUP SSID band must be y=48..63");
  static_assert(kStartupLogo.left == 10 && kStartupLogo.top == 4 &&
                    kStartupLogo.width == 108 &&
                    kStartupLogo.height == 23,
                "VoxOne bitmap must be centered in the upper half");
  static_assert(kStartupProgress.top == 36 &&
                    kStartupProgress.height == 8 &&
                    kStartupSsid.top == 52 && kStartupSsid.height == 8,
                "Progress and SSID must be centered in their 16 px bands");

  static_assert(kStationRow.top == 0 && kStationRow.height == 8 &&
                    kArtistRow.top == 10 && kArtistRow.height == 8 &&
                    kTitleRow.top == 20 && kTitleRow.height == 8 &&
                    kClockBand.top == 30 && kClockBand.height == 24 &&
                    kPlayback.top == 56 && kPlayback.height == 8,
                "PLAYER row bounding boxes changed unexpectedly");
  static_assert(verticalGap(kStationRow, kArtistRow) == 2 &&
                    verticalGap(kArtistRow, kTitleRow) == 2 &&
                    verticalGap(kTitleRow, kClockBand) == 2 &&
                    verticalGap(kClockBand, kPlayback) == 2,
                "PLAYER vertical gaps must all be 2 px");
  static_assert(kBluetoothTextWidth == 12 &&
                    kBluetoothBand.top == 56,
                "BT must use plain 12 px text in the status row");

  assert(playbackIcon(true, false, false) == PlaybackIcon::Play);
  assert(playbackIcon(false, true, false) == PlaybackIcon::Pause);
  assert(playbackIcon(false, false, true) == PlaybackIcon::Stop);
  assert(playbackIcon(false, false, false) == PlaybackIcon::None);
  assert(bluetoothVisible(true));
  assert(!bluetoothVisible(false));

  char source[9]{};
  formatSource(source, sizeof(source), "WEB");
  assert(std::strcmp(source, "[WEB]") == 0);
  assert(centeredTextLeft(kSource, std::strlen(source)) == 17);
  formatSource(source, sizeof(source), "SPDIF");
  assert(std::strcmp(source, "[SPDIF]") == 0);
  assert(centeredTextLeft(kSource, std::strlen(source)) == 11);

  char volume[5]{};
  formatStatusVolume(volume, sizeof(volume), 0, false);
  assert(std::strcmp(volume, "0") == 0);
  assert(centeredTextLeft(kVolume, std::strlen(volume)) == 71);
  assert(bluetoothTextLeft(77) == 90);
  formatStatusVolume(volume, sizeof(volume), 100, false);
  assert(std::strcmp(volume, "100") == 0);
  assert(centeredTextLeft(kVolume, std::strlen(volume)) == 65);
  assert(bluetoothTextLeft(83) == 93);
  formatStatusVolume(volume, sizeof(volume), 255, false);
  assert(std::strcmp(volume, "100") == 0);
  formatStatusVolume(volume, sizeof(volume), 42, true);
  assert(std::strcmp(volume, "X") == 0);
  formatStatusVolume(volume, sizeof(volume), 80, false, true);
  assert(std::strcmp(volume, "LINE") == 0);

  char ssid[22]{};
  formatStartupSsid(ssid, sizeof(ssid), "");
  assert(std::strcmp(ssid, "(brak SSID)") == 0);
  formatStartupSsid(ssid, sizeof(ssid), "VoxOne-Test");
  assert(std::strcmp(ssid, "VoxOne-Test") == 0);
  formatStartupSsid(ssid, sizeof(ssid),
                    "12345678901234567890123456789012");
  assert(std::strcmp(ssid, "123456789012345678...") == 0);
  assert(std::strlen(ssid) == kStartupVisibleChars);

  static_assert(right(kPlayback) <= kSource.left,
                "Playback must not overlap source");
  static_assert(right(kSource) <= kSpeaker.left,
                "Source must not overlap speaker");
  static_assert(right(kSpeaker) <= kVolume.left,
                "Speaker must not overlap volume");
  static_assert(right(kVolume) <= kBluetoothBand.left,
                "Volume must not overlap BT band");
  static_assert(right(kBluetoothBand) <= kWifi.left,
                "BT band must not overlap Wi-Fi");
}
