#ifndef VOXONE_NETSERVER_BACKPRESSURE_H_
#define VOXONE_NETSERVER_BACKPRESSURE_H_

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace netserver_backpressure {

enum class InsertResult : uint8_t {
  Inserted,
  Existing,
  Full
};

template <size_t Capacity>
class ClientIdSet {
 public:
  InsertResult insert(uint32_t clientId) {
    for (const Slot& slot : slots_) {
      if (slot.used && slot.clientId == clientId) return InsertResult::Existing;
    }
    for (Slot& slot : slots_) {
      if (!slot.used) {
        slot.clientId = clientId;
        slot.used = true;
        return InsertResult::Inserted;
      }
    }
    return InsertResult::Full;
  }

  bool contains(uint32_t clientId) const {
    for (const Slot& slot : slots_) {
      if (slot.used && slot.clientId == clientId) return true;
    }
    return false;
  }

  void erase(uint32_t clientId) {
    for (Slot& slot : slots_) {
      if (slot.used && slot.clientId == clientId) slot.used = false;
    }
  }

  bool takeFirst(uint32_t& clientId) {
    for (Slot& slot : slots_) {
      if (!slot.used) continue;
      clientId = slot.clientId;
      slot.used = false;
      return true;
    }
    return false;
  }

  void clear() {
    for (Slot& slot : slots_) slot.used = false;
  }

  size_t size() const {
    size_t used = 0;
    for (const Slot& slot : slots_) {
      if (slot.used) ++used;
    }
    return used;
  }

 private:
  struct Slot {
    uint32_t clientId = 0;
    bool used = false;
  };

  Slot slots_[Capacity]{};
};

template <size_t Capacity>
class ClientRequestSet {
 public:
  InsertResult insert(uint32_t clientId, uint8_t requestType) {
    for (const Slot& slot : slots_) {
      if (slot.used && slot.clientId == clientId &&
          slot.requestType == requestType)
        return InsertResult::Existing;
    }
    for (Slot& slot : slots_) {
      if (!slot.used) {
        slot.clientId = clientId;
        slot.requestType = requestType;
        slot.used = true;
        return InsertResult::Inserted;
      }
    }
    return InsertResult::Full;
  }

  void erase(uint32_t clientId, uint8_t requestType) {
    for (Slot& slot : slots_) {
      if (slot.used && slot.clientId == clientId &&
          slot.requestType == requestType)
        slot.used = false;
    }
  }

  void eraseClient(uint32_t clientId) {
    for (Slot& slot : slots_) {
      if (slot.used && slot.clientId == clientId) slot.used = false;
    }
  }

  void clear() {
    for (Slot& slot : slots_) slot.used = false;
  }

  size_t sizeForClient(uint32_t clientId) const {
    size_t used = 0;
    for (const Slot& slot : slots_) {
      if (slot.used && slot.clientId == clientId) ++used;
    }
    return used;
  }

 private:
  struct Slot {
    uint32_t clientId = 0;
    uint8_t requestType = 0;
    bool used = false;
  };

  Slot slots_[Capacity]{};
};

}  // namespace netserver_backpressure
}  // namespace voxone

#endif  // VOXONE_NETSERVER_BACKPRESSURE_H_
