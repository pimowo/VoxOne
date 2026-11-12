#pragma once

#include <freertos/FreeRTOS.h>

namespace voxone::i2s_output {

bool begin();
bool take(TickType_t waitTicks);
void give();

class Guard {
 public:
  explicit Guard(TickType_t waitTicks) : locked_(take(waitTicks)) {}
  ~Guard() {
    if (locked_) give();
  }

  Guard(const Guard&) = delete;
  Guard& operator=(const Guard&) = delete;

  explicit operator bool() const { return locked_; }

 private:
  bool locked_;
};

}  // namespace voxone::i2s_output
