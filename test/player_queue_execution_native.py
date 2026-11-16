#!/usr/bin/env python3
"""Execute unmodified Player admission/consumer/STOP/reconnect methods.

FreeRTOS queues/portMUX are bounded native fakes; NVS/audio/socket/UI are
adapters. SourceManagerState, ConnectAttemptLifetime and ReconnectPolicy are
production code. This is not a full firmware or physical timing test.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from hls_ts_transport_native import extract_function

ROOT = Path(__file__).resolve().parents[1]
PREFIX = r'''
#include "src/core/source_manager_state.h"
#include "src/core/source_media_state.h"
#include "src/core/source_media_request.h"
#include "src/core/player_station_navigation.h"
#include "src/core/player_pending_refresh.h"
#include "src/core/radio_connect_lifetime.h"
#include "src/core/radio_reconnect_policy.h"
#include "src/core/radio_source_policy.h"
#include <atomic>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <vector>
#include <mutex>
#include <thread>
#include <functional>
#include <iostream>
using UBaseType_t=unsigned;
constexpr int pdTRUE=1,pdPASS=1;
using portMUX_TYPE=std::recursive_mutex;
static portMUX_TYPE playerCommandMux;
static thread_local bool producer=false;
constexpr int PL_QUEUE_TICKS_ST=15;
static void vTaskDelay(int ticks){assert(!producer&&ticks==PL_QUEUE_TICKS_ST);}
#define portENTER_CRITICAL(m) (m)->lock()
#define portEXIT_CRITICAL(m) (m)->unlock()
#define log_i(...) ((void)0)
struct Queue {
  Queue(size_t n,size_t b):capacity(n),bytes(b){}
  size_t capacity,bytes;std::deque<std::vector<unsigned char>> items;
  std::mutex mutex;
};
using QueueHandle_t=Queue*;
static QueueHandle_t playerQueue=nullptr;
static int xQueueSend(Queue* q,const void* data,unsigned wait){
  std::lock_guard<std::mutex> guard(q->mutex);
  assert(wait==0);if(q->items.size()==q->capacity)return 0;
  const auto* b=static_cast<const unsigned char*>(data);q->items.emplace_back(b,b+q->bytes);return 1;
}
static int xQueueReceive(Queue* q,void* out,unsigned wait){
  std::lock_guard<std::mutex> guard(q->mutex);
  assert(wait==0);if(q->items.empty())return 0;
  memcpy(out,q->items.front().data(),q->bytes);q->items.pop_front();return 1;
}
static unsigned uxQueueMessagesWaiting(Queue* q){std::lock_guard<std::mutex> guard(q->mutex);return q->items.size();}
static void xQueueReset(Queue* q){std::lock_guard<std::mutex> guard(q->mutex);q->items.clear();}
static bool blocked=false;
static bool systemUpdateAudioBlocked(){return blocked;}
static uint32_t nowMs=0;
static uint32_t millis(){return nowMs;}
static SourceManagerState sources;
static std::recursive_mutex sourceMux;
static void sourceManagerRadioCommandQueued(bool play,uint16_t station=0){
  std::lock_guard<std::recursive_mutex> guard(sourceMux);sources.recordRadioCommand(play,station);
}
static void sourceManagerRadioPlayConsumed(){std::lock_guard<std::recursive_mutex> guard(sourceMux);sources.radioPlayConsumed();}
static void sourceManagerRadioStopConsumed(){std::lock_guard<std::recursive_mutex> guard(sourceMux);sources.radioStopConsumed();}
static bool sourceManagerRadioPlayIntent(){std::lock_guard<std::recursive_mutex> guard(sourceMux);return sources.radioPlayIntent();}
static bool sourceManagerRadioResumeAllowed(){std::lock_guard<std::recursive_mutex> guard(sourceMux);return sources.radioResumeAllowed();}
static void sourceManagerPlaylistSelectionCleared(){sources.playlistSelectionCleared();}
enum class SourceId {Web,Local,HomeAssistant,Dlna};
static SourceId sourceIdForMedia(voxone::source_media::Source s){return s==voxone::source_media::Source::Local?SourceId::Local:SourceId::HomeAssistant;}
static bool sourceManagerSourceAvailable(SourceId){return true;}
static unsigned returns=0;
static void sourceManagerReturnAfterUserMediaStop(SourceId){++returns;}
namespace voxone { namespace sd_path {static bool audioFile(const char*){return true;}}
namespace line_out {template<class T>static int effectiveRawVolume(T){return 1;}} }
struct VolumeState {uint8_t raw,user;};
static VolumeState volumeStateFromRaw(uint8_t raw,int){return {raw,raw};}
static int constrain(int n,int low,int high){return n<low?low:n>high?high:n;}
static std::function<void()> duringPrepare,duringConnect,duringStop;
struct Config {
  uint16_t selected=1,count=5;
  struct Runtime {bool lineOutEnabled=false;int maximumVolume=100;};
  struct Station {const char* url="http://fixture/";} station;
  char tmpBuf[256]{};
  Runtime runtimeSnapshot(){return {};}
  uint16_t lastStation(){return selected;}
  uint16_t playlistLength(){assert(!producer);return count;}
  void setLastStation(uint16_t n){assert(!producer);selected=n;}
  bool prepareForPlaying(uint16_t,bool){assert(!producer);if(duringPrepare)duringPrepare();return true;}
  void configPostPlaying(){assert(!producer);}
  void setVolumeState(int,int){assert(!producer);}
  void reduceVuThreshold(int){assert(!producer);}
} config;
struct Network {bool lostPlaying=false;bool ready(){return true;}} network;
struct Serial {template<class...T>void printf(const char*,T...){assert(!producer);}} serialCli;
static void (*player_on_station_change)()=nullptr;
static unsigned allocations=0,releases=0,connects=0,stops=0;
struct QueuedSourceMediaRequest { int marker; };
static QueuedSourceMediaRequest* copySourceMediaRequest(const voxone::source_media::Request&){++allocations;return new QueuedSourceMediaRequest{1};}
static void releaseSourceMediaRequest(QueuedSourceMediaRequest* p){if(p){++releases;delete p;}}
struct Audio {
  std::atomic<bool> m_connectionStopRequested{false};
  voxone::radio::ConnectAttemptLifetime m_connectAttempt;
  void cancelPendingConnection(){m_connectAttempt.requestCancel();}
  void setConnectionStopRequested(bool);
  bool connectionStopRequested()const;
  void setVolume(int){assert(!producer);}
  void setDefaults(){assert(!producer);}
  void armPcmOutput(){assert(!producer);}
  void cancelPcmOutput(){assert(!producer);}
  bool connecttohost(const char*,const char* = "",const char* = "");
  bool connectToHost(const char*,const char*,const char*,bool){
    assert(!producer);++connects;
    if(connectionStopRequested())return false;
    assert(m_connectAttempt.begin());
    if(duringConnect)duringConnect();
    if(connectionStopRequested())cancelPendingConnection();
    m_connectAttempt.complete(true,[](){},[](){});
    return m_connectAttempt.succeeded()&&!connectionStopRequested();
  }
};
'''
CLASS = r'''
struct Player:Audio {
  bool _localAudioAvailable=true,_stopPending=false;
  int _stopPayload=PR_STOP_KEEP_STATION;
  std::atomic<uint32_t> _rejectedCommands{0};
  PlayerPendingRefresh _pendingStationRefresh;
  QueueHandle_t _sourceMediaUrls=nullptr;
  SourceMediaState _sourceMedia;
  SourceId _sourceMediaPrevious=SourceId::Web;
  bool _sourceMediaReturnPending=false,_sourceMediaReturnToPrevious=false;
  voxone::radio::ReconnectPolicy _radioReconnect;
  bool _hasError=false,remoteStationName=false,_pcmOutputPending=false,_volTimer=false;
  std::atomic<bool> connproc{true};
  uint32_t _volTicks=0;
  std::atomic<plStatus_e> _status{STOPPED};
  struct Mute {int outputVolume(int n){return n;}} _mute;
  bool sendCommand(playerRequestParams_t);
  void resetQueue();void clearSourceMediaRequests();
  bool processPlayerCommands();void _applyStop(int,bool);
  void _play(uint16_t,bool=false);
  void scheduleRadioReconnect();void serviceRadioReconnect();
  bool requestSourceMedia(const voxone::source_media::Request&);
  bool next();bool prev();bool toggle();
  bool sourceMediaBusy(){return _sourceMedia.busy();}
  bool sourceMediaActive(){return _sourceMedia.active();}
  bool isRunning(){return _status==PLAYING;}
  bool suppressSourceMediaReturn(){bool p=_sourceMediaReturnPending||_sourceMediaReturnToPrevious;_sourceMediaReturnPending=_sourceMediaReturnToPrevious=false;return p;}
  void finishSourceMedia(){assert(!producer);_sourceMedia.finish(_sourceMedia.token());_status=STOPPED;}
  void _stop(bool=false,RadioStopReason=RadioStopReason::Normal){
    assert(!producer);++stops;_status=STOPPED;
    if(duringStop){auto once=duringStop;duringStop={};once();}
  }
  void _clearStation(bool){assert(!producer);config.selected=0;}
  void stopInfo(){assert(!producer);}
  bool _refreshStation(uint16_t){return true;}
  int volToI2S(int n){return n;}
  void _loadVol(int){assert(!producer);}
  void setOutputPins(bool){assert(!producer);}
  void setError(){assert(!producer);}
};
'''
TESTS = r'''
int main(){
  Queue commands{5,sizeof(playerRequestParams_t)},urls{1,sizeof(QueuedSourceMediaRequest*)};
  playerQueue=&commands;Player p;p._sourceMediaUrls=&urls;
  auto send=[&](playerRequestType_e type,int n=0){producer=true;bool ok=p.sendCommand({type,n});producer=false;return ok;};
  auto drain=[&](){for(int i=0;i<12&&(p._stopPending||!commands.items.empty());++i)p.processPlayerCommands();assert(!p._stopPending&&commands.items.empty());};
  assert(send(PR_PLAY,2));drain();assert(p._status==PLAYING&&config.selected==2);
  assert(send(PR_STOP));assert(p.connectionStopRequested());drain();assert(p._status==STOPPED);
  // A full queue of non-transport commands cannot prevent STOP, or lose data.
  for(int i=0;i<5;++i)assert(send(PR_VOL,i));
  const auto rejected=p._rejectedCommands.load();
  assert(!send(PR_NEXT));assert(p._rejectedCommands==rejected+1);
  assert(!sources.radioPlayIntent());
  assert(send(PR_STOP));assert(commands.items.size()==5);drain();
  // Old starts are discarded, and a new explicit PLAY after STOP is retained.
  for(int i=0;i<5;++i)assert(send(PR_PLAY,i+1));
  assert(send(PR_STOP));assert(commands.items.empty());
  auto before=connects;drain();assert(connects==before);
  assert(send(PR_PLAY,4));assert(send(PR_STOP));assert(send(PR_PLAY,3));
  p.processPlayerCommands();assert(p._status==STOPPED&&commands.items.size()==1);
  drain();assert(p._status==PLAYING&&config.selected==3);
  assert(send(PR_PLAY,1));assert(send(PR_PLAY,2));p.processPlayerCommands();
  assert(sources.radioStationForResume(config.selected)==2);
  // This is the reset/resume sequence used on a source change. Its snapshot
  // must still contain the newest accepted absolute station, not station 1.
  const auto resume=sources.radioStationForResume(config.selected);p.resetQueue();
  assert(send(PR_RADIO_RESUME,resume));drain();assert(config.selected==2);
  // NEXT/PREV resolve at the real FIFO consumer, including wrap and empty list.
  config.selected=4;
  for(auto c:{PR_NEXT,PR_NEXT,PR_PREV,PR_NEXT,PR_PREV})assert(send(c));
  assert(!send(PR_NEXT));drain();assert(config.selected==5);
  config.count=0;before=connects;producer=true;
  assert(p.next()&&p.prev());producer=false;drain();
  assert(connects==before&&!sources.radioPlayIntent());
  assert(send(PR_PLAY,1));drain();assert(connects==before&&!sources.radioPlayIntent());
  config.count=5;
  // STOP during Config preparation (before connect begins) and ConnectTask.
  duringPrepare=[&](){assert(send(PR_STOP));};before=connects;
  assert(send(PR_PLAY,1));p.processPlayerCommands();assert(connects==before);
  duringPrepare={};drain();assert(p._status==STOPPED);
  duringConnect=[&](){std::thread t([&](){assert(send(PR_STOP));});t.join();};
  assert(send(PR_PLAY,1));p.processPlayerCommands();duringConnect={};
  assert(!p.m_connectAttempt.succeeded());drain();assert(p._status==STOPPED);
  // Reconnect cannot race a pending STOP or overtake an explicit FIFO command.
  assert(send(PR_PLAY,1));drain();p.scheduleRadioReconnect();assert(p._radioReconnect.pending());
  nowMs+=60000;assert(send(PR_STOP));before=connects;p.serviceRadioReconnect();
  assert(connects==before);drain();assert(!p._radioReconnect.pending());
  p.scheduleRadioReconnect();assert(!p._radioReconnect.pending());
  // Source-change queue reset must not erase STOP, including repeated STOPs.
  assert(send(PR_STOP));p.resetQueue();assert(p._stopPending);drain();
  duringStop=[&](){std::thread t([&](){
    producer=true;assert(p.sendCommand({PR_PLAY,4}));assert(p.sendCommand({PR_STOP,0}));producer=false;
  });t.join();};
  assert(send(PR_STOP));before=connects;p.processPlayerCommands();
  assert(p._stopPending&&p.connectionStopRequested());drain();assert(connects==before);
  assert(send(PR_STOP,-1));assert(send(PR_STOP,-2));assert(send(PR_STOP,0));
  assert(p._stopPayload==-2);drain();assert(config.selected==0);
  // Mailbox ownership: rejected wakeup must not publish; STOP removes older
  // media but cleanup cannot delete a request admitted after that STOP.
  voxone::source_media::Request media("http://fixture/audio", "", "",
      voxone::source_media::Source::HomeAssistant, false);
  for(int i=0;i<5;++i)assert(send(PR_VOL,i));
  assert(!p.requestSourceMedia(media));assert(urls.items.empty());drain();
  assert(p.requestSourceMedia(media));assert(urls.items.size()==1);
  // Releasing a copied URL is safe in producer context; never transport/NVS.
  assert(p.sendCommand({PR_STOP,0}));assert(urls.items.empty());
  assert(p.requestSourceMedia(media));p.processPlayerCommands();assert(urls.items.size()==1);
  p.clearSourceMediaRequests();drain();assert(allocations==releases);
  p._sourceMedia.begin();p._sourceMediaReturnToPrevious=true;
  sources.recordRadioCommand(true,2);assert(send(PR_STOP));drain();
  assert(returns==1&&sources.radioPlayIntent()); // preserved media return contract
  // Multiple producers execute the production admission path under the native
  // portMUX adapter. Exactly five accepted commands, no silent overflow.
  std::atomic<unsigned> accepted{0},denied{0};std::vector<std::thread> threads;
  for(int n=0;n<8;++n)threads.emplace_back([&](){for(int i=0;i<100;++i){producer=true;
    if(p.sendCommand({PR_NEXT,0}))++accepted;else ++denied;producer=false;}});
  for(auto& t:threads){t.join();}assert(accepted==5&&denied==795);drain();
  // Concurrent consumer + producers: every accepted NEXT executes once.
  accepted=0;denied=0;std::atomic<unsigned> done{0};threads.clear();
  const unsigned connectionsBefore=connects;
  for(int n=0;n<4;++n)threads.emplace_back([&](){
    for(int i=0;i<100;++i){producer=true;
      if(p.sendCommand({PR_NEXT,0}))++accepted;else ++denied;
      producer=false;std::this_thread::yield();}
    ++done;
  });
  while(done!=4){p.processPlayerCommands();std::this_thread::yield();}
  for(auto& t:threads){t.join();}drain();
  assert(accepted+denied==400&&connects-connectionsBefore==accepted);
  threads.clear();for(int n=0;n<8;++n)threads.emplace_back([&](){producer=true;assert(p.sendCommand({PR_STOP,0}));producer=false;});
  for(auto& t:threads){t.join();}drain();assert(p._status==STOPPED&&!sources.radioPlayIntent());
  blocked=true;assert(!send(PR_PLAY,1));assert(!send(PR_STOP));blocked=false;
  std::cout<<"PASS production Player admission/consumer/STOP/reconnect/navigation/media mailbox; native queue/socket/NVS adapters\n";
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    header = (ROOT / 'src/core/player.h').read_text()
    source = (ROOT / 'src/core/player.cpp').read_text()
    audio = (ROOT / 'src/audioI2S/AudioEx.h').read_text()
    audio_source = (ROOT / 'src/audioI2S/Audio.cpp').read_text()
    types = header[header.index('enum playerRequestType_e'):header.index('struct PlayerRuntimeVolumeSnapshot')]
    code = PREFIX + types + CLASS
    for signature in ('void setConnectionStopRequested', 'bool connectionStopRequested'):
        body = extract_function(audio, signature)
        code += body.replace(signature, signature.split()[0] + ' Audio::' + signature.split()[1], 1)
    code += extract_function(source, 'bool startsPlayerTransport')
    code += extract_function(source, 'void consumeRadioPlayUnlessQueued')
    code += extract_function(audio_source, 'bool Audio::connecttohost')
    for name in ('bool Player::sendCommand', 'void Player::resetQueue',
                 'void Player::clearSourceMediaRequests', 'void Player::_applyStop',
                 'bool Player::processPlayerCommands', 'void Player::_play',
                 'void Player::scheduleRadioReconnect', 'void Player::serviceRadioReconnect',
                 'bool Player::requestSourceMedia', 'bool Player::prev', 'bool Player::next',
                 'bool Player::toggle'):
        code += extract_function(source, name) + '\n'
    code += TESTS
    with tempfile.TemporaryDirectory(prefix='voxone-player-queue-') as tmp:
        cpp = Path(tmp) / 'test.cpp'
        binary = Path(tmp) / 'test'
        cpp.write_text(code)
        flags = ['-std=c++11', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
        subprocess.run([args.cxx, *flags, '-I', str(ROOT), str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
