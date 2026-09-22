#pragma once

#include <Arduino.h>

class TimeService {
public:
    void begin();
    void onWifiConnected();
    void loop();

private:
    uint32_t _lastPollMs = 0;
    bool _lastValid = false;
    String _lastClockText;

    void publish(bool valid, const String& clockText);
};
