#ifndef VOXONE_PLAYER128X64CONF_H
#define VOXONE_PLAYER128X64CONF_H

#include <stdint.h>
#include "../widgets/widgetsconfig.h"

// OLED 128x64 PLAYER coordinates shared by SSD1306 and SSD1309.
namespace player128x64 {
constexpr ScrollConfig station = {{0, 0, 1, WA_LEFT}, 140, 128, 5000, 2, 35};
constexpr ScrollConfig artist = {{0, 10, 1, WA_LEFT}, 140, 128, 5000, 2, 35};
constexpr ScrollConfig title = {{0, 20, 1, WA_LEFT}, 140, 128, 5000, 2, 35};
constexpr WidgetConfig wifi = {0, 56, 1, WA_RIGHT};
constexpr WidgetConfig volumeNumber = {0, 26, 0, WA_CENTER};
constexpr WidgetConfig volumeIp = {0, 56, 1, WA_CENTER};
}

// OLED 128x64 STATIONS coordinates shared by SSD1306 and SSD1309.
namespace stations128x64 {
constexpr WidgetConfig header = {0, 0, 1, WA_CENTER};
constexpr WidgetConfig previous = {2, 10, 1, WA_LEFT};
constexpr ScrollConfig selected = {{0, 21, 2, WA_LEFT}, 140, 128, 1200, 2, 35};
constexpr WidgetConfig next = {2, 44, 1, WA_LEFT};
constexpr WidgetConfig playing = {2, 56, 1, WA_LEFT};
constexpr WidgetConfig counter = {2, 56, 1, WA_RIGHT};
}

// OLED 128x64 UPDATE coordinates shared by SSD1306 and SSD1309.
namespace update128x64 {
constexpr WidgetConfig title = {0, 0, 1, WA_CENTER};
constexpr WidgetConfig target = {0, 10, 1, WA_CENTER};
constexpr WidgetConfig percent = {0, 20, 2, WA_CENTER};
constexpr WidgetConfig activity = {0, 54, 1, WA_CENTER};
constexpr uint16_t barLeft = 4;
constexpr uint16_t barTop = 39;
constexpr uint16_t barWidth = 120;
constexpr uint16_t barHeight = 10;
constexpr uint16_t barInset = 2;
constexpr uint16_t barInteriorWidth = barWidth - 2 * barInset;
constexpr uint16_t barInteriorHeight = barHeight - 2 * barInset;
constexpr uint16_t indeterminateWidth = 18;
constexpr uint16_t indeterminateStep = 4;
}

#endif
