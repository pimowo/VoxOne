#!/usr/bin/env python3
"""Execute production Audio setters, block/sample processing, IIR and Gain.

Only the ESP32/FreeRTOS/I2S/transport environment is replaced. The test extracts
method bodies unchanged; it is not a decoder, physical timing or audible test.
--thread-sanitize checks host races separately from --sanitize (ASan/UBSan).
"""
import argparse
from pathlib import Path
import subprocess
import re
import sys
import tempfile

sys.dont_write_bytecode = True
from hls_ts_transport_native import extract_function

ROOT = Path(__file__).resolve().parents[1]
PREFIX = r'''
#include "src/core/audio_balance.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
constexpr double PI=3.1415926535897932384626433832795;
constexpr int portMAX_DELAY=-1, kI2SOutputLockWait=100, ESP_OK=0;
using esp_err_t=int; using i2s_port_t=int;
static thread_local bool producer=false;
static thread_local unsigned audioDepth=0, outputDepth=0;
using SemaphoreHandle_t=std::recursive_mutex*;
static void xSemaphoreTakeRecursive(SemaphoreHandle_t m,int){
  assert(!producer && !outputDepth);m->lock();++audioDepth;
}
static void xSemaphoreGiveRecursive(SemaphoreHandle_t m){--audioDepth;m->unlock();}
namespace voxone { namespace i2s_output {
static std::recursive_mutex mutex;
struct Guard {
  explicit Guard(int){assert(!producer);mutex.lock();++outputDepth;}
  ~Guard(){--outputDepth;mutex.unlock();}
  explicit operator bool()const{return true;}
};
} }
static std::atomic<bool> radioEnabled{true};
static bool radioCanUseI2S(){return radioEnabled.load();}
static std::vector<int32_t> pcm;
static std::function<void()> onWrite;
static int i2s_set_sample_rates(int,uint32_t){assert(!producer && outputDepth);return ESP_OK;}
static int i2s_zero_dma_buffer(int){assert(audioDepth && outputDepth);return ESP_OK;}
static int i2s_write(int,const char* data,size_t length,size_t* written,int){
  assert(!producer && audioDepth && outputDepth && length==4);
  int32_t sample;memcpy(&sample,data,4);pcm.push_back(sample);
  if(onWrite)onWrite();
  *written=length;return ESP_OK;
}
#define log_e(...) ((void)0)
struct Audio {
  enum {LEFTCHANNEL=0,RIGHTCHANNEL=1,LOWSHELF=0,PEAKEQ=1,HIFGSHELF=2};
  struct filter_t{float a0,a1,a2,b1,b2;};
  filter_t m_filter[3]{};
  float m_filterBuff[3][2][2][2]{};
  // Parameter declarations below are taken directly from AudioEx.h.
'''
CLASS = r'''
  std::recursive_mutex mutex;
  SemaphoreHandle_t m_audioTaskMutex=&mutex;
  bool m_f_running=true,m_audioDecodeReady=true;
  std::atomic<uint32_t> m_decodedSampleRate{44100};
  bool m_f_internalDAC=false,m_f_forceMono=false;
  uint8_t m_i2s_num=0,m_bitsPerSample=16,m_channels=2;
  size_t m_i2s_bytesWritten=0;
  int16_t m_outBuff[8192]{},m_validSamples=0,m_curSample=0;
  struct {int dma_buf_len=8;} m_i2s_config;
  unsigned cancellations=0;
  void cancelPendingConnection(){++cancellations;}
  void cancelPcmOutput(){}
  void _computeVUlevel(int16_t*){}
  void playAudioData(){playChunk();}
  bool setSampleRate(uint32_t);uint32_t getSampleRate();
  void setTone(int8_t,int8_t,int8_t);void setBalance(int8_t);void setVolume(uint8_t);
  void applyPendingAudioParameters();void performAudioTask();
  bool playChunk();bool playSample(int16_t[2]);void playI2Sremains();
  uint32_t stopSong();
  uint8_t getBitsPerSample();uint8_t getChannels();bool setChannels(int);
  int32_t Gain(int16_t[2]);
  void IIR_calculateCoefficients(int8_t,int8_t,int8_t,uint32_t);
  int16_t* IIR_filterChain0(int16_t[2],bool=false);
  int16_t* IIR_filterChain1(int16_t[2],bool=false);
  int16_t* IIR_filterChain2(int16_t[2],bool=false);
  void block(int samples=32){
    for(int i=0;i<samples*2;++i)m_outBuff[i]=(i%31-15)*8;
    m_validSamples=samples;m_curSample=0;performAudioTask();
  }
};
'''
TESTS = r'''
static uint32_t pack(int a,int b,int c){return (a+128)|((b+128)<<8)|((c+128)<<16);}
static void verifyBank(Audio& a){
  Audio expected;
  auto t=a.m_appliedTone;
  expected.IIR_calculateCoefficients(int(t&255)-128,int((t>>8)&255)-128,
                                    int((t>>16)&255)-128,a.m_filterSampleRate);
  assert(memcmp(a.m_filter,expected.m_filter,sizeof a.m_filter)==0);
  for(auto f:a.m_filter)assert(std::isfinite(f.a0)&&std::isfinite(f.a1)&&
                             std::isfinite(f.a2)&&std::isfinite(f.b1)&&std::isfinite(f.b2));
}
int main(){
  Audio a;
  // Golden banks evaluated from the pre-change 6bc7984 implementation. These
  // also cover sample-rate dependence, shelf cut and peak boost (not an oracle
  // made by calling the implementation under test with itself).
  const float golden[4][15]={
    {0.982866228f,-1.92207193f,0.940154493f,-1.92104602f,0.924046695f,1.06433868f,-0.646001458f,0.62374419f,-0.646001458f,0.688082933f,0.874575198f,0.824557483f,0.291525096f,0.724674404f,0.265983343f},
    {0.993747234f,-1.97147644f,0.977857172f,-1.97133803f,0.971742928f,1.03158355f,-1.68072748f,0.815298557f,-1.68072748f,0.846881986f,0.711262584f,-0.608833313f,0.216373414f,-1.05816853f,0.376971334f},
    {0.994253874f,-1.97378337f,0.979637802f,-1.97366631f,0.974008679f,1.02932954f,-1.71639204f,0.828479946f,-1.71639204f,0.857809484f,0.70465529f,-0.664355338f,0.234885067f,-1.13340914f,0.40859431f},
    {0.997122586f,-1.98686182f,0.989766419f,-1.98683226f,0.986918509f,1.01549196f,-1.88790822f,0.90940243f,-1.88790822f,0.924894333f,0.667578161f,-0.970821202f,0.383231193f,-1.56204021f,0.642028391f}
  };
  unsigned bank=0;
  for(auto rate:{16000u,44100u,48000u,96000u}){
    a.IIR_calculateCoefficients(-10,3,-4,rate);
    float actual[15];static_assert(sizeof actual==sizeof a.m_filter,"filter layout");
    memcpy(actual,a.m_filter,sizeof actual);
    for(unsigned j=0;j<15;++j)assert(std::fabs(actual[j]-golden[bank][j])<0.000002f);
    ++bank;
  }
  assert(a.m_requestedTone.is_lock_free()&&a.m_requestedBalance.is_lock_free());
  assert(a.setSampleRate(44100));
  a.setVolume(254);a.block();
  Audio::filter_t before[3];memcpy(before,a.m_filter,sizeof before);
  float history[3][2][2][2];memcpy(history,a.m_filterBuff,sizeof history);
  producer=true;a.setTone(-10,3,-4);a.setBalance(8);producer=false;
  assert(memcmp(before,a.m_filter,sizeof before)==0);
  assert(memcmp(history,a.m_filterBuff,sizeof history)==0 && a.m_balance==0);
  a.m_validSamples=0;a.performAudioTask(); // real block boundary, no samples
  assert(a.m_appliedTone==pack(-10,3,-4)&&a.m_balance==8);
  verifyBank(a);assert(memcmp(history,a.m_filterBuff,sizeof history)==0);
  // A setter invoked while PCM is being written cannot alter this block.
  memcpy(before,a.m_filter,sizeof before);
  onWrite=[&]{
    producer=true;a.setTone(6,-40,0);a.setBalance(-16);producer=false;
    assert(memcmp(before,a.m_filter,sizeof before)==0 && a.m_balance==8);
  };
  a.block(128);onWrite=nullptr;
  assert(a.m_appliedTone==pack(-10,3,-4));
  a.block();assert(a.m_appliedTone==pack(6,-40,0)&&a.m_balance==-16);verifyBank(a);
  // No-op publications do not rebuild or clear filter state.
  memcpy(before,a.m_filter,sizeof before);memcpy(history,a.m_filterBuff,sizeof history);
  a.setTone(6,-40,0);a.m_validSamples=0;a.performAudioTask();
  assert(memcmp(before,a.m_filter,sizeof before)==0);
  assert(memcmp(history,a.m_filterBuff,sizeof history)==0);
  // Clamp extremes and every Balance value; production Gain is used.
  for(int bal=-128;bal<=127;++bal){
    producer=true;a.setBalance(static_cast<int8_t>(bal));producer=false;
    a.m_validSamples=0;a.performAudioTask();
    assert(a.m_balance==(bal<-16?-16:bal>16?16:bal));
    int16_t sample[2]={1234,-1234};
    auto g=voxone::audio_balance::calculateStereoGain(254,a.m_balance);
    assert(a.Gain(sample)==voxone::audio_balance::applyStereoGain(sample[0],sample[1],g));
  }
  for(int gain=-128;gain<=127;++gain){
    a.setTone(gain,gain,gain);a.m_validSamples=0;a.performAudioTask();verifyBank(a);
    Audio clipped;int g=gain<-40?-40:gain>6?6:gain;
    clipped.IIR_calculateCoefficients(g,g,g,44100);
    assert(memcmp(a.m_filter,clipped.m_filter,sizeof a.m_filter)==0);
  }
  // Sequential accepted presets, then coalescing while the consumer is idle.
  for(int i=0;i<200;++i){a.setTone(i%47-40,6-i%47,0);a.block(1);
    assert(a.m_appliedTone==pack(i%47-40,6-i%47,0));verifyBank(a);}
  for(int i=0;i<200;++i)a.setTone(i%47-40,6-i%47,0);
  a.block(1);assert(a.m_appliedTone==pack(199%47-40,6-199%47,0));
  // STOP/source suspension preserves requested controls and does not use NVS.
  a.stopSong();assert(!a.m_f_running && a.cancellations==1);
  radioEnabled=false;a.setTone(-8,0,5);a.setBalance(4);a.block();
  assert(a.m_appliedTone!=pack(-8,0,5));
  radioEnabled=true;assert(a.setSampleRate(48000));a.block();
  assert(a.m_appliedTone==pack(-8,0,5)&&a.m_balance==4&&a.m_filterSampleRate==48000);
  verifyBank(a);
  for(auto rate:{16000u,22050u,44100u,48000u,96000u}){
    a.setSampleRate(rate);a.block(1);verifyBank(a);assert(a.m_filterSampleRate==rate);
  }
  assert(a.setSampleRate(0)&&a.getSampleRate()==16000);
  // Golden PCM from 6bc7984, including existing float/int quantization (flat
  // biquads are not mathematically exact identity in finite precision).
  a.setSampleRate(44100);a.setTone(0,0,0);a.setBalance(0);a.setVolume(128);
  memset(a.m_filterBuff,0,sizeof a.m_filterBuff);pcm.clear();a.block(32);
  const int32_t goldenPcm[]={-1900572,-1638424,-1376276,-1114128,-851980,-589832,-327684,-131072,65539,327687,589835,851983,1114131,1376279,1638427,1966050,-1769498,-1441814,-1245202,-983054,-720906,-458758,-196610,0,131077,458761,720909,983057,1245205,1507353,1769501,-1900571};
  assert(pcm.size()==32);
  for(int i=0;i<32;++i)assert(pcm[i]==goldenPcm[i]);
  // Many control producers; consumer must see a complete published triple.
  a.setTone(-10,2,-4);
  std::atomic<bool> go{false};
  auto writer=[&](bool alternate){producer=true;while(!go.load())std::this_thread::yield();
    for(int i=0;i<100000;++i){
      if(alternate)a.setTone(6,-40,0);else a.setTone(-10,2,-4);
      a.setBalance(i%33-16);
    }producer=false;
  };
  std::thread w1(writer,false),w2(writer,true);go=true;
  std::thread rates([&]{for(int i=0;i<2000;++i)a.setSampleRate(i%2?44100:48000);});
  for(int i=0;i<4000;++i){a.block(8);assert(a.m_appliedTone==pack(-10,2,-4)||
    a.m_appliedTone==pack(6,-40,0));verifyBank(a);assert(a.m_balance>=-16&&a.m_balance<=16);}
  w1.join();w2.join();rates.join();
  a.setTone(-2,-3,-4);a.setBalance(3);a.block(1);
  assert(a.m_appliedTone==pack(-2,-3,-4)&&a.m_balance==3);verifyBank(a);
  // EOF/drain and decode share the existing recursive audio mutex; test both
  // lock order and actual filter-history accesses with competing consumers.
  std::thread drain([&]{for(int i=0;i<100;++i)a.playI2Sremains();});
  for(int i=0;i<100;++i){std::lock_guard<std::recursive_mutex> lock(a.mutex);a.block(8);}
  drain.join();
  auto begin=std::chrono::steady_clock::now();
  for(int i=0;i<10000;++i){a.setTone(i%47-40,0,0);a.m_validSamples=0;a.performAudioTask();}
  auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
  std::cout<<"PASS production Audio parameter/block/IIR/Gain/STOP/drain tests; host changed-boundary mean "
           <<us/10000.0<<" us (not ESP32 timing)\n";
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='g++')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--sanitize', action='store_true')
    mode.add_argument('--thread-sanitize', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'src/audioI2S/Audio.cpp').read_text()
    # Mask comments only for locating braces (apostrophes in prose are not C++
    # character literals). Compile the original, unmodified method text.
    tokens = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/'
    masked = re.sub(tokens, lambda m: ' ' * len(m[0]) if m[0].startswith('/') else m[0],
                    source, flags=re.DOTALL)
    header = (ROOT / 'src/audioI2S/AudioEx.h').read_text()
    members = ('m_requestedTone', 'm_appliedTone', 'm_filterSampleRate',
               'm_requestedBalance', 'm_balance', 'm_vol', 'm_sampleRate')
    declarations = '\n'.join(line for line in header.splitlines()
                             if any(name in line.split('//')[0] for name in members))
    assert len(declarations.splitlines()) == len(members)
    code = PREFIX + declarations + CLASS
    for signature in ('void Audio::setTone', 'void Audio::setBalance',
                      'void Audio::setVolume', 'bool Audio::setSampleRate',
                      'uint32_t Audio::getSampleRate', 'void Audio::applyPendingAudioParameters',
                      'void Audio::IIR_calculateCoefficients', 'int16_t* Audio::IIR_filterChain0',
                      'int16_t* Audio::IIR_filterChain1', 'int16_t* Audio::IIR_filterChain2',
                      'int32_t Audio::Gain', 'bool Audio::playChunk', 'bool Audio::playSample',
                      'void Audio::performAudioTask', 'void Audio::playI2Sremains',
                      'uint32_t Audio::stopSong', 'uint8_t Audio::getBitsPerSample',
                      'uint8_t Audio::getChannels', 'bool Audio::setChannels'):
        start = masked.index(signature)
        size = len(extract_function(masked, signature))
        code += source[start:start + size] + '\n'
    code += TESTS
    with tempfile.TemporaryDirectory(prefix='voxone-audio-parameters-') as tmp:
        cpp = Path(tmp) / 'test.cpp'
        binary = Path(tmp) / 'test'
        cpp.write_text(code)
        flags = ['-std=c++11', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror',
                 '-Wno-unused-parameter']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
        if args.thread_sanitize:
            flags += ['-fsanitize=thread', '-fno-omit-frame-pointer', '-no-pie']
        subprocess.run([args.cxx, *flags, '-I', str(ROOT), str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=60)


if __name__ == '__main__':
    main()
