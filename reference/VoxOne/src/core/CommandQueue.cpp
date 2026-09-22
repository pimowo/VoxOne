#include "CommandQueue.h"

CommandQueue& CommandQueue::instance() {
    static CommandQueue q;
    return q;
}

bool CommandQueue::begin() {
    if (_queue) return true;
    _queue = xQueueCreate(16, sizeof(Command));
    return _queue != nullptr;
}

bool CommandQueue::push(const Command& cmd) {
    return _queue && xQueueSend(_queue, &cmd, 0) == pdTRUE;
}

bool CommandQueue::pop(Command& cmd) {
    return _queue && xQueueReceive(_queue, &cmd, 0) == pdTRUE;
}
