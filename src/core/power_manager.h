#ifndef VOXONE_POWER_MANAGER_H
#define VOXONE_POWER_MANAGER_H

#include <atomic>
#include <stdint.h>

#include "button_handler.h"

class PowerManager {
 public:
  void begin();
  void loop();
  bool standby() const { return standby_.load(std::memory_order_acquire); }

 private:
  void beginShutdown();
  void enterStandby();
  void wakeup();

  voxone::power::ButtonHandler button_;
  std::atomic<bool> standby_{false};
  bool shutdownNotice_ = false;
};

extern PowerManager powerManager;

#endif
