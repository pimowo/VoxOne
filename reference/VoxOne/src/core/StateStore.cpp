#include "StateStore.h"

StateStore& StateStore::instance() {
    static StateStore s;
    return s;
}

DeviceState StateStore::snapshot() const {
    portENTER_CRITICAL(&_mux);
    DeviceState copy = _state;
    portEXIT_CRITICAL(&_mux);
    return copy;
}

void StateStore::update(const DeviceState& s) {
    portENTER_CRITICAL(&_mux);
    _state = s;
    portEXIT_CRITICAL(&_mux);
}
