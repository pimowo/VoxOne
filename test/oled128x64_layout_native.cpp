#include "../src/displays/conf/player128x64conf.h"
#include "../src/displays/fonts/voxone_boot_logo_oled_128x64.h"

#include <cassert>

namespace {
constexpr uint16_t bottom(const WidgetConfig& widget) {
  return widget.top + widget.textsize * 8;
}
}

int main() {
  constexpr uint16_t charWidth = 6;
  constexpr uint16_t charHeight = 8;
  constexpr uint16_t maxSsidWidth = startup128x64::maxSsidChars * charWidth;
  constexpr uint16_t maxSsidLeft = (128 - maxSsidWidth) / 2;

  static_assert(voxone::display_boot_branding::oled::kBootLogoLeft == 10 &&
                    voxone::display_boot_branding::oled::kBootLogoTop == 5 &&
                    voxone::display_boot_branding::oled::kBootLogoWidth == 108 &&
                    voxone::display_boot_branding::oled::kBootLogoHeight == 23,
                "STARTUP logo bounds changed unexpectedly");
  static_assert(voxone::display_boot_branding::oled::kBootLogoTop +
                    voxone::display_boot_branding::oled::kBootLogoHeight <=
                    startup128x64::progress.top,
                "STARTUP logo overlaps progress");
  static_assert(startup128x64::progress.top == 36 &&
                    startup128x64::progress.textsize == 1 &&
                    startup128x64::progress.align == WA_CENTER &&
                    bottom(startup128x64::progress) == 44,
                "STARTUP progress must be centered in band 3");
  static_assert(startup128x64::ssid.top == 52 &&
                    startup128x64::ssid.textsize == 1 &&
                    startup128x64::ssid.align == WA_CENTER &&
                    bottom(startup128x64::ssid) == 60,
                "STARTUP SSID must be centered in band 4");
  static_assert(startup128x64::ssidBufferSize ==
                    startup128x64::maxSsidChars + 1,
                "STARTUP SSID buffer must include the terminator");
  static_assert(maxSsidLeft == 1 && maxSsidWidth == 126,
                "STARTUP SSID must fit within the OLED width");
  static_assert(charHeight == 8, "OLED classic font metrics changed");

  static_assert(bottom(stations128x64::header) <=
                    stations128x64::selected.widget.top,
                "STATIONS header overlaps selection");
  static_assert(bottom(stations128x64::selected.widget) <=
                    stations128x64::playing.top,
                "STATIONS selection overlaps footer");
  static_assert(bottom(stations128x64::playing) <= 64,
                "STATIONS footer exceeds OLED");
  static_assert(stations128x64::header.textsize == 1 &&
                    stations128x64::playing.textsize == 1 &&
                    stations128x64::counter.textsize == 1,
                "STATIONS header and footer must use the same small font");
  static_assert(stations128x64::header.align == WA_CENTER &&
                    stations128x64::selected.widget.align == WA_CENTER,
                "STATIONS header and selection must be centered");
  static_assert(stations128x64::selected.widget.top == 24 &&
                    stations128x64::selected.widget.textsize == 2,
                "STATIONS selection must be centered between header and footer");
  static_assert(stations128x64::selected.width == 128,
                "STATIONS selected row must clip to OLED width");

  static_assert(player128x64::volumeNumber.top == 24 &&
                    player128x64::volumeNumber.align == WA_CENTER,
                "VOL value must be centered in the middle band");
  static_assert(player128x64::volumeIp.textsize ==
                    stations128x64::header.textsize,
                "VOL IP and STATIONS header must use the same font");
  static_assert(player128x64::station.widget.textsize ==
                    stations128x64::header.textsize,
                "VOL header and STATIONS header must use the same font");
  static_assert(player128x64::clockColonLeft == 59 &&
                    player128x64::clockDigitTop == 33 &&
                    player128x64::clockColonTop == 31,
                "PLAYER clock colon position changed unexpectedly");

  static_assert(bottom(update128x64::title) <= update128x64::target.top,
                "UPDATE title overlaps target");
  static_assert(bottom(update128x64::target) <= update128x64::percent.top,
                "UPDATE target overlaps percent");
  static_assert(bottom(update128x64::percent) <= update128x64::barTop,
                "UPDATE percent overlaps bar");
  static_assert(update128x64::barTop + update128x64::barHeight <=
                    update128x64::activity.top,
                "UPDATE bar overlaps activity");
  static_assert(bottom(update128x64::activity) <= 64,
                "UPDATE activity exceeds OLED");
  static_assert(update128x64::barLeft + update128x64::barWidth <= 128,
                "UPDATE bar exceeds OLED width");
  assert(update128x64::barInteriorWidth == 116);
  assert(update128x64::barInteriorHeight == 6);
}
