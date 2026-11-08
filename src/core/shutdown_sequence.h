#ifndef VOXONE_SHUTDOWN_SEQUENCE_H
#define VOXONE_SHUTDOWN_SEQUENCE_H

#include <stdint.h>

namespace voxone {
namespace shutdown_sequence {

class Timeline {
 public:
  static constexpr uint32_t kVisibleMs = 2500;

  void begin(uint32_t nowMs) {
    startedAtMs_ = nowMs;
    active_ = true;
  }

  void cancel() { active_ = false; }

  bool active() const { return active_; }

  bool screenOffDue(uint32_t nowMs) const {
    return active_ && nowMs - startedAtMs_ >= kVisibleMs;
  }

 private:
  uint32_t startedAtMs_ = 0;
  bool active_ = false;
};

}  // namespace shutdown_sequence
}  // namespace voxone

#endif
