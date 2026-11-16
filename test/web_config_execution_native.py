#!/usr/bin/env python3
"""Execute production WS/REST admission, authorization, consumer and dispatcher.

FreeRTOS queues, client transport, clock and NVS backend are controlled fakes.
No ESP32, real NVS or audio is claimed. Config's actual Tone/Balance/startup,
HA/theme/power methods and saveValue template are included; hardware-only setters are
adapters. The REST policy/session adapter is fake (tested separately natively).
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from hls_ts_transport_native import extract_function

ROOT = Path(__file__).resolve().parents[1]

PREFIX = r'''
#include "src/core/web_config_command.h"
#include "src/core/rest_config_operation.h"
#include "src/core/configuration_mutation.h"
#include "src/core/tft_theme_config.h"
#include "src/core/power_mode_config.h"
#include "src/core/web_tone_command.h"
#include "src/core/websocket_command.h"
#include "src/core/audio_balance.h"
#include "src/core/line_out_config.h"
#include "src/core/ui_timeout_config.h"
#include "src/core/display_scroll.h"
#include "src/core/commandhandler.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <deque>
#include <string>
#include <vector>
#include <iostream>
#define VOXONE_INCLUDE_DSP 0
#define VOXONE_HAS_BT 0
#define VOXONE_PIN_MAP_COMPLETE 0
using TickType_t = unsigned;
constexpr int pdTRUE=1, WS_CONNECTED=1, WS_TEXT=1;
static bool callback=false, storageOk=true, securityBusy=false, updateLocked=false;
static bool sessionValid=true;
static uint32_t generation=1;
static std::vector<std::string> writes, replies, commands;
static std::string currentCommand;
static unsigned nextCount=0, prevCount=0;
static bool commit() {
  assert(!callback); // A regression that persists from onWsMessage aborts here.
  writes.push_back(currentCommand);
  return storageOk;
}
struct Queue {
  size_t size, capacity;
  std::deque<std::vector<unsigned char>> entries;
};
using QueueHandle_t=Queue*;
static int xQueueSend(Queue* q,const void* p,unsigned wait) {
  assert(wait==0);
  if(q->entries.size()==q->capacity) return 0;
  const auto* bytes=static_cast<const unsigned char*>(p);
  q->entries.emplace_back(bytes,bytes+q->size); return 1;
}
static int xQueuePeek(Queue* q,void* p,unsigned wait) {
  assert(wait==0); if(q->entries.empty()) return 0;
  memcpy(p,q->entries.front().data(),q->size); return 1;
}
static int xQueueReceive(Queue* q,void* p,unsigned wait) {
  if(!xQueuePeek(q,p,wait)) return 0;
  q->entries.pop_front(); return 1;
}
using SemaphoreHandle_t=bool*;
static bool mutationHeld=false;
static int xSemaphoreTake(bool* held,unsigned wait) {
  assert(wait==0); if(*held) return 0; *held=true; return 1;
}
static void xSemaphoreGive(bool* held) { assert(*held); *held=false; }
static uint32_t testNow=100;
static uint32_t millis(){return testNow;}
static bool systemRestartPending(){return false;}
static uint32_t esp_random(){static uint32_t n=1;return n++;}
namespace voxone { namespace security {
struct SecuritySessionBinding { uint32_t securityGeneration=1; int mode=1; uint8_t tokenHash[32]={}; };
static bool securityHttpEnforcementEnabled(){return true;}
static bool securityWebSocketCommandAllowed(const char*){return true;}
struct Sessions {
  bool bind(const char*,uint32_t,SecuritySessionBinding& b) {
    b.securityGeneration=generation;return sessionValid;
  }
  bool validateBinding(const SecuritySessionBinding& b,uint32_t,bool) {
    return sessionValid && b.securityGeneration==generation;
  }
};
static Sessions& deviceSecuritySessionService(){static Sessions s;return s;}
constexpr size_t kSecuritySessionTokenBufferSize=33;
} }
struct SecurityRuntimeLock {
  SecurityRuntimeLock(bool,unsigned){}
  bool locked()const{return !securityBusy;}
};
struct AsyncWebSocketClient {
  bool connected=true;
  voxone::security::SecuritySessionBinding binding;
  uint32_t id()const{return 7;}
  int status()const{return connected?WS_CONNECTED:0;}
};
static AsyncWebSocketClient client;
static auto webSocketBinding(AsyncWebSocketClient* c)
 -> voxone::security::SecuritySessionBinding* {return &c->binding;}
struct Socket {
  template<class F> void forEachClient(F f){f(&client);}
  void close(uint32_t,int,const char*){client.connected=false;}
} websocket;
enum class WebSocketAuthorization { Authorized, Denied, Busy };
static bool securityWebSocketText(uint32_t,const char* p){
  if(securityBusy)return false;
  replies.emplace_back(p);return true;
}
static void scheduleOverloadRejection(uint32_t){replies.push_back("overload");}
struct AwsFrameInfo { bool final=true; size_t index=0,len=0; int opcode=WS_TEXT; };
enum { VOLUME, BALANCE, EQUALIZER, GETSCREEN, GETSYSTEM, GETINDEX, NRSSI, WEBSTATUS,
       RESETIDLE, CLEAR, PLAYER, SHOWVUMETER, DRAWVOL, PR_PLAY, PR_STOP };
enum { STARTUP_LAST=0, STARTUP_FIXED=1 };
struct NetServer {
  char _wscmd[32]={},_wsval[128]={};
  void onWsMessage(void*,const uint8_t*,size_t,uint32_t);
  void serviceScrollSettings();
  void requestOnChange(int,int){replies.push_back("snapshot");}
} netserver;
static bool updateLockActive(){return updateLocked;}
struct { bool blocksRequests(){return updateLocked;} } systemOperationState;
static std::atomic<bool> updateNetworkQuiescing{false};
static void requestSystemRestart(){}
static void transitionUiMode(int){}
struct {void putRequest(int){} void flip(){} } display;
static UiTimeoutSettings timeoutSettings;
const UiTimeoutSettings& uiTimeoutConfig(){return timeoutSettings;}
bool uiTimeoutSave(const UiTimeoutSettings& s){if(!commit())return false;timeoutSettings=s;return true;}
enum class SourceId { HomeAssistant };
static bool parseSourceId(const char*,SourceId&){return true;}
static bool sourceManagerSourceAvailable(SourceId){return true;}
static bool sourceManagerRequestSource(SourceId){return true;}
enum class WebTransportAction { RadioPrevious,RadioToggle,RadioNext };
static WebTransportAction webTransportAction(bool,const char* s){return !strcmp(s,"prev")?WebTransportAction::RadioPrevious:WebTransportAction::RadioNext;}
namespace voxone { namespace source_media {
struct RequestStorage {int view(){return 0;}};
static bool parseRequest(const char*,size_t,RequestStorage&){return true;}
} namespace config_format { enum class ConfigStorageMode {V7}; } }
struct Player {
  struct Request {int type,payload;};
  void sendCommand(Request){}
  void prev(){++prevCount;} void next(){++nextCount;} void toggle(){}
  void requestSourceMedia(int){}
  void setUserVol(uint8_t){assert(!callback);}
  void setMuted(bool){assert(!callback);}
  void requestMaximumVolume(uint8_t){}
  void setTone(int,int,int){} void setBalance(int){}
  void applyCurrentVolume(){}
} player;
struct Config {
  struct Store {
    char haName[33]{};
    TftTheme tftTheme=THEME_DEFAULT;
    PowerMode powerMode=MODE_SOFT_STANDBY;
    int8_t bass=0,middle=0,trebble=0,balance=0;
    uint8_t startupMode=0,startupFixedVolume=0;
    bool flipscreen=false,haDiscoveryEnabled=false;
    uint16_t timeSyncInterval=1,timeSyncIntervalRTC=1;
    bool lineOutEnabled=false,vumeter=false;
    uint8_t lineOutVolume=100,maximumVolume=100,reservedWeather[79]={};
  } store;
  struct Storage {
    bool dirty_=false;
    template<class T> void update(T& f,T v,bool force=false){dirty_|=force||f!=v;f=v;}
    bool dirty()const{return dirty_;}
    void markDirty(){dirty_=true;}
    voxone::config_format::ConfigStorageMode mode(){return voxone::config_format::ConfigStorageMode::V7;}
  } _storage;
  struct WriteLock {WriteLock(void*){assert(!callback);}};
  void* _persistMutex=nullptr;
  bool _startupReadOnly=false;
  struct Pending {bool value=false;void store(bool v,std::memory_order){value=v;}} _pendingConfigSave;
  void _publishRuntimeLocked(){}
  bool persistV7(){bool ok=commit();if(ok)_storage.dirty_=false;return ok;}
  static int8_t clampTone(int8_t v){return std::max(-6,std::min(6,int(v)));}
  static int8_t clampBalance(int8_t v){return std::max(-16,std::min(16,int(v)));}
  struct Runtime {int8_t bass,middle,treble; PowerMode powerMode;};
  Runtime runtimeSnapshot(){return {store.bass,store.middle,store.trebble,store.powerMode};}
  bool setTone(int8_t,int8_t,int8_t);
  bool setHaName(const char*);
  bool setTftTheme(TftTheme);
  bool setPowerMode(PowerMode);
  bool setBalance(int8_t);
  bool setStartupMode(uint8_t);
  bool setStartupFixedVolume(uint8_t);
  bool setEncoderUiReversed(bool){return commit();}
  bool setMaximumVolume(uint8_t){return commit();}
  bool setLineOutEnabled(bool,bool now=false);
  bool setLineOutVolume(uint8_t,bool now=false);
  bool setVuMeter(bool,bool now=false);
  void setRuntimeBrightness(uint8_t){assert(!callback);}
  bool setBrightness(bool){return commit();}
  uint16_t lastStation(){return 1;}
  uint16_t playlistLength(){return 4;}
'''

MIDDLE = r'''
} config;
CommandHandler cmd;
// GET metadata only. No actuator API is exposed to this harness.
struct PowerManager {bool standby(){return false;}} powerManager;
namespace voxone { namespace hardware {
struct Hardware {struct {int button=42, relay=-1;} power;};
static const Hardware& currentHardware(){static Hardware h;return h;}
static bool hasPin(int pin){return pin>=0;}
} }
struct Param {std::string data; const std::string& value()const{return data;}};
struct AsyncWebServerRequest {
  Param parameter;
  bool has=true,allowed=true;
  int status=0;
  std::string body;
  bool hasParam(const char*,bool=false){return has;}
  Param* getParam(const char*,bool=false){return &parameter;}
};
static bool deviceApiAuthorized(AsyncWebServerRequest*){return true;}
struct Decision{bool allowed;int httpStatus;};
static Decision authorizeHttpRequest(AsyncWebServerRequest* r){return {r->allowed,r->allowed?200:403};}
static bool requestSessionToken(AsyncWebServerRequest*,char*){return false;}
static void stationsReply(AsyncWebServerRequest* r,int status,const char* body){r->status=status;r->body=body;}
using String=std::string;
namespace voxone { namespace display_scroll {
ScrollSettingsWebView currentScrollSettingsWebView(){
  ScrollSettingsWebView v{};v.canScrollSettings=true;v.scrollCoreActive=true;return v;
}
ScrollSettingsApplyResult applyPersistentScrollSettings(const ScrollSettings&,bool active){
  bool ok=commit();
  return {ok?ScrollSettingsApplyStatus::Applied:ScrollSettingsApplyStatus::StorageCommitFailed,
          ok,ok,ok,active};
}
} }
'''

TESTS = r'''
static void send(const std::string& s){
  AwsFrameInfo frame;frame.len=s.size();callback=true;
  netserver.onWsMessage(&frame,reinterpret_cast<const uint8_t*>(s.data()),s.size(),7);
  callback=false;
}
static void reset(){
  toneCommandQueue->entries.clear();scrollSettingsCommandQueue->entries.clear();
  writes.clear();replies.clear();config=Config{};mutationHeld=false;
  securityBusy=false;sessionValid=true;generation=1;client=AsyncWebSocketClient{};
  storageOk=true;updateLocked=false;
  restConfigOperations=decltype(restConfigOperations){};testNow=100;
}
static void step(const char* name){currentCommand=name;netserver.serviceScrollSettings();}
static bool replyHas(const char* s){
  for(const auto& r:replies)if(r.find(s)!=std::string::npos)return true;
  return false;
}
int main(){
  Queue tone{sizeof(QueuedToneCommand),4,{}},scroll{sizeof(QueuedScrollSettingsCommand),4,{}};
  toneCommandQueue=&tone;scrollSettingsCommandQueue=&scroll;
  // Every ordinary setting goes through the actual callback and dispatcher.
  const char* settings[]={"balance=-4","startupmode=1","startupfixedvolume=42",
    "dim=60","encoderuireversed=1","stationlisttimeout=20","bttransporttimeout=30",
    "flipscreen=1","timeint=60","timeintrtc=24","hadiscovery=1",
    "lineoutenabled=1","lineoutvolume=50","vumeter=1","maximumvolume=80"};
  for(const char* s:settings){reset();send(s);assert(writes.empty());step(s);
    assert(writes.size()==1);assert(replyHas("\"saved\""));}
  reset();send("vol100=42");send("mute=1");step("vol100");step("mute");
  assert(writes.empty());assert(replyHas("\"applied\""));
  reset();assert(config.setLineOutEnabled(true));assert(config.setLineOutVolume(42));
  assert(config.setVuMeter(true));assert(writes.empty());assert(config._pendingConfigSave.value);
  reset();send("bass=3");send("balance=4");send("tone=-2,1,2");send("treble=5");
  assert(writes.empty());
  step("bass");step("balance");step("tone");step("treble");
  assert((writes==std::vector<std::string>{"bass","balance","tone","treble"}));
  assert(config.store.bass==-2 && config.store.middle==1 && config.store.trebble==5);
  // Real queue admission and bounded capacity; the fifth request is not saved.
  reset();for(int i=0;i<4;++i)send("bass=1");send("balance=2");
  assert(tone.entries.size()==4);assert(replyHas("\"busy\""));assert(writes.empty());
  reset();mutationHeld=true;send("balance=1");assert(replyHas("\"busy\""));
  assert(tone.entries.empty());mutationHeld=false;
  // NVS error follows actual Config::saveValue / setter return paths.
  for(const char* s:{"bass=2","balance=3","startupmode=1","startupfixedvolume=3",
                     "stationlisttimeout=12","flipscreen=1","timeint=30",
                     "lineoutenabled=1","lineoutvolume=42","vumeter=1"}){
    reset();storageOk=false;send(s);step(s);
    assert(replyHas("storage_failed"));assert(!replyHas("\"saved\""));
    assert(!replyHas("snapshot"));
    storageOk=true;replies.clear();send(s);step(s);
    assert(replyHas("\"saved\""));assert(writes.size()==2);
  }
  reset();send("balance=2");sessionValid=false;step("expired");
  assert(writes.empty() && tone.entries.empty() && !client.connected);
  reset();send("balance=2");++generation;step("revoked");assert(writes.empty());
  reset();send("balance=2");client.binding.tokenHash[0]=1;step("rebound");assert(writes.empty());
  reset();send("balance=2");client.connected=false;step("disconnected");assert(writes.empty());
  reset();send("balance=2");securityBusy=true;step("busy");
  assert(writes.empty() && tone.entries.size()==1);
  securityBusy=false;step("balance");assert(writes.size()==1);
  reset();securityBusy=true;send("balance=2");assert(tone.entries.empty());
  assert(replyHas("overload"));
  reset();sessionValid=false;send("balance=2");assert(tone.entries.empty());
  assert(!client.connected);
  reset();send("balance=2");updateLocked=true;step("locked");
  assert(writes.empty() && tone.entries.size()==1);updateLocked=false;step("balance");
  // Existing scroll and tone queues share admission order, across wraparound.
  reset();configurationCommandSequence=0xfffffffeU;
  send("bass=1");
  const auto settingsValue=voxone::display_scroll::defaultScrollSettings();
  send("scrollsettings=true,normal,1000");
  assert(scroll.entries.size()==1);
  send("balance=2");step("bass");step("scroll");step("balance");
  assert((writes==std::vector<std::string>{"bass","scroll","balance"}));
  reset();for(int i=0;i<4;++i)assert(enqueueScrollSettingsCommand(7,settingsValue)==ScrollCommandQueueStatus::Queued);
  assert(enqueueScrollSettingsCommand(7,settingsValue)==ScrollCommandQueueStatus::Busy);
  reset();send("next=1");send("prev=1");assert(nextCount==1 && prevCount==1);
  send("getscreen=1");assert(replyHas("snapshot"));assert(writes.empty());
  reset();for(const char* s:{"balance=17","timeint=0","startupmode=3","dim=abc"})send(s);
  assert(tone.entries.empty());assert(replyHas("invalid"));
  voxone::web_config::Command parsed{};
  assert(voxone::web_config::parse("dim","999",parsed)==voxone::web_config::Parse::Ok);
  assert(std::string(parsed.value)=="100");
  assert(voxone::web_config::parse("timeint","999999999999999999999999",parsed)==voxone::web_config::Parse::Invalid);
  // Real REST handlers -> same FIFO consumer -> real setters. HTTP transport
  // and authorization adapter are fakes; policy/session have native tests.
  reset();AsyncWebServerRequest post;post.parameter.data="Kitchen";
  callback=true;handleHaSettingsSave(&post);callback=false;
  assert(post.status==202 && writes.empty());
  const auto* op=restConfigOperations.find(tone.entries.empty()?0:1);
  assert(op);std::string id=op->id;
  AsyncWebServerRequest get;get.parameter.data=id;
  handleConfigOperationRead(&get);assert(get.status==200 && get.body.find("pending")!=std::string::npos);
  step("rest-ha");assert(writes.size()==1 && std::string(config.store.haName)=="Kitchen");
  handleConfigOperationRead(&get);assert(get.body.find("saved")!=std::string::npos);
  testNow+=voxone::rest_config::kRetentionMs;step("idle-expiry");
  assert(!restConfigOperations.find(1));
  reset();post.parameter.data="yamaha";
  callback=true;handleDisplayThemeSave(&post);callback=false;
  assert(post.status==202 && writes.empty());storageOk=false;step("rest-theme-fail");
  assert(restConfigOperations.find(1)->status==voxone::rest_config::Status::AppliedNotPersisted);
  assert(config.store.tftTheme==THEME_YAMAHA);
  storageOk=true;handleDisplayThemeSave(&post);step("rest-theme-retry");
  assert(writes.size()==2 && restConfigOperations.find(2)->status==voxone::rest_config::Status::Saved);
  reset();post.parameter.data="Retry";storageOk=false;handleHaSettingsSave(&post);step("ha-fail");
  handleHaSettingsSave(&post);step("ha-fail-again");assert(writes.size()==2);
  assert(restConfigOperations.find(2)->status==voxone::rest_config::Status::AppliedNotPersisted);
  storageOk=true;handleHaSettingsSave(&post);step("ha-retry");assert(writes.size()==3);
  handleHaSettingsSave(&post);step("ha-noop");assert(writes.size()==3);
  assert(restConfigOperations.find(4)->status==voxone::rest_config::Status::Saved);
  reset();post.parameter.data="Queued";for(int i=0;i<4;++i)send("bass=1");
  handleHaSettingsSave(&post);assert(post.status==503 && !restConfigOperations.find(1));
  reset();for(int i=0;i<4;++i)handleHaSettingsSave(&post);
  handleHaSettingsSave(&post);assert(post.status==503);
  reset();handleHaSettingsSave(&post);sessionValid=false;step("expired");
  assert(writes.empty() && restConfigOperations.find(1)->status==voxone::rest_config::Status::Cancelled);
  reset();handleHaSettingsSave(&post);++generation;step("revoked");assert(writes.empty());
  reset();handleHaSettingsSave(&post);id=restConfigOperations.find(1)->id;
  get.parameter.data=id;++generation;handleConfigOperationRead(&get);assert(get.status==404);
  reset();post.has=false;handleHaSettingsSave(&post);assert(post.status==400 && tone.entries.empty());post.has=true;
  reset();post.parameter.data=std::string(33,'x');handleHaSettingsSave(&post);assert(post.status==400);
  reset();post.parameter.data="invalid";handleDisplayThemeSave(&post);assert(post.status==400);
  reset();post.parameter.data="Queued";handleHaSettingsSave(&post);securityBusy=true;step("busy");
  assert(writes.empty() && tone.entries.size()==1);securityBusy=false;step("ha");assert(writes.size()==1);
  reset();handleHaSettingsSave(&post);
  {voxone::configuration_mutation::Lease busy(voxone::configuration_mutation::deviceCoordinator(),
       voxone::configuration_mutation::Owner::ConfigBackup);
   step("busy");assert(writes.empty() && tone.entries.size()==1);}
  step("ha");assert(writes.size()==1);
  reset();post.allowed=false;handleHaSettingsSave(&post);assert(post.status==403 && tone.entries.empty());post.allowed=true;
  reset();{AsyncWebServerRequest disconnected;disconnected.parameter.data="Detached";
    handleHaSettingsSave(&disconnected);assert(disconnected.status==202);}
  // Request and its input buffer destroyed before execution (ASan use-after-scope).
  step("disconnected");assert(writes.size()==1);
  reset();post.parameter.data="Timeout";handleHaSettingsSave(&post);testNow+=30000;step("timeout");
  assert(writes.empty() && restConfigOperations.find(1)->status==voxone::rest_config::Status::Cancelled);
  reset();configurationCommandSequence=0xfffffffeU;send("bass=2");
  handleHaSettingsSave(&post);send("scrollsettings=true,normal,1000");send("balance=2");
  step("bass");step("rest");step("scroll");step("balance");
  assert((writes==std::vector<std::string>{"bass","rest","scroll","balance"}));
  // Production power POST -> shared FIFO -> actual Config setter, never NVS in callback.
  for(const char* mode:{"soft_standby","hard_power","mute_only"}) {
    reset();post.parameter.data=mode;
    callback=true;handlePowerSettingsSave(&post);callback=false;
    assert(post.status==202 && writes.empty());
    get.parameter.data=restConfigOperations.find(1)->id;
    handleConfigOperationRead(&get);assert(get.body.find("pending")!=std::string::npos);
    step("power");handleConfigOperationRead(&get);
    assert(get.body.find("saved")!=std::string::npos);
    assert(std::string(voxone::power_mode::apiName(config.store.powerMode))==mode);
    callback=true;handlePowerSettingsRead(&get);callback=false;
    assert(get.status==200 && get.body==std::string("{\"mode\":\"")+mode+
      "\",\"standby\":false,\"buttonPin\":42,\"relayAvailable\":false}");
  }
  reset();post.parameter.data="hard_power";storageOk=false;
  handlePowerSettingsSave(&post);step("power-fail");
  assert(config.store.powerMode==MODE_HARD_POWER && writes.size()==1);
  get.parameter.data=restConfigOperations.find(1)->id;handleConfigOperationRead(&get);
  assert(get.body.find("applied_but_not_persisted")!=std::string::npos);
  handlePowerSettingsSave(&post);step("power-fail-again");assert(writes.size()==2);
  storageOk=true;handlePowerSettingsSave(&post);step("power-retry");assert(writes.size()==3);
  assert(restConfigOperations.find(3)->status==voxone::rest_config::Status::Saved);
  handlePowerSettingsSave(&post);step("power-noop");assert(writes.size()==3);
  reset();for(int i=0;i<4;++i)send("bass=1");handlePowerSettingsSave(&post);
  assert(post.status==503 && !restConfigOperations.find(1));
  reset();for(int i=0;i<4;++i)handlePowerSettingsSave(&post);
  handlePowerSettingsSave(&post);assert(post.status==503);
  reset();post.has=false;handlePowerSettingsSave(&post);assert(post.status==400);post.has=true;
  reset();post.parameter.data="invalid";handlePowerSettingsSave(&post);assert(post.status==400 && writes.empty());
  reset();post.parameter.data="mute_only";post.allowed=false;
  handlePowerSettingsSave(&post);assert(post.status==403 && tone.entries.empty());post.allowed=true;
  reset();sessionValid=false;handlePowerSettingsSave(&post);assert(post.status==401 && tone.entries.empty());
  reset();handlePowerSettingsSave(&post);sessionValid=false;step("power-expired");
  assert(writes.empty() && restConfigOperations.find(1)->status==voxone::rest_config::Status::Cancelled);
  reset();handlePowerSettingsSave(&post);++generation;step("power-revoked");
  assert(writes.empty() && restConfigOperations.find(1)->status==voxone::rest_config::Status::Cancelled);
  get.parameter.data=restConfigOperations.find(1)->id;handleConfigOperationRead(&get);assert(get.status==404);
  reset();handlePowerSettingsSave(&post);testNow+=30000;step("power-timeout");
  assert(writes.empty() && restConfigOperations.find(1)->status==voxone::rest_config::Status::Cancelled);
  reset();{AsyncWebServerRequest disconnected;disconnected.parameter.data="hard_power";
    handlePowerSettingsSave(&disconnected);assert(disconnected.status==202);}
  step("power-detached");assert(writes.size()==1);
  reset();handlePowerSettingsSave(&post);
  std::strcpy(restConfigOperations.find(1)->value,"invalid");step("power-invalid-execution");
  assert(writes.empty() && restConfigOperations.find(1)->status==voxone::rest_config::Status::Failed);
  reset();send("bass=1");post.parameter.data="Mixed";handleHaSettingsSave(&post);
  post.parameter.data="hard_power";handlePowerSettingsSave(&post);
  post.parameter.data="yamaha";handleDisplayThemeSave(&post);
  step("tone");step("ha");step("power");step("theme");
  assert((writes==std::vector<std::string>{"tone","ha","power","theme"}));
  std::cout<<"PASS production WS/REST handlers (HA/theme/power), FIFO, consumer and setters; fake RTOS/HTTP/NVS\n"
           <<"Result store bytes="<<sizeof(restConfigOperations)<<" queue item bytes="<<sizeof(QueuedToneCommand)<<"\n";
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    args = parser.parse_args()
    server = (ROOT / "src/core/netserver.cpp").read_text()
    config = (ROOT / "src/core/config.cpp").read_text()
    config_h = (ROOT / "src/core/config.h").read_text()
    handler = (ROOT / "src/core/commandhandler.cpp").read_text()
    source = PREFIX + "template<typename T>\n" + extract_function(config_h, "bool saveValue(T *field") + MIDDLE
    for name in ("bool Config::setPowerMode", "bool Config::setHaName", "bool Config::setTftTheme", "bool Config::setTone", "bool Config::setBalance", "bool Config::setStartupMode", "bool Config::setStartupFixedVolume",
                 "bool Config::setLineOutEnabled", "bool Config::setLineOutVolume", "bool Config::setVuMeter"):
        source += extract_function(config, name) + "\n"
    for name in ("static bool parseVolumeValue", "static bool parseTimeInterval", "bool CommandHandler::exec"):
        source += extract_function(handler, name) + "\n"
    # Copy production layouts, then initialize fake queue/mutex handles.
    start = server.index("struct QueuedScrollSettingsCommand")
    end = server.index("voxone::netserver_backpressure::InsertResult reserveSnapshot", start)
    source += server[start:end].replace("configurationMutationMutex = nullptr", "configurationMutationMutex = &mutationHeld")
    for enum in ("ScrollCommandQueueStatus", "ToneCommandQueueStatus"):
        start = server.index("enum class " + enum)
        source += server[start:server.index("};", start)+2] + "\n"
    for name in ("static bool parseToneValue", "bool equalSecurityBinding", "ScrollCommandQueueStatus enqueueScrollSettingsCommand",
                 "WebSocketAuthorization queuedScrollCommandAuthorization", "ToneCommandQueueStatus enqueueToneCommand",
                 "WebSocketAuthorization queuedToneCommandAuthorization", "void sendScrollSettingsResult", "void sendConfigResult",
                 "void clearSensitive(char* value, size_t size) {",
                 "bool captureRestConfigOwner", "void enqueueRestConfig", "void handleConfigOperationRead(AsyncWebServerRequest* request) {", "bool executeRestConfig",
                 "void handleHaSettingsSave(AsyncWebServerRequest* request) {", "void handleDisplayThemeSave(AsyncWebServerRequest* request) {",
                 "void handlePowerSettingsSave(AsyncWebServerRequest* request) {", "void handlePowerSettingsRead(AsyncWebServerRequest* request) {",
                 "void NetServer::serviceScrollSettings", "void NetServer::onWsMessage"):
        source += extract_function(server, name) + "\n"
    source += TESTS
    with tempfile.TemporaryDirectory(prefix="voxone-ws-config-") as work:
        cpp, binary = Path(work) / "harness.cpp", Path(work) / "harness"
        cpp.write_text(source)
        flags = ["-std=c++11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter"]
        if args.sanitize:
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"]
        subprocess.run([args.cxx, *flags, "-I", str(ROOT), str(cpp),
                        str(ROOT / "src/core/websocket_command.cpp"), str(ROOT / "src/core/display_scroll.cpp"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], cwd=work, check=True)


if __name__ == "__main__":
    main()
