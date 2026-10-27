#ifndef VOXONE_PLAYER128X32CONF_H
#define VOXONE_PLAYER128X32CONF_H

#include <stdint.h>
#include "../oled128x32_layout.h"
#include "../widgets/widgetsconfig.h"

namespace startup128x32 {
constexpr WidgetConfig title = {0, 0, 1, WA_CENTER};
constexpr WidgetConfig progress = {0, 9, 1, WA_CENTER};
constexpr WidgetConfig ssid = {0, 24, 1, WA_CENTER};
constexpr uint16_t ssidBufferSize = 22;
}

namespace setup128x32 {
constexpr ScrollConfig title = {{0, 0, 1, WA_CENTER}, 140, 128, 0, 2, 35};
constexpr ScrollConfig ssid = {{0, 8, 1, WA_CENTER}, 33, 128, 1200, 2, 35};
constexpr WidgetConfig detail = {0, 16, 1, WA_CENTER};
constexpr ScrollConfig footer = {{0, 24, 1, WA_CENTER}, 140, 128, 0, 2, 15};
}

namespace player128x32 {
constexpr ScrollConfig station = {{0, voxone::oled128x32::kPlayerStation.top,
                                    2, WA_LEFT}, 140,
                                   voxone::oled128x32::kPlayerStation.width,
                                   5000, 2, 35};
constexpr ScrollConfig artist = {{0, voxone::oled128x32::kPlayerArtist.top,
                                   1, WA_LEFT}, 140,
                                  voxone::oled128x32::kPlayerArtist.width,
                                  5000, 2, 35};
constexpr WidgetConfig volumeLabel = {
    0, voxone::oled128x32::kVolumeLabel.top, 1, WA_CENTER};
constexpr WidgetConfig volumeNumber = {
    0, voxone::oled128x32::kVolumeValue.top, 2, WA_CENTER};
constexpr WidgetConfig volumeIp = {
    0, voxone::oled128x32::kVolumeFooter.top, 1, WA_CENTER};
constexpr WidgetConfig screensaverClock = {0, 8, 0, WA_CENTER};
}

namespace stations128x32 {
constexpr WidgetConfig header = {0, 0, 1, WA_CENTER};
constexpr ScrollConfig selected = {{0, 8, 2, WA_CENTER}, 140, 128, 1200, 2, 35};
constexpr WidgetConfig playing = {2, 24, 1, WA_LEFT};
constexpr WidgetConfig counter = {2, 24, 1, WA_RIGHT};
}

namespace update128x32 {
constexpr WidgetConfig title = {0, 0, 1, WA_CENTER};
constexpr WidgetConfig target = {0, 8, 1, WA_CENTER};
constexpr WidgetConfig percent = {0, 16, 1, WA_CENTER};
constexpr WidgetConfig activity = {0, 24, 1, WA_CENTER};
}

#endif
