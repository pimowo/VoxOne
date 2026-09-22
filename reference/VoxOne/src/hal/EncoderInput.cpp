#include "EncoderInput.h"
#include "BoardConfig.h"
#include "../core/CommandQueue.h"
#include "../diagnostics/Logger.h"

namespace {
// Index = previous AB << 2 | current AB; 0 means no legal Gray transition.
constexpr int8_t QUAD_TABLE[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0
};
constexpr int kTransitionsPerDetent = 4;
constexpr uint32_t kLongPressMs = 800;
constexpr uint32_t kDoubleClickWindowMs = 400;
}

void EncoderInput::begin(const EncoderConfig& config) {
    _config = config;
    pinMode(_config.pinA, Board::ENC_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT);
    pinMode(_config.pinB, Board::ENC_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT);
    pinMode(_config.pinButton, Board::ENC_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT);

    _previousAB = (digitalRead(_config.pinA) ? 2 : 0) |
                  (digitalRead(_config.pinB) ? 1 : 0);
    // External pull-ups normally park at 11; accept a stable 00 detent too.
    _detentAB = (_previousAB == 0 || _previousAB == 3) ? _previousAB : 3;
    _lastButtonRaw = digitalRead(_config.pinButton);
    _stableButton = _lastButtonRaw;
    _buttonChangedMs = millis();
    _pressStartedMs = millis();
    _longPressSent = false;
    _pendingClickMs = 0;
    _pendingShortClick = false;
}

void EncoderInput::loop() {
    const uint8_t currentAB = (digitalRead(_config.pinA) ? 2 : 0) |
                              (digitalRead(_config.pinB) ? 1 : 0);
    if (currentAB != _previousAB) {
        const uint8_t index = (_previousAB << 2) | currentAB;
        _previousAB = currentAB;
        const int8_t move = QUAD_TABLE[index];

        if (move == 0) {
            // Both bits changed: at least one edge was missed or the input bounced.
            _transitionCount = 0;
            ++_invalidTransitions;
        } else {
            _transitionCount += move;
            if (_transitionCount > kTransitionsPerDetent ||
                _transitionCount < -kTransitionsPerDetent) {
                _transitionCount = 0;
                ++_incompleteDetents;
            }
        }

        if (currentAB == _detentAB) {
            if (_transitionCount == kTransitionsPerDetent ||
                _transitionCount == -kTransitionsPerDetent) {
                // Old yoRadio mapped positive transitions to physical right.
                // Here NORMAL is the opposite base orientation; REVERSED
                // restores right = louder, with inversion applied exactly once.
                const int baseStep = _transitionCount > 0 ? -1 : 1;
                const int direction = _config.direction == EncoderDirection::Reversed
                    ? -baseStep : baseStep;
                _pendingRotationDelta = constrain(
                    _pendingRotationDelta + direction * _config.volumeStep, -100, 100);
            } else if (_transitionCount != 0) {
                ++_incompleteDetents;
            }
            _transitionCount = 0;
        }
    }

    // One bounded rotation command per App loop, retaining a delta if the queue
    // is temporarily full. Rotation never creates PLAY/PAUSE or source commands.
    if (_pendingRotationDelta != 0) {
        if (CommandQueue::instance().push({
                CommandType::EncoderRotation, CommandSource::Encoder, _pendingRotationDelta
            })) {
            _pendingRotationDelta = 0;
        } else if (millis() - _lastQueueWarningMs >= 1000) {
            _lastQueueWarningMs = millis();
            Logger::warn("ENCODER", "Rotation queue full; delta retained");
        }
    }

#ifdef VOXONE_DEBUG
    if (millis() - _lastDecoderLogMs >= 5000 &&
        (_invalidTransitions || _incompleteDetents)) {
        _lastDecoderLogMs = millis();
        Logger::warn("ENCODER",
            "Decoder invalid=" + String(_invalidTransitions) +
            " incomplete=" + String(_incompleteDetents));
        _invalidTransitions = 0;
        _incompleteDetents = 0;
    }
#endif
    const bool raw = digitalRead(_config.pinButton);
    if (raw != _lastButtonRaw) {
        _lastButtonRaw = raw;
        _buttonChangedMs = millis();
    }

    auto queueButton = [&](CommandType type) {
        if (CommandQueue::instance().push({type, CommandSource::Encoder, 0})) {
            Logger::debug("ENCODER", type == CommandType::EncoderLongPress ?
                "Long press queued" : type == CommandType::EncoderDoubleClick ?
                "Double click queued" : "Short press queued");
            return true;
        }
        if (millis() - _lastQueueWarningMs >= 1000) {
            _lastQueueWarningMs = millis();
            Logger::warn("ENCODER", "Button queue full");
        }
        return false;
    };

    if (millis() - _buttonChangedMs >= 30 && raw != _stableButton) {
        _stableButton = raw;
        if (raw == LOW) {
            _pressStartedMs = millis();
            _longPressSent = false;
        } else if (!_longPressSent) {
            const bool longPress =
                _buttonChangedMs - _pressStartedMs >= kLongPressMs;
            if (longPress) {
                _pendingShortClick = false;
                queueButton(CommandType::EncoderLongPress);
            } else if (_pendingShortClick &&
                       millis() - _pendingClickMs <= kDoubleClickWindowMs) {
                if (queueButton(CommandType::EncoderDoubleClick))
                    _pendingShortClick = false;
            } else {
                _pendingShortClick = true;
                _pendingClickMs = millis();
            }
        }
    }

    if (_stableButton == LOW && !_longPressSent &&
        millis() - _pressStartedMs >= kLongPressMs) {
        _pendingShortClick = false;
        _longPressSent = queueButton(CommandType::EncoderLongPress);
    }

    if (_stableButton == HIGH && _pendingShortClick &&
        millis() - _pendingClickMs > kDoubleClickWindowMs) {
        if (queueButton(CommandType::TogglePlayStop))
            _pendingShortClick = false;
    }
}
