#ifndef timekeeper_h
#define timekeeper_h
#pragma once

void _syncTask(void * pvParameters);

enum class DelayedActionSlot : uint8_t {
  REBOOT,
  SLEEP,
  MQTT,
  COUNT
};

class TimeKeeper {
  public:
    volatile bool forceTimeSync;
    volatile bool busy;
  public:
    TimeKeeper();
    bool loop0();
    bool loop1();
    void timeTask();
    void waitAndReturnPlayer(uint32_t time_s);
    void waitAndDo(uint32_t time_s, void (*callback)(), DelayedActionSlot slot);
  private:
    struct DelayedAction {
      uint32_t startedAt;
      uint32_t delayMs;
      void (*callback)();
    };
    uint32_t _returnPlayerStartedAt, _returnPlayerDelayMs;
    bool _returnPlayerPending;
    DelayedAction _delayedActions[static_cast<uint8_t>(DelayedActionSlot::COUNT)];
    void _upRSSI();
    void _upSDPos();
    void _upClock();
    void _upScreensaver();
    void _returnPlayer();
    void _doAfterWait();
    void _doWatchDog();
    
};

extern TimeKeeper timekeeper;

#endif
