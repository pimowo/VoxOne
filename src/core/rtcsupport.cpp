#include "rtcsupport.h"

#include "../hardware/i2c_runtime.h"

RTC rtc;

bool RTC::init() {
  kind_ = voxone::hardware::currentHardware().rtcKind;
  if (kind_ == voxone::hardware::RtcKind::None ||
      !voxone::hardware::initHardwareI2c()) return false;
  TwoWire& wire = voxone::hardware::hardwareI2cBus();
  switch (kind_) {
    case voxone::hardware::RtcKind::Ds3231: return ds3231_.begin(&wire);
    case voxone::hardware::RtcKind::Ds1307: return ds1307_.begin(&wire);
    case voxone::hardware::RtcKind::None: return false;
  }
  return false;
}

bool RTC::isRunning() {
  switch (kind_) {
    case voxone::hardware::RtcKind::Ds3231: return !ds3231_.lostPower();
    case voxone::hardware::RtcKind::Ds1307: return ds1307_.isrunning();
    case voxone::hardware::RtcKind::None: return false;
  }
  return false;
}

DateTime RTC::now() {
  return kind_ == voxone::hardware::RtcKind::Ds3231
      ? ds3231_.now() : ds1307_.now();
}

void RTC::getTime(struct tm* tinfo) {
  if (isRunning()) {
    const DateTime nowTm = now();
    tinfo->tm_sec = nowTm.second();
    tinfo->tm_min = nowTm.minute();
    tinfo->tm_hour = nowTm.hour();
    tinfo->tm_wday = nowTm.dayOfTheWeek();
    tinfo->tm_mday = nowTm.day();
    tinfo->tm_mon = nowTm.month() - 1;
    tinfo->tm_year = nowTm.year() - 1900;
  } else {
    tinfo->tm_sec++;
    mktime(tinfo);
  }
}

void RTC::setTime(struct tm* tinfo) {
  const DateTime value(tinfo->tm_year + 1900, tinfo->tm_mon + 1,
                       tinfo->tm_mday, tinfo->tm_hour, tinfo->tm_min,
                       tinfo->tm_sec);
  if (kind_ == voxone::hardware::RtcKind::Ds3231) ds3231_.adjust(value);
  else if (kind_ == voxone::hardware::RtcKind::Ds1307) ds1307_.adjust(value);
}
