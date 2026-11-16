#!/usr/bin/env python3
"""Run the production HA/theme/power/hardware submitters and result poller in Node.

DOM fields, fetch and timers are fakes. This is not a browser/layout test.
Use --node when Node is not on PATH. No mutation is sent to a physical device.
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True
from hls_ts_transport_native import extract_function

ROOT = Path(__file__).resolve().parents[1]
PREFIX = r'''
const assert = require('node:assert/strict');
let haSettingsSaving = false, tftThemeSaving = false;
let powerModeSaving = false, powerModeLoading = false;
const state = {connection: 'connected', canTftTheme: true, haName: 'Old', tftTheme: 'default'};
const haNameInput = {value: 'Kitchen'};
const tftThemeSelect = {value: 'yamaha'};
const tftThemeStatus = {textContent: '', dataset: {}};
const powerModeSelect = {value:'hard_power'};
const powerModeStatus = {textContent:'', dataset:{}};
const powerModeSave = {disabled:false};
let hardwareSaving=false, hardwareConfigLoaded=true, savedHardwareConfig;
const hardwareDisplay={value:'none',selectedOptions:[{disabled:false}]};
const hardwareInput={value:'ec11',selectedOptions:[{disabled:false}]};
const hardwareAudio={value:'none',selectedOptions:[{disabled:false}]};
const hardwareFeedback={textContent:'',dataset:{}};
const hardwareSaveRestart={disabled:false}, hardwareDirtyBadge={hidden:true};
const hardwareInputLabels={};
let missingHardwareResult=false, hardwareReadStatus=200;
function replaceHardwareOptions(select,options,value){select.value=value;}
function text(){}
function send(){throw new Error('Hardware must not send reboot or other WS commands');}
function waitForDeviceRestart(){throw new Error('Hardware must not restart');}
let powerReadStatus = 200;
let feedback = '', feedbackError = false, posts = 0, gets = 0;
let replies = [], postStatus = 202, networkFailure = false, otherId = false;
const id = '0123456789abcdef0123456789abcdef';
function renderHaSettings() {}
function renderTftTheme() {}
function setHaNameFeedback(value, error = false) {feedback=value; feedbackError=error;}
function setTimeout(callback) {callback();}
async function fetch(url, options) {
  if (options.method === 'POST') {
    ++posts;
    assert(['/api/ha','/api/display/theme','/api/power','/api/hardware'].includes(url));
    assert(haSettingsSaving || tftThemeSaving || powerModeSaving || hardwareSaving);
    if(url==='/api/hardware') {
      const data=new URLSearchParams(options.body);
      assert.equal(data.get('display'),hardwareDisplay.value);
      assert.equal(data.get('input'),hardwareInput.value);
      assert.equal(data.get('audio'),hardwareAudio.value);
    }
    if (url === '/api/power') assert.equal(options.body.get('mode'),powerModeSelect.value);
    if (networkFailure) throw new Error('network disconnected');
    return {status: postStatus, json: async () => ({id, status:'pending'})};
  }
  if (url === '/api/power') return {ok:powerReadStatus===200,status:powerReadStatus,
    json:async()=>({mode:'soft_standby',standby:false,buttonPin:42,relayAvailable:false})};
  if (url === '/api/hardware') return {ok:hardwareReadStatus===200,status:hardwareReadStatus,json:async()=>({
    config:{display:'none',input:'ec11',audio:'none'},
    supportedDisplays:[],supportedInputs:[],supportedAudioOutputs:[],roadmap:{audio:[]}})};
  ++gets;
  assert.equal(url, '/api/config/operation?id=' + id);
  assert.equal(options.cache, 'no-store');
  assert(!feedback.startsWith('Zapisano'));
  assert(!tftThemeStatus.textContent.startsWith('Zapisano'));
  assert(!powerModeStatus.textContent.startsWith('Zapisano'));
  assert(!hardwareFeedback.textContent.includes('została zapisana'));
  const next = replies.shift() || 'pending';
  const status = typeof next === 'number' ? next : 200;
  return {status, ok:status===200, json:async()=>({id:otherId?'bad':id,status:next,
    result:missingHardwareResult?undefined:{ok:true,restartRequired:true,changed:true,
      config:{display:'none',input:'ec11',audio:'none'}}})};
}
function reset() {
  posts=gets=0;feedback='';feedbackError=false;tftThemeStatus.textContent='';
  tftThemeStatus.dataset.kind='';haSettingsSaving=tftThemeSaving=false;
  postStatus=202;networkFailure=otherId=false;state.haName='Old';state.tftTheme='default';
  powerModeSaving=powerModeLoading=false;powerReadStatus=200;state.powerMode='soft_standby';
  powerModeSelect.value='hard_power';powerModeStatus.textContent='';powerModeStatus.dataset.kind='';
  haNameInput.value='Kitchen';tftThemeSelect.value='yamaha';
  replies=['pending',503,'saved'];
  hardwareSaving=false;hardwareConfigLoaded=true;missingHardwareResult=false;hardwareReadStatus=200;
  savedHardwareConfig={display:'st7796s_480x320',input:'ec11',audio:'pcm5102a'};
  hardwareDisplay.value='none';hardwareInput.value='ec11';hardwareAudio.value='none';
  hardwareDisplay.selectedOptions=[{disabled:false}];
  hardwareFeedback.textContent='';hardwareFeedback.dataset.kind='';
}
const event = {preventDefault(){}};
'''
TESTS = r'''
(async()=>{
  for (const submit of [saveHaSettings, saveTftTheme]) {
    reset(); await submit(event);
    assert.equal(posts,1);assert.equal(gets,3);
    assert((submit===saveHaSettings?feedback:tftThemeStatus.textContent).startsWith('Zapisano'));
    assert.equal(submit===saveHaSettings?state.haName:state.tftTheme,
                 submit===saveHaSettings?'Kitchen':'yamaha');
    for (const result of ['applied_but_not_persisted','failed','cancelled',401,404]) {
      reset();replies=[result];await submit(event);
      assert.equal(posts,1);assert.equal(gets,1);
      assert(submit===saveHaSettings?feedbackError:tftThemeStatus.dataset.kind==='error');
      assert.equal(submit===saveHaSettings?state.haName:state.tftTheme,
                   submit===saveHaSettings?'Old':'default');
      if(result==='applied_but_not_persisted') {
        assert((submit===saveHaSettings?feedback:tftThemeStatus.textContent).includes('RAM'));
        replies=['saved'];await submit(event);assert.equal(posts,2);
        assert((submit===saveHaSettings?feedback:tftThemeStatus.textContent).startsWith('Zapisano'));
      }
    }
    reset();postStatus=503;await submit(event);assert.equal(gets,0);assert.equal(posts,1);
    reset();networkFailure=true;await submit(event);assert.equal(gets,0);assert.equal(posts,1);
    reset();replies=[];await submit(event);assert.equal(gets,60);assert.equal(posts,1);
    assert(submit===saveHaSettings?feedbackError:tftThemeStatus.dataset.kind==='error');
    reset();otherId=true;replies=['saved'];await submit(event);
    assert(submit===saveHaSettings?feedbackError:tftThemeStatus.dataset.kind==='error');
    assert(!haSettingsSaving && !tftThemeSaving);
  }
  reset();haNameInput.value='x'.repeat(33);await saveHaSettings(event);assert.equal(posts,0);
  reset();haNameInput.value='bad\x01';await saveHaSettings(event);assert.equal(posts,0);
  reset();tftThemeSelect.value='invalid';await saveTftTheme(event);assert.equal(posts,0);
  for(const mode of ['soft_standby','hard_power','mute_only']) {
    reset();powerModeSelect.value=mode;await savePowerMode(event);
    assert.equal(posts,1);assert.equal(gets,3);assert.equal(state.powerMode,mode);
    assert(powerModeStatus.textContent.startsWith('Zapisano'));
    assert.equal(powerModeStatus.dataset.kind,'notice');assert(!powerModeSaving);
  }
  for(const result of ['applied_but_not_persisted','failed','cancelled',401,403,404]) {
    reset();replies=[result];await savePowerMode(event);
    assert.equal(posts,1);assert.equal(gets,1);
    assert.equal(state.powerMode,'soft_standby');assert(!powerModeSave.disabled);
    assert.equal(powerModeStatus.dataset.kind,'error');
    assert(!powerModeStatus.textContent.startsWith('Zapisano'));
    if(result==='applied_but_not_persisted') {
      assert(powerModeStatus.textContent.includes('RAM'));
      replies=['saved'];await savePowerMode(event);assert.equal(posts,2);
      assert.equal(state.powerMode,'hard_power');assert(powerModeSave.disabled);
      assert(powerModeStatus.textContent.startsWith('Zapisano'));
    }
  }
  for(const status of [401,403,503]) {
    reset();postStatus=status;await savePowerMode(event);
    assert.equal(posts,1);assert.equal(gets,0);assert.equal(powerModeStatus.dataset.kind,'error');
    assert.equal(state.powerMode,'soft_standby');
  }
  reset();networkFailure=true;await savePowerMode(event);
  assert.equal(posts,1);assert.equal(gets,0);assert.equal(powerModeStatus.dataset.kind,'error');
  reset();replies=[];await savePowerMode(event);
  assert.equal(posts,1);assert.equal(gets,60);assert.equal(powerModeStatus.dataset.kind,'error');
  reset();otherId=true;replies=['saved'];await savePowerMode(event);
  assert.equal(powerModeStatus.dataset.kind,'error');assert.equal(state.powerMode,'soft_standby');
  reset();powerModeSelect.value='invalid';await savePowerMode(event);assert.equal(posts,0);
  reset();await loadPowerMode();assert.equal(posts,0);assert.equal(state.powerMode,'soft_standby');
  assert.equal(powerModeSelect.value,'soft_standby');
  reset();powerReadStatus=401;await loadPowerMode();assert.equal(state.powerMode,null);
  assert(powerModeSave.disabled && powerModeSelect.disabled);
  reset();await saveHardwareConfig();
  assert.equal(posts,1);assert.equal(gets,3);assert.equal(savedHardwareConfig.audio,'none');
  assert(hardwareFeedback.textContent.includes('została zapisana'));
  assert(hardwareFeedback.textContent.includes('Uruchom ponownie'));
  assert(hardwareSaveRestart.disabled && !hardwareSaving);
  await saveHardwareConfig();assert.equal(posts,1); // unchanged, no duplicate submit
  for(const result of ['failed','cancelled','applied_but_not_persisted',401,403,404]) {
    reset();replies=[result];await saveHardwareConfig();
    assert.equal(posts,1);assert.equal(gets,1);
    assert.equal(savedHardwareConfig.audio,'pcm5102a');
    assert.equal(hardwareFeedback.dataset.kind,'error');
    assert(!hardwareSaveRestart.disabled && !hardwareSaving);
    replies=['saved'];await saveHardwareConfig();assert.equal(posts,2);
    assert.equal(savedHardwareConfig.audio,'none');
  }
  for(const status of [400,401,403,422,503]){
    reset();postStatus=status;await saveHardwareConfig();
    assert.equal(posts,1);assert.equal(gets,0);assert.equal(hardwareFeedback.dataset.kind,'error');
    assert.equal(savedHardwareConfig.audio,'pcm5102a');
  }
  reset();networkFailure=true;await saveHardwareConfig();
  assert.equal(posts,1);assert.equal(gets,0);assert.equal(hardwareFeedback.dataset.kind,'error');
  reset();replies=[];await saveHardwareConfig();assert.equal(posts,1);assert.equal(gets,60);
  assert.equal(hardwareFeedback.dataset.kind,'error');
  reset();otherId=true;replies=['saved'];await saveHardwareConfig();
  assert.equal(hardwareFeedback.dataset.kind,'error');
  reset();missingHardwareResult=true;replies=['saved'];await saveHardwareConfig();
  assert.equal(hardwareFeedback.dataset.kind,'error');assert.equal(savedHardwareConfig.audio,'pcm5102a');
  reset();hardwareDisplay.selectedOptions[0].disabled=true;await saveHardwareConfig();assert.equal(posts,0);
  reset();hardwareConfigLoaded=false;await saveHardwareConfig();assert.equal(posts,0);
  reset();hardwareSaving=true;await saveHardwareConfig();assert.equal(posts,0);
  reset();await loadHardwareConfig();assert.equal(posts,0);assert(hardwareSaveRestart.disabled);
  reset();hardwareReadStatus=503;await loadHardwareConfig();
  assert.equal(posts,0);assert(!hardwareConfigLoaded && hardwareSaveRestart.disabled);
  assert.equal(hardwareFeedback.dataset.kind,'error');
  hardwareReadStatus=200;await loadHardwareConfig();assert.equal(posts,0);assert(hardwareConfigLoaded);
  console.log('PASS production HA/theme/power/hardware forms, GET and REST result polling; fake DOM/fetch/timers');
})().catch(error=>{console.error(error);process.exitCode=1;});
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--node', default=shutil.which('node'))
    args = parser.parse_args()
    if not args.node:
        parser.error('Node is required; specify --node (no packages are installed)')
    source = (ROOT / 'web-src/voxone.js').read_text()
    script = PREFIX + '\n'.join(extract_function(source, name) for name in (
        'async function awaitConfigPersistence', 'async function saveHaSettings',
        'async function saveTftTheme', 'async function savePowerMode',
        'async function loadPowerMode', 'function renderPowerMode',
        'async function saveHardwareConfig', 'async function loadHardwareConfig',
        'function hardwareSaveError', 'function setHardwareFeedback',
        'function hardwareDirty', 'function hardwareSelectionSaveable',
        'function renderHardwareFormState')) + TESTS
    with tempfile.TemporaryDirectory(prefix='voxone-rest-frontend-') as work:
        path = Path(work) / 'test.cjs'
        path.write_text(script)
        subprocess.run([args.node, str(path)], check=True, timeout=30)


if __name__ == '__main__':
    main()
