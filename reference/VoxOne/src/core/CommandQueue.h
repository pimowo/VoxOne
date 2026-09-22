#pragma once
#include <Arduino.h>

enum class CommandType : uint8_t {
    VolumeDelta,
    EncoderRotation,
    SetVolumeAbsolute,
    TogglePlayStop,
    EncoderDoubleClick,
    EncoderLongPress,
    SetStop,
    SetPlay,
    Pause,
    Next,
    Previous
};

enum class CommandSource : uint8_t {
    Encoder,
    Web,
    Bluetooth,
    System
};

struct Command {
    CommandType type;
    CommandSource source;
    int value = 0;
};

class CommandQueue {
public:
    static CommandQueue& instance();
    bool begin();
    bool push(const Command& cmd);
    bool pop(Command& cmd);

private:
    QueueHandle_t _queue = nullptr;
};
