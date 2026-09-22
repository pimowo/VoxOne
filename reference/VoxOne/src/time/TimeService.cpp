#include "TimeService.h"

#include <time.h>

#include "AppConfig.h"
#include "../core/StateStore.h"
#include "../diagnostics/Logger.h"

void TimeService::begin() {
    Logger::info("TIME", "NTP waiting for Wi-Fi");
}

void TimeService::onWifiConnected() {
    configTzTime(
        AppConfig::TZ_INFO,
        AppConfig::NTP_SERVER_1,
        AppConfig::NTP_SERVER_2
    );
    Logger::info("TIME", "NTP configured after Wi-Fi connect");
}

void TimeService::publish(
    bool valid,
    const String& clockText
) {
    if (valid == _lastValid &&
        clockText == _lastClockText) {
        return;
    }

    auto s = StateStore::instance().snapshot();
    s.timeValid = valid;
    s.clockText = clockText;
    StateStore::instance().update(s);

    _lastValid = valid;
    _lastClockText = clockText;
}

void TimeService::loop() {
    const uint32_t nowMs = millis();

    if (nowMs - _lastPollMs < AppConfig::TIME_POLL_MS) {
        return;
    }

    _lastPollMs = nowMs;

    const time_t now = time(nullptr);

    // Reject the ESP's unsynchronised epoch.
    if (now < 1700000000) {
        publish(false, "");
        return;
    }

    struct tm local {};
    localtime_r(&now, &local);

    char buf[6];
    snprintf(
        buf,
        sizeof(buf),
        "%02d:%02d",
        local.tm_hour,
        local.tm_min
    );

    publish(true, String(buf));
}
