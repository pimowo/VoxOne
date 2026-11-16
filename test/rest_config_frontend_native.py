#!/usr/bin/env python3
"""Run the production HA/theme/power submitters and result poller in Node.

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
    assert(['/api/ha','/api/display/theme','/api/power'].includes(url));
    assert(haSettingsSaving || tftThemeSaving || powerModeSaving);
    if (url === '/api/power') assert.equal(options.body.get('mode'),powerModeSelect.value);
    if (networkFailure) throw new Error('network disconnected');
    return {status: postStatus, json: async () => ({id, status:'pending'})};
  }
  if (url === '/api/power') return {ok:powerReadStatus===200,status:powerReadStatus,
    json:async()=>({mode:'soft_standby',standby:false,buttonPin:42,relayAvailable:false})};
  ++gets;
  assert.equal(url, '/api/config/operation?id=' + id);
  assert.equal(options.cache, 'no-store');
  assert(!feedback.startsWith('Zapisano'));
  assert(!tftThemeStatus.textContent.startsWith('Zapisano'));
  assert(!powerModeStatus.textContent.startsWith('Zapisano'));
  const next = replies.shift() || 'pending';
  const status = typeof next === 'number' ? next : 200;
  return {status, ok:status===200, json:async()=>({id:otherId?'bad':id,status:next})};
}
function reset() {
  posts=gets=0;feedback='';feedbackError=false;tftThemeStatus.textContent='';
  tftThemeStatus.dataset.kind='';haSettingsSaving=tftThemeSaving=false;
  postStatus=202;networkFailure=otherId=false;state.haName='Old';state.tftTheme='default';
  powerModeSaving=powerModeLoading=false;powerReadStatus=200;state.powerMode='soft_standby';
  powerModeSelect.value='hard_power';powerModeStatus.textContent='';powerModeStatus.dataset.kind='';
  haNameInput.value='Kitchen';tftThemeSelect.value='yamaha';
  replies=['pending',503,'saved'];
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
  console.log('PASS production HA/theme/power forms, GET and REST result polling; fake DOM/fetch/timers');
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
        'async function loadPowerMode', 'function renderPowerMode')) + TESTS
    with tempfile.TemporaryDirectory(prefix='voxone-rest-frontend-') as work:
        path = Path(work) / 'test.cjs'
        path.write_text(script)
        subprocess.run([args.node, str(path)], check=True, timeout=30)


if __name__ == '__main__':
    main()
