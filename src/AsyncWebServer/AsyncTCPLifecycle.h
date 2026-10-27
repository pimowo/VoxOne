#ifndef ASYNCTCP_LIFECYCLE_H_
#define ASYNCTCP_LIFECYCLE_H_

#include <stddef.h>
#include <stdint.h>

namespace asynctcp_lifecycle {

constexpr uint32_t kActiveClosedSlot = 0;
constexpr uint32_t kInvalidatedClosedSlot = UINT32_MAX;

inline bool closedSlotIsActive(uint32_t state) {
    return state == kActiveClosedSlot;
}

inline bool closedSlotIsReusable(uint32_t state) {
    return state != kActiveClosedSlot && state != kInvalidatedClosedSlot;
}

inline void invalidateClosedSlot(uint32_t &state) {
    if (closedSlotIsActive(state)) {
        state = kInvalidatedClosedSlot;
    }
}

template <typename Event, size_t Capacity>
class FixedEventPool {
  public:
    Event * acquire() {
        for(size_t i = 0; i < Capacity; ++i){
            if(!_used[i]){
                _used[i] = true;
                return &_events[i];
            }
        }
        return NULL;
    }

    bool release(Event * event) {
        for(size_t i = 0; i < Capacity; ++i){
            if(event == &_events[i]){
                if(!_used[i]){
                    return false;
                }
                _used[i] = false;
                return true;
            }
        }
        return false;
    }

    size_t available() const {
        size_t count = 0;
        for(size_t i = 0; i < Capacity; ++i){
            if(!_used[i]){
                ++count;
            }
        }
        return count;
    }

  private:
    Event _events[Capacity];
    bool _used[Capacity] = {};
};

// Queue must provide count(), pop() and push(Event *). The caller must be the
// only consumer and must exclude concurrent producers for the whole rotation.
// Under that contract events for other clients keep their order, while every
// event owned by the selected client is released exactly once.
template <typename Queue, typename Event, typename Owner, typename Matches,
          typename Release>
size_t removeQueuedEvents(Queue &queue, Owner owner, Matches matches,
                          Release release) {
    const size_t queued = queue.count();
    size_t removed = 0;
    for (size_t i = 0; i < queued; ++i) {
        Event *event = queue.pop();
        if (!event) {
            break;
        }
        if (matches(event, owner)) {
            release(event);
            ++removed;
        } else {
            queue.push(event);
        }
    }
    return removed;
}

}  // namespace asynctcp_lifecycle

#endif  // ASYNCTCP_LIFECYCLE_H_
