#ifndef VOXONE_BT_LINK_COMMAND_QUEUE_H
#define VOXONE_BT_LINK_COMMAND_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#include "bt_link_protocol.h"

enum class BtLinkCommandKind : uint8_t {
  RequestStatus,
  RequestDiagnostics,
  Ping,
  Play,
  Pause,
  Next,
  Previous,
  SetVolume,
  SetSessionVolume,
};

struct BtLinkCommand {
  BtLinkCommand() = default;
  explicit BtLinkCommand(BtLinkCommandKind commandKind)
      : kind(commandKind) {}
  BtLinkCommandKind kind = BtLinkCommandKind::RequestStatus;
  uint8_t volume = 0;
  BtVolumeSessionCommand session{};
};

template <size_t Capacity>
class BoundedBtLinkCommandQueue {
 public:
  static_assert(Capacity > 0, "BT command queue must not be empty");

  bool push(const BtLinkCommand& command) {
    if (count_ == Capacity) return false;
    entries_[tail_] = command;
    tail_ = (tail_ + 1) % Capacity;
    ++count_;
    return true;
  }

  bool pop(BtLinkCommand& command) {
    if (count_ == 0) return false;
    command = entries_[head_];
    head_ = (head_ + 1) % Capacity;
    --count_;
    return true;
  }

  size_t size() const { return count_; }

 private:
  BtLinkCommand entries_[Capacity]{};
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t count_ = 0;
};

#endif
