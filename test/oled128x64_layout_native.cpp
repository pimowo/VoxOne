#include "../src/displays/conf/player128x64conf.h"

#include <cassert>

namespace {
constexpr uint16_t bottom(const WidgetConfig& widget) {
  return widget.top + widget.textsize * 8;
}
}

int main() {
  static_assert(bottom(stations128x64::header) <= stations128x64::previous.top,
                "STATIONS header overlaps previous row");
  static_assert(bottom(stations128x64::previous) <=
                    stations128x64::selected.widget.top,
                "STATIONS previous row overlaps selection");
  static_assert(bottom(stations128x64::selected.widget) <=
                    stations128x64::next.top,
                "STATIONS selection overlaps next row");
  static_assert(bottom(stations128x64::next) <= stations128x64::playing.top,
                "STATIONS next row overlaps footer");
  static_assert(bottom(stations128x64::playing) <= 64,
                "STATIONS footer exceeds OLED");
  static_assert(stations128x64::selected.width == 128,
                "STATIONS selected row must clip to OLED width");

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
