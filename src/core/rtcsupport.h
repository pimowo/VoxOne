#ifndef VOXONE_RTC_SUPPORT_H
#define VOXONE_RTC_SUPPORT_H

#include "RTClib.h"
#include "../hardware/hardware_descriptor.h"

class RTC {
 public:
  bool init();
  bool isRunning();
  void getTime(struct tm* tinfo);
  void setTime(struct tm* tinfo);

 private:
  DateTime now();
  voxone::hardware::RtcKind kind_ = voxone::hardware::RtcKind::None;
  RTC_DS3231 ds3231_;
  RTC_DS1307 ds1307_;
};

extern RTC rtc;

#endif
