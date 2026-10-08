#include "options.h"
#include "Arduino.h"
#include "timekeeper.h"
#include "config.h"
#include "network.h"
#include "display.h"
#include "player.h"
#include "netserver.h"
#include "rtcsupport.h"
#include <esp_sntp.h>
#if DSP_MODEL==DSP_DUMMY
#define DUMMYDISPLAY
#endif

#define SYNC_STACK_SIZE       1024 * 4
#define SYNC_TASK_CORE        0
#define SYNC_TASK_PRIORITY    3

namespace {
portMUX_TYPE returnPlayerMux = portMUX_INITIALIZER_UNLOCKED;
constexpr uint32_t secondsToMillis(uint32_t seconds) {
  return seconds > UINT32_MAX / 1000UL ? UINT32_MAX : seconds * 1000UL;
}

constexpr bool timeoutElapsed(uint32_t now, uint32_t startedAt, uint32_t delayMs) {
  return static_cast<uint32_t>(now - startedAt) >= delayMs;
}

static_assert(secondsToMillis(300) == 300000UL, "timeouts above 255 seconds must be preserved");
static_assert(timeoutElapsed(0x00000020UL, 0xFFFFFFF0UL, 0x30UL), "timeout must survive millis wraparound");
static_assert(!timeoutElapsed(0x0000001FUL, 0xFFFFFFF0UL, 0x30UL), "timeout must not fire early after millis wraparound");
}

#ifdef HEAP_DBG
  void printHeapFragmentationInfo(const char* title){
    size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    float fragmentation = 100.0 * (1.0 - ((float)largestBlock / (float)freeHeap));
    Serial.printf("\n****** %s ******\n", title);
    Serial.printf("* Free heap: %u bytes\n", freeHeap);
    Serial.printf("* Largest free block: %u bytes\n", largestBlock);
    Serial.printf("* Fragmentation: %.2f%%\n", fragmentation);
    Serial.printf("*************************************\n\n");
  }
  #define HEAP_INFO() printHeapFragmentationInfo(__PRETTY_FUNCTION__)
#else
  #define HEAP_INFO()
#endif

TimeKeeper timekeeper;
static std::atomic<uint32_t> ntpSyncCount{0};
static std::atomic<uint32_t> syncTaskSequence{0};

static bool logTimeSequence(uint32_t sequence) {
  return sequence <= 10 || sequence % 100 == 0;
}

static void onNtpSync(timeval*) {
  ++ntpSyncCount;
  timekeeper.forceTimeSync = true;
}

void TimeKeeper::watchNtp() {
  sntp_set_time_sync_notification_cb(onNtpSync);
}

void _syncTask(void *pvParameters) {
  const uint32_t sequence = syncTaskSequence.fetch_add(1) + 1;
  if (logTimeSequence(sequence)) {
    Serial.printf("##[TIME]# syncTask start seq=%lu force=%u count=%lu restart=%u\n",
                  static_cast<unsigned long>(sequence), timekeeper.forceTimeSync.load(),
                  static_cast<unsigned long>(ntpSyncCount.load()), timekeeper.restartNtp.load());
  }
  if (timekeeper.forceTimeSync) timekeeper.timeTask();
  if (logTimeSequence(sequence)) {
    Serial.printf("##[TIME]# syncTask done seq=%lu force=%u count=%lu restart=%u\n",
                  static_cast<unsigned long>(sequence), timekeeper.forceTimeSync.load(),
                  static_cast<unsigned long>(ntpSyncCount.load()), timekeeper.restartNtp.load());
  }
  timekeeper.busy = false;
  vTaskDelete(NULL);
}

TimeKeeper::TimeKeeper(){
  busy          = false;
  forceTimeSync = true;
  successfulSyncCount = 0;
  forceRtcSync = true;
  restartNtp = false;
  for(uint8_t i = 0; i < static_cast<uint8_t>(DelayedActionSlot::COUNT); i++){
    _delayedActions[i] = {0, 0, nullptr};
  }
}

bool TimeKeeper::loop0(){ // core0 (display)
  if (network.status != CONNECTED
#if RTCSUPPORTED
      && !config.isRTCFound()
#endif
      ) return true;
  uint32_t currentTime = millis();
  static uint32_t _last1s = 0;
  static uint32_t _last2s = 0;
  static uint32_t _last5s = 0;
  if (currentTime - _last1s >= 1000) { // 1sec
    _last1s = currentTime;
//#ifndef DUMMYDISPLAY
#if !defined(DUMMYDISPLAY)
  #ifndef UPCLOCK_CORE1
    _upClock();
  #endif
#endif
  }
  if (currentTime - _last2s >= 2000) { // 2sec
    _last2s = currentTime;
    _upRSSI();
  }
  if (currentTime - _last5s >= 5000) { // 5sec
    _last5s = currentTime;
    //HEAP_INFO();
  }

  return true; // just in case
}

bool TimeKeeper::loop1(){ // core1 (player)
  uint32_t currentTime = millis();
  static uint32_t _last1s = 0;
  static uint32_t _last2s = 0;
  if (currentTime - _last1s >= 1000) { // 1sec
    _last1s = currentTime;
//#ifndef DUMMYDISPLAY
#if !defined(DUMMYDISPLAY)
  #ifdef UPCLOCK_CORE1
    _upClock();
  #endif
#endif
    _upScreensaver();
    _returnPlayer();
    _doAfterWait();
  }
  if (currentTime - _last2s >= 2000) { // 2sec
    _last2s = currentTime;
  }


  static uint32_t lastTimeTime = 0;
  const uint32_t ntpInterval = static_cast<uint32_t>(constrain(config.store.timeSyncInterval, 1, 10080)) * 60000UL;
  if (currentTime - lastTimeTime >= ntpInterval) {
    lastTimeTime = currentTime;
    forceTimeSync = true;
  }
  if (!busy && forceTimeSync && network.status == CONNECTED) {
    busy = true;
    //config.setTimeConf();
    BaseType_t result = xTaskCreatePinnedToCore(
      _syncTask,
      "syncTask",
      SYNC_STACK_SIZE,
      NULL,           // Params
      SYNC_TASK_PRIORITY,
      NULL,           // Descriptor
      SYNC_TASK_CORE
    );
    if(result != pdPASS){
      busy = false;
      Serial.println("##[ERROR]#\tFailed to create syncTask; time sync will retry");
    }
  }
  
  return true; // just in case
}

void TimeKeeper::waitAndReturnPlayer(uint32_t time_s){
  portENTER_CRITICAL(&returnPlayerMux);
  _returnPlayerTimer.arm(millis(), time_s);
  portEXIT_CRITICAL(&returnPlayerMux);
}
void TimeKeeper::waitAndReturnPlayerForMode(displayMode_e mode, uint32_t time_s){
  portENTER_CRITICAL(&returnPlayerMux);
  _returnPlayerTimer.armForMode(millis(), time_s, mode);
  portEXIT_CRITICAL(&returnPlayerMux);
}
void TimeKeeper::cancelReturnPlayer(){
  portENTER_CRITICAL(&returnPlayerMux);
  _returnPlayerTimer.cancel();
  portEXIT_CRITICAL(&returnPlayerMux);
}
void TimeKeeper::_returnPlayer(){
  portENTER_CRITICAL(&returnPlayerMux);
  const bool due = _returnPlayerTimer.poll(millis(), display.mode());
  portEXIT_CRITICAL(&returnPlayerMux);
  if(due){
    display.putRequest(NEWMODE, PLAYER);
  }
}

void TimeKeeper::waitAndDo(uint32_t time_s, void (*callback)(), DelayedActionSlot slot){
  uint8_t index = static_cast<uint8_t>(slot);
  if(index >= static_cast<uint8_t>(DelayedActionSlot::COUNT) || callback == nullptr) return;
  _delayedActions[index] = {millis(), secondsToMillis(time_s), callback};
}
void TimeKeeper::_doAfterWait(){
  uint32_t now = millis();
  for(uint8_t i = 0; i < static_cast<uint8_t>(DelayedActionSlot::COUNT); i++){
    DelayedAction &action = _delayedActions[i];
    if(action.callback != nullptr && timeoutElapsed(now, action.startedAt, action.delayMs)){
      void (*callback)() = action.callback;
      action.callback = nullptr;
      callback();
    }
  }
}

void TimeKeeper::_upClock(){
#if RTCSUPPORTED
  if(config.isRTCFound() && rtc.isRunning()) rtc.getTime(&network.timeinfo);
  else if(network.timeinfo.tm_year>100) {
    network.timeinfo.tm_sec++;
    mktime(&network.timeinfo);
  }
#else
  if(network.timeinfo.tm_year>100) {
    network.timeinfo.tm_sec++;
    mktime(&network.timeinfo);
  }
#endif
  if(display.ready()) display.putRequest(CLOCK);
}

void TimeKeeper::_upScreensaver(){
  if(!display.ready()) return;
  if(config.store.screensaverEnabled && display.mode()==PLAYER && !player.isRunning()){
    config.screensaverTicks++;
    if(config.screensaverTicks > config.store.screensaverTimeout+SCREENSAVERSTARTUPDELAY){
      if(config.store.screensaverBlank){
        display.putRequest(NEWMODE, SCREENBLANK);
      }else{
        display.putRequest(NEWMODE, SCREENSAVER);
      }
      config.screensaverTicks=SCREENSAVERSTARTUPDELAY;
    }
  }
  if(config.store.screensaverPlayingEnabled && display.mode()==PLAYER && player.isRunning()){
    config.screensaverPlayingTicks++;
    if(config.screensaverPlayingTicks > config.store.screensaverPlayingTimeout*60+SCREENSAVERSTARTUPDELAY){
      if(config.store.screensaverPlayingBlank){
        display.putRequest(NEWMODE, SCREENBLANK);
      }else{
        display.putRequest(NEWMODE, SCREENSAVER);
      }
      config.screensaverPlayingTicks=SCREENSAVERSTARTUPDELAY;
    }
  }
}

void TimeKeeper::_upRSSI(){
  if(network.status == CONNECTED){
    netserver.setRSSI(WiFi.RSSI());
    netserver.requestOnChange(NRSSI, 0);
    if(display.ready()) display.putRequest(DSPRSSI, netserver.getRSSI());
  }
  player.sendCommand({PR_VUTONUS, 0});
}

void TimeKeeper::timeTask(){
  static uint32_t handledSyncCount = 0;
#if RTCSUPPORTED
  static uint32_t lastRtcSyncAt = 0;
#endif
  config.waitConnection();
  if (restartNtp) {
    restartNtp = false;
    handledSyncCount = ntpSyncCount;
    forceTimeSync = false;
    config.setTimeConf();
    return;
  }
  const uint32_t receivedSyncCount = ntpSyncCount;
  if (receivedSyncCount == handledSyncCount) {
    forceTimeSync = false;
    config.setTimeConf(); // Restart SNTP; its callback will schedule the RTC update.
    return;
  }
  if(getLocalTime(&network.timeinfo, 1000)){
    handledSyncCount = receivedSyncCount;
    forceTimeSync = false;
    mktime(&network.timeinfo);
    display.putRequest(CLOCK, 1);
    #if RTCSUPPORTED
      const uint32_t rtcInterval = static_cast<uint32_t>(constrain(config.store.timeSyncIntervalRTC, 1, 1000)) * 3600000UL;
      if (config.isRTCFound() && (forceRtcSync || lastRtcSyncAt == 0 || millis() - lastRtcSyncAt >= rtcInterval)) {
        rtc.setTime(&network.timeinfo);
        lastRtcSyncAt = millis();
        forceRtcSync = false;
      }
    #endif
    ++successfulSyncCount;
    network.requestTimeSync(true);
  }else{
    forceTimeSync = true;
  }
}
//******************
