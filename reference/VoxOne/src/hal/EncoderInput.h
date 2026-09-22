#pragma once
#include <Arduino.h>
#include "../config/ConfigModel.h"

class EncoderInput {
public:
    void begin(const EncoderConfig& config);
    void loop();

private:
    EncoderConfig _config;
    uint8_t _previousAB = 0;
    uint8_t _detentAB = 3;
    int8_t _transitionCount = 0;
    int _pendingRotationDelta = 0;
    uint32_t _invalidTransitions = 0;
    uint32_t _incompleteDetents = 0;
    uint32_t _lastDecoderLogMs = 0;
    uint32_t _lastQueueWarningMs = 0;
    bool _lastButtonRaw = HIGH;
    bool _stableButton = HIGH;
    uint32_t _buttonChangedMs = 0;
    uint32_t _pressStartedMs = 0;
    bool _longPressSent = false;
    uint32_t _pendingClickMs = 0;
    bool _pendingShortClick = false;
};
