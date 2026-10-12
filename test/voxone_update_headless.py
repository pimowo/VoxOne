"""Exercise the update tab with the production HTML/JS in headless Chrome."""

from pathlib import Path
import gzip
import os
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def resolve_chrome():
    configured = os.environ.get("CHROME_BIN")
    if configured:
        candidate = shutil.which(configured)
        if candidate:
            return Path(candidate).resolve()
        raise FileNotFoundError(f"CHROME_BIN does not point to an executable: {configured}")

    for name in ("chromium", "chromium-browser", "google-chrome",
                 "google-chrome-stable", "chrome"):
        candidate = shutil.which(name)
        if candidate:
            return Path(candidate).resolve()

    if os.name == "nt":
        for directory in ("PROGRAMFILES", "PROGRAMFILES(X86)", "LOCALAPPDATA"):
            base = os.environ.get(directory)
            if base:
                candidate = Path(base) / "Google" / "Chrome" / "Application" / "chrome.exe"
                if candidate.is_file():
                    return candidate.resolve()
        original = Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe")
        if original.is_file():
            return original.resolve()

    raise FileNotFoundError(
        "Chrome/Chromium executable not found; set CHROME_BIN or add a browser to PATH"
    )


MOCK = r"""
<script>
window.voxOneProfile = 'l0';
window.voxOneVersion = '0.8.0';
window.voxOneChannel = 'dev';
window.voxOneBuild = '192a400';
window.yoRadioVersion = 'test';
class MockWebSocket {
  static OPEN = 1;
  static CLOSING = 2;
  constructor() {
    this.readyState = 1;
    this.sent = [];
    window.btTestSocket = this;
    setTimeout(() => this.onopen(), 0);
  }
  send(message) { this.sent.push(message); }
  close() { this.readyState = 3; this.onclose?.(); }
  receive(status) { this.onmessage({data: JSON.stringify({webStatus: status})}); }
  receiveSystem(info) { this.onmessage({data: JSON.stringify({systemInfo: info})}); }
  receiveNetwork(info) { this.onmessage({data: JSON.stringify({networkInfo: info})}); }
  receiveVolume(volume, muted) { this.onmessage({data: JSON.stringify({payload:[
    {id:'volume100',value:volume},{id:'muted',value:muted ? 1 : 0}
  ]})}); }
}
window.WebSocket = MockWebSocket;
class MockUpdateRequest {
  constructor() { this.handlers = {}; this.upload = {addEventListener() {}}; window.btUpdateRequest = this; }
  open(method, url) { this.method = method; this.url = url; }
  addEventListener(name, handler) { this.handlers[name] = handler; }
  send(body) {
    this.body = body;
    if (this.url === '/update') {
      window.updateTransportRequests.push(this);
      const result = window.updateTransportOutcomes.shift() ||
        {event:'load', status:400, text:'Update rejected'};
      this.status = result.status || 0;
      this.responseText = result.text || '';
      this.handlers[result.event]();
      return;
    }
    this.status = 202;
    this.responseText = 'STAGED';
    this.handlers.load();
  }
}
window.XMLHttpRequest = MockUpdateRequest;
window.updateTransportOutcomes = [];
window.updateTransportRequests = [];
window.btUpdateResponses = [];
window.hardwareRequests = [];
window.hardwarePostResponses = [];
window.configBackupRequests = 0;
window.configDownloadedName = '';
window.configRestoreRequests = [];
window.configRestoreResponses = [];
window.hardwareSnapshot = {
  family:'M0', mcu:'ESP32-S3', restartRequired:false,
  config:{display:'ssd1309_128x64', input:'ec11'},
  supportedDisplays:[
    {value:'none',label:'Brak',status:'supported',statusLabel:'DOSTĘPNE'},
    {value:'ssd1306_128x64',label:'SSD1306_128X64',status:'supported',statusLabel:'DOSTĘPNE'},
    {value:'ssd1309_128x64',label:'SSD1309_128X64',status:'supported',statusLabel:'DOSTĘPNE'}
  ],
  supportedInputs:[
    {value:'none',label:'Brak',status:'supported',statusLabel:'DOSTĘPNE'},
    {value:'ec11',label:'EC11',status:'untested',statusLabel:'DO TESTU'},
    {value:'buttons3',label:'BUTTONS3',status:'untested',statusLabel:'DO TESTU'}
  ],
  roadmap:{
    displays:[
      {value:'none',label:'Brak',note:'',status:'supported',statusLabel:'DOSTĘPNE',selectable:true,current:false},
      {value:'ssd1306_128x64',label:'SSD1306_128X64',note:'',status:'supported',statusLabel:'DOSTĘPNE',selectable:true,current:false},
      {value:'ssd1309_128x64',label:'SSD1309_128X64',note:'',status:'supported',statusLabel:'DOSTĘPNE',selectable:true,current:true},
      {value:'ch1116_128x64',label:'CH1116_128X64',note:'',status:'planned',statusLabel:'PLANOWANE',selectable:false,current:false},
      {value:'sh1106_128x64',label:'SH1106_128X64',note:'',status:'planned',statusLabel:'PLANOWANE',selectable:false,current:false},
      {value:'st7290_128x64_tbd',label:'ST7290_128X64 (?)',note:'Kontroler do potwierdzenia',status:'planned',statusLabel:'PLANOWANE',selectable:false,current:false},
      {value:'st7789v_320x240',label:'ST7789V_320X240',note:'',status:'planned',statusLabel:'PLANOWANE',selectable:false,current:false},
      {value:'ssd1322_256x64',label:'SSD1322_256X64',note:'',status:'planned',statusLabel:'PLANOWANE',selectable:false,current:false},
      {value:'st7796s_480x320',label:'ST7796S_480X320',note:'',status:'planned',statusLabel:'PLANOWANE',selectable:false,current:false},
      {value:'gc9a01_240x240',label:'GC9A01_240X240',note:'',status:'planned',statusLabel:'PLANOWANE',selectable:false,current:false}
    ],
    inputs:[
      {value:'none',label:'Brak',note:'',status:'supported',statusLabel:'DOSTĘPNE',selectable:true,current:false},
      {value:'ec11',label:'EC11',note:'',status:'untested',statusLabel:'DO TESTU',selectable:true,current:true},
      {value:'buttons3',label:'BUTTONS3',note:'Brak fizycznego PASS',status:'untested',statusLabel:'DO TESTU',selectable:true,current:false}
    ],
    audio:[
      {value:'pcm5102a',label:'PCM5102A',note:'',status:'supported',statusLabel:'DOSTĘPNE',selectable:false,current:true},
      {value:'max98357a',label:'MAX98357A',note:'',status:'planned',statusLabel:'PLANOWANE',selectable:false,current:false}
    ],
    modules:[
      {value:'voxonebt',label:'VoxOneBT',note:'',status:'planned',statusLabel:'PLANOWANE',selectable:false,current:false},
      {value:'ds3231',label:'DS3231',note:'',status:'planned',statusLabel:'PLANOWANE',selectable:false,current:false},
      {value:'battery',label:'Pomiar baterii',note:'',status:'planned',statusLabel:'PLANOWANE',selectable:false,current:false}
    ]
  }
};
const originalFetch = window.fetch.bind(window);
window.fetch = (url, options = {}) => {
  if (url === '/api/config/backup') {
    window.configBackupRequests++;
    return Promise.resolve({ok:true,status:200,
      headers:{get:name => name.toLowerCase() === 'content-disposition' ?
        'attachment; filename="VoxOne-0.8.0-backup.vxcf"' : null},
      blob:() => Promise.resolve(new Blob(['VXCF-backup'], {type:'application/octet-stream'}))});
  }
  if (url === '/api/config/restore') {
    window.configRestoreRequests.push(options);
    const prepared = window.configRestoreResponses.shift() ||
      {ok:true,status:200,json:() => Promise.resolve({ok:true,restartRequired:true})};
    return Promise.resolve(prepared);
  }
  if (url === '/api/bt/update')
    return Promise.resolve({ok:true, json:() => Promise.resolve(window.btUpdateResponses.shift() || {state:0})});
  if (url === '/api/hardware') {
    if ((options.method || 'GET') === 'GET')
      return Promise.resolve({ok:true, status:200, json:() => Promise.resolve(window.hardwareSnapshot)});
    const request = Object.fromEntries(new URLSearchParams(options.body || ''));
    window.hardwareRequests.push(request);
    const prepared = window.hardwarePostResponses.shift();
    if (prepared) return Promise.resolve(prepared);
    const result = {ok:true, changed:true, restartRequired:true,
      config:{display:request.display,input:request.input}};
    return Promise.resolve({ok:true,status:200,json:() => Promise.resolve(result)});
  }
  return originalFetch(url, options);
};
const originalAnchorClick = HTMLAnchorElement.prototype.click;
HTMLAnchorElement.prototype.click = function() {
  if (this.download) { window.configDownloadedName = this.download; return; }
  return originalAnchorClick.call(this);
};
const originalSetInterval = window.setInterval.bind(window);
window.setInterval = (handler, delay, ...args) => {
  if (delay === 1500) { window.btPollTest = handler; return 1; }
  return originalSetInterval(handler, delay, ...args);
};
</script>
"""
CHECK = r"""
<script>
setTimeout(async () => {
  const result = document.getElementById('headless-result');
  const get = id => document.getElementById(id);
  const check = (ok, message) => { if (!ok) throw Error(message); };
  try {
    check(location.hash === '#status', 'startup tab');
    const muteButtons = [...document.querySelectorAll('.mute-button')];
    check(muteButtons.length === 2 && muteButtons.every(button => button.disabled), 'mute waits for snapshot');
    btTestSocket.receiveVolume(27, false);
    check(muteButtons.every(button => !button.disabled && button.getAttribute('aria-pressed') === 'false'), 'initial mute off');
    muteButtons[0].click();
    check(btTestSocket.sent.at(-1) === 'mute=1', 'WWW mute on command');
    btTestSocket.receiveVolume(27, true);
    check(muteButtons.every(button => button.getAttribute('aria-pressed') === 'true'), 'mute on both controls');
    get('volume-slider').value = '28';
    get('volume-slider').dispatchEvent(new Event('input'));
    get('volume-slider').dispatchEvent(new Event('change'));
    check(btTestSocket.sent.some(item => item === 'vol100=28'), 'volume change while muted');
    btTestSocket.receiveVolume(28, true);
    check(muteButtons.every(button => button.getAttribute('aria-pressed') === 'true'), 'volume ack preserves mute');
    muteButtons[1].click();
    check(btTestSocket.sent.at(-1) === 'mute=0', 'WWW mute off command');
    btTestSocket.receiveVolume(28, false);
    check(muteButtons.every(button => button.getAttribute('aria-pressed') === 'false'), 'mute off both controls');
    btTestSocket.receiveVolume(0, false);
    check(muteButtons.every(button => button.getAttribute('aria-pressed') === 'false'), 'volume zero is not runtime mute');
    btTestSocket.receiveVolume(27, true);
    check(muteButtons[0].getAttribute('aria-pressed') === 'true', 'external mute update');
    check(!btTestSocket.sent.some(item => /^(stop|webtransport|source)=/.test(item)), 'mute does not stop or switch source');
    const sourceSnapshot = (source, availableSources, online, fields = {}) => ({
      source, activeSource:source === 'WEB' ? 'radio' : 'bt', availableSources,
      name:'Radio', metadata:'', artist:'', title:'',
      codec:'MP3', playback:'PLAY', transport:'playing', bitrate:128, sampleRate:0,
      btConnected:false, volume100:27, muted:true,
      btModule:{online, firmware:'', protocol:0, name:'', capabilities:''}, ...fields
    });
    const selector = get('source-selector');
    check(selector.querySelectorAll('button').length === 0, 'source waits for snapshot');
    btTestSocket.receive(sourceSnapshot('WEB', ['radio','bt'], true, {
      name:'Radio A', metadata:'Artysta - Utwór', artist:'Artysta', title:'Utwór',
      playback:'STOP', transport:'stopped', volume100:41, muted:false
    }));
    check(get('current-station-name').textContent === 'Radio A', 'RADIO station from snapshot');
    check(get('metadata').textContent === 'Artysta - Utwór', 'RADIO ICY metadata from snapshot');
    check(get('playback-state').textContent === 'STOP', 'RADIO stopped snapshot');
    check(get('volume').textContent === '41' && muteButtons.every(button => button.getAttribute('aria-pressed') === 'false'),
      'volume and mute from one snapshot');
    const sourceButton = source => selector.querySelector('button[data-source="' + source + '"]');
    check(sourceButton('radio').getAttribute('aria-pressed') === 'true', 'RADIO active');
    check(!sourceButton('bt').disabled, 'BT available without phone connection');
    sourceButton('bt').click();
    check(btTestSocket.sent.at(-1) === 'source=bt', 'manual BT command');
    check(sourceButton('radio').getAttribute('aria-pressed') === 'true', 'selection waits for device');
    btTestSocket.receive(sourceSnapshot('BT', ['radio','bt'], true, {
      name:'Telefon', artist:'BT Artysta', title:'BT Utwór', codec:'', bitrate:0,
      sampleRate:44100, btConnected:true, playback:'PLAY'
    }));
    check(sourceButton('bt').getAttribute('aria-pressed') === 'true', 'BT active from device');
    check(get('current-station-name').textContent === 'Telefon' &&
      get('bt-artist').textContent.includes('BT Artysta') && get('bt-title').textContent.includes('BT Utwór'),
      'BT AVRCP metadata replaces RADIO metadata');
    check(get('metadata').hidden && get('playback-state').textContent === 'PLAY', 'BT playing');
    btTestSocket.receive({source:'BT', btConnected:false});
    check(get('bt-disconnected').hidden && get('playback-state').textContent === 'PLAY',
      'incomplete status cannot imply BT disconnect; complete webStatus is atomic');
    btTestSocket.receive(sourceSnapshot('BT', ['radio','bt'], true, {
      name:'Telefon', codec:'', bitrate:0, sampleRate:44100,
      btConnected:true, playback:'PAUZA', transport:'paused'
    }));
    check(get('playback-state').textContent === 'PAUZA' &&
      !get('bt-artist').textContent.includes('BT Artysta') &&
      !get('bt-title').textContent.includes('BT Utwór'), 'BT pause and metadata clear');
    btTestSocket.receive(sourceSnapshot('BT', ['radio','bt'], true, {
      name:'Bluetooth', codec:'', bitrate:0, btConnected:false, playback:'', transport:'unavailable'
    }));
    check(get('bt-disconnected').hidden === false && get('playback-state').textContent === 'Brak połączenia',
      'BT phone disconnect snapshot');
    sourceButton('radio').click();
    check(btTestSocket.sent.at(-1) === 'source=radio', 'manual RADIO command');
    btTestSocket.receive(sourceSnapshot('WEB', ['radio','bt'], true));
    check(sourceButton('radio').getAttribute('aria-pressed') === 'true', 'RADIO active from device');
    check(!get('metadata').hidden && get('metadata').textContent === '—' &&
      get('playback-state').textContent === 'PLAY', 'RADIO return has no stale BT metadata');
    btTestSocket.receiveVolume(32, true);
    check(get('volume').textContent === '32' && muteButtons.every(button => button.getAttribute('aria-pressed') === 'true'),
      'live volume and mute update');
    btTestSocket.receive(sourceSnapshot('WEB', ['radio','bt'], false));
    check(sourceButton('bt').disabled && sourceButton('bt').textContent.includes('offline'), 'BT offline shown');
    btTestSocket.receive(sourceSnapshot('WEB', ['radio'], false));
    check(selector.querySelectorAll('button').length === 1 && !sourceButton('bt'), 'target without BT');
    check(!btTestSocket.sent.some(item => /^(webtransport|stop)=/.test(item)), 'source selection sends no transport');
    const icon = document.querySelector('link[rel="icon"]');
    const logo = document.querySelector('header img[src*="voxone-logo.svg"]');
    check(icon?.type === 'image/svg+xml' && !!logo, 'VoxOne SVG favicon and header logo');
    check(icon.getAttribute('href') === logo.getAttribute('src'), 'favicon reuses header logo');
    check(get('footer-details').textContent === 'VoxOne 0.8.0 · L0', 'footer identity');
    check(get('footer-connection').textContent === 'Połączono', 'footer WebSocket connected');
    check(get('system').querySelector('h3').textContent === 'VoxOne', 'VoxOne system card');
    check([...get('system').querySelectorAll('h3')].some(node => node.textContent === 'VoxOneBT'), 'VoxOneBT system card');
    check([...get('system').querySelectorAll('h3')].some(node => node.textContent === 'Backup konfiguracji'),
      'backup card visible');
    check(get('system').textContent.includes('hasła Wi-Fi/MQTT'), 'backup secrets warning');
    get('config-backup-button').click();
    await new Promise(resolve => setTimeout(resolve, 0));
    check(configBackupRequests === 1 && configDownloadedName === 'VoxOne-0.8.0-backup.vxcf' &&
      get('config-backup-status').textContent.includes('pobrany'),
      'backup download action');
    const restoreFile = new File(['VXCF-backup'], 'settings.vxcf', {type:'application/octet-stream'});
    const restoreTransfer = new DataTransfer();
    restoreTransfer.items.add(restoreFile);
    Object.defineProperty(get('config-restore-file'), 'files', {configurable:true, value:restoreTransfer.files});
    get('config-restore-file').dispatchEvent(new Event('change'));
    check(!get('config-restore-button').disabled && get('config-restore-name').textContent === 'settings.vxcf',
      'restore file selection');
    get('config-restore-button').click();
    await new Promise(resolve => setTimeout(resolve, 0));
    check(configRestoreRequests.length === 1 && configRestoreRequests[0].body === restoreFile,
      'restore uploads selected binary');
    check(get('config-restore-status').textContent.includes('Wymagane ponowne uruchomienie') &&
      !get('config-restore-restart').hidden, 'restore success and restart action');
    configRestoreResponses.push({ok:false,status:400,
      json:() => Promise.resolve({error:'invalid_file'})});
    get('config-restore-button').click();
    await new Promise(resolve => setTimeout(resolve, 0));
    check(get('config-restore-status').dataset.kind === 'error' &&
      get('config-restore-status').textContent.includes('Nieprawidłowy') &&
      get('config-restore-restart').hidden, 'restore invalid error without restart');
    for (const [error, text] of [['wrong_schema','schematu'], ['bad_crc','CRC'],
                                ['unsupported_hardware','nieobsługiwaną']]) {
      configRestoreResponses.push({ok:false,status:422,
        json:() => Promise.resolve({error})});
      get('config-restore-button').click();
      await new Promise(resolve => setTimeout(resolve, 0));
      check(get('config-restore-status').textContent.includes(text) &&
        get('config-restore-restart').hidden, 'restore error ' + error);
    }
    configRestoreResponses.push({ok:false,status:503,
      json:() => Promise.resolve({error:'recovery_pending'})});
    get('config-restore-button').click();
    await new Promise(resolve => setTimeout(resolve, 0));
    check(get('config-restore-status').textContent.includes('wykonać recovery') &&
      get('config-restore-restart').hidden, 'restore recovery pending without success action');
    ['Czas pracy', 'Wolna pamięć heap', 'Możliwości', 'Protokół', 'Nazwa BT'].forEach(label =>
      check([...get('system').querySelectorAll('dt')].some(node => node.textContent === label), 'system label ' + label));
    ['VoxOne Firmware', 'VoxOne system plików', 'VoxOneBT Firmware'].forEach(label =>
      check([...get('update').querySelectorAll('h3')].some(node => node.textContent === label), label));
    const btUpdateCard = [...get('update').querySelectorAll('.card')].find(card =>
      card.querySelector('h3')?.textContent === 'VoxOneBT Firmware');
    check([...btUpdateCard.querySelectorAll('dt')].map(node => node.textContent).join(',') === 'Status',
      'BT update card contains only compact status fact');
    check(btUpdateCard.textContent.includes('Aktualizacja VoxOneBT: bezpośrednio przez USB (X0).'),
      'BT update card keeps direct USB guidance');
    const networkCard = [...get('settings').querySelectorAll('.card')].find(card => card.querySelector('h3')?.textContent === 'Sieć');
    check(!!networkCard, 'network card');
    location.hash = '#settings';
    window.dispatchEvent(new Event('hashchange'));
    check(btTestSocket.sent.some(item => item === 'getsystem=1'), 'network snapshot requested on Settings');
    location.hash = '#update';
    window.dispatchEvent(new Event('hashchange'));
    check(btTestSocket.sent.some(item => item.includes('getwebstatus')), 'status request');
    const status = {source:'BT', activeSource:'bt', availableSources:['radio','bt'], name:'Telefon', metadata:'', artist:'', title:'',
      codec:'', playback:'', transport:'unavailable', bitrate:0, sampleRate:0, btConnected:false,
      volume100:32, muted:true,
      btModule:{online:true, firmware:'0.6.1-dev', protocol:2,
                name:'VoxOneBT-EFF35A', capabilities:'AVRCP,VU'}};
    btTestSocket.receive(status);
    btTestSocket.receiveNetwork({hostname:'voxone-l0', activeSsid:'HomeNet', profiles:[
      {ssid:'HomeNet', passwordSet:true, password:'secret', order:1},
      {ssid:'backup', passwordSet:false, order:2}
    ]});
    check(get('network-hostname').textContent === 'voxone-l0', 'network hostname');
    check(get('network-active-ssid').textContent === 'HomeNet', 'active Wi-Fi SSID');
    check(get('network-profiles').textContent.includes('Hasło: zapisane'), 'passwordSet true');
    check(get('network-profiles').textContent.includes('Hasło: brak'), 'passwordSet false');
    check(get('network-profiles').textContent.includes('kolejność prób: 2'), 'profile order');
    check(!networkCard.querySelector('input,button,select,textarea'), 'network card has no unsupported edit controls');
    check(!get('network-profiles').textContent.includes('secret'), 'no password value rendered');
    location.hash = '#hardware';
    window.dispatchEvent(new Event('hashchange'));
    await new Promise(resolve => setTimeout(resolve, 0));
    const hardwareDisplay = get('hardware-display');
    const hardwareInput = get('hardware-input');
    check(!get('hardware').hidden, 'SPRZĘT tab visible');
    check(get('hardware-family').textContent === 'M0' && get('hardware-mcu').textContent === 'ESP32-S3',
      'M0 hardware family from descriptor API');
    check([...hardwareDisplay.options].map(option => option.value).join(',') ===
      'none,ssd1306_128x64,ssd1309_128x64,ch1116_128x64,sh1106_128x64,st7290_128x64_tbd,st7789v_320x240,ssd1322_256x64,st7796s_480x320,gc9a01_240x240',
      'display dropdown contains the complete M0 roadmap');
    check([...hardwareInput.options].map(option => option.value).join(',') === 'none,ec11,buttons3',
      'input options follow capabilities');
    check(hardwareDisplay.querySelector('option[value="ch1116_128x64"]').disabled,
      'planned display remains visible and disabled');
    check(!hardwareInput.querySelector('option[value="buttons3"]').disabled,
      'BUTTONS3 remains selectable');
    check(get('hardware-audio-roadmap').textContent.includes('PCM5102A') &&
      get('hardware-audio-roadmap').textContent.includes('MAX98357A') &&
      get('hardware-audio-roadmap').textContent.includes('PLANOWANE'),
      'audio roadmap is informational');
    check(get('hardware-module-roadmap').textContent.includes('VoxOneBT') &&
      get('hardware-module-roadmap').textContent.includes('Pomiar baterii') &&
      get('hardware-module-roadmap').textContent.includes('PLANOWANE'),
      'module roadmap is informational');
    const moduleToggles = [...get('hardware-module-roadmap').querySelectorAll('input[type=checkbox]')];
    check(!get('hardware-audio-roadmap').querySelector('input,button,select,textarea') &&
      moduleToggles.length === 3 && moduleToggles.every(toggle => toggle.disabled),
      'audio is informational and module roadmap uses disabled toggles');
    check(hardwareDisplay.value === 'ssd1309_128x64' && hardwareInput.value === 'ec11',
      'M0 current SSD1309 and EC11 selections selected');
    check(get('hardware-display-status').textContent === 'DOSTĘPNE' &&
      !get('hardware-display-current').hidden && !get('hardware-input-current').hidden,
      'current selection status is compact');
    check(!get('hardware-save') && get('hardware-save-restart').disabled &&
      get('hardware-dirty-badge').hidden, 'no standalone save and apply initially disabled');
    hardwareDisplay.value = 'none';
    hardwareDisplay.dispatchEvent(new Event('change'));
    check(!get('hardware-save-restart').disabled && !get('hardware-dirty-badge').hidden,
      'supported display change enables apply');
    hardwareDisplay.value = 'ssd1309_128x64';
    hardwareDisplay.dispatchEvent(new Event('change'));
    check(get('hardware-save-restart').disabled && get('hardware-dirty-badge').hidden,
      'return to SSD1309 clears dirty state');
    hardwareInput.value = 'buttons3';
    hardwareInput.dispatchEvent(new Event('change'));
    check(get('hardware-input-status').textContent === 'DO TESTU' &&
      get('hardware-input-note').textContent === 'Brak fizycznego PASS' &&
      !get('hardware-save-restart').disabled, 'BUTTONS3 status and note');
    hardwareInput.value = 'ec11';
    hardwareInput.dispatchEvent(new Event('change'));
    hardwareDisplay.value = 'ch1116_128x64';
    hardwareDisplay.dispatchEvent(new Event('change'));
    check(get('hardware-display-status').textContent === 'PLANOWANE' &&
      get('hardware-save-restart').disabled && get('hardware-dirty-badge').hidden,
      'planned value cannot become saveable dirty state');
    const requestsBeforePlannedSubmit = hardwareRequests.length;
    get('hardware-form').requestSubmit();
    await new Promise(resolve => setTimeout(resolve, 0));
    check(hardwareRequests.length === requestsBeforePlannedSubmit,
      'planned value cannot be submitted');
    hardwareDisplay.value = 'ssd1309_128x64';
    hardwareDisplay.dispatchEvent(new Event('change'));
    hardwareInput.value = 'none';
    hardwareInput.dispatchEvent(new Event('change'));
    check(!get('hardware-save-restart').disabled && !get('hardware-dirty-badge').hidden,
      'EC11 to NONE enables apply');
    hardwareInput.value = 'ec11';
    hardwareInput.dispatchEvent(new Event('change'));
    check(get('hardware-save-restart').disabled && get('hardware-dirty-badge').hidden,
      'return to EC11 clears dirty state');
    hardwareDisplay.value = 'none';
    hardwareDisplay.dispatchEvent(new Event('change'));
    hardwarePostResponses.push({ok:false,status:422,json:() => Promise.resolve({error:'unsupported_hardware'})});
    get('hardware-save-restart').click();
    await new Promise(resolve => setTimeout(resolve, 0));
    check(get('hardware-feedback').dataset.kind === 'error' &&
      get('hardware-feedback').textContent.includes('nie jest obsługiwana'), 'hardware validation error shown');
    check(!btTestSocket.sent.some(item => item === 'reboot=1'), 'failed save does not restart');
    check(get('update-bt-online').textContent === 'Online', 'compact update status online');
    check(!get('update-bt-firmware') && !get('update-bt-protocol') &&
      !get('update-bt-name') && !get('update-bt-capabilities'),
      'compact BT update card does not duplicate SYSTEM details');
    check(get('system-bt-online').textContent === 'TAK', 'system BT online while phone disconnected');
    check(get('system-bt-firmware').textContent === '0.6.1-dev', 'system BT firmware');
    location.hash = '#system';
    window.dispatchEvent(new Event('hashchange'));
    check(btTestSocket.sent.some(item => item === 'getsystem=1'), 'system snapshot request');
    check(btTestSocket.sent.some(item => item === 'getrssi=1'), 'system RSSI request');
    btTestSocket.onmessage({data:JSON.stringify({ipaddr:'192.168.1.42',
      payload:[{id:'rssi',value:-57}]})});
    check(get('footer-details').textContent === 'VoxOne 0.8.0 · L0 · 192.168.1.42', 'footer IP');
    check(get('system-version').textContent === '0.8.0-dev', 'identity firmware fallback');
    check(get('system-build').textContent === '192a400', 'identity build');
    check(get('update-version').textContent === 'VoxOne 0.8.0-dev', 'update firmware');
    check(get('update-build').textContent === '192a400', 'update build');
    check(get('update-profile').textContent === 'L0', 'update profile');
    check(get('system-profile').textContent === 'L0', 'identity profile fallback');
    check(get('system-ip').textContent === '192.168.1.42', 'legacy IP field');
    check(get('system-rssi').textContent === '-57 dBm', 'legacy RSSI field');
    check(get('system-psram').textContent !== 'Brak', 'missing snapshot is not no-PSRAM');
    const systemInfo = {mac:'AA:BB:CC:DD:EE:FF', rssi:-57,
      uptimeSeconds:4*3600+31*60+18,
      freeHeap:256000, minimumFreeHeap:128000, psramTotal:8388608, psramFree:4194304,
      capabilities:'DISPLAY, ENCODER, BT, VU, RTC'};
    btTestSocket.receiveSystem(systemInfo);
    check(get('system-version').textContent === '0.8.0-dev', 'system firmware');
    check(get('system-profile').textContent === 'L0', 'system profile');
    check(get('system-ip').textContent === '192.168.1.42', 'system IP');
    check(get('system-rssi').textContent === '-57 dBm', 'system RSSI');
    check(get('system-uptime').textContent === '04:31:18', 'formatted uptime below one day');
    systemInfo.uptimeSeconds = 2*86400+4*3600+31*60+18;
    btTestSocket.receiveSystem(systemInfo);
    check(get('system-uptime').textContent === '2 d 04:31:18', 'formatted uptime with days');
    check(get('system-free-heap').textContent === '250 kB', 'free heap');
    check(get('system-minimum-heap').textContent === '125 kB', 'minimum free heap');
    check(get('system-psram').textContent === '8.0 MB total / 4.0 MB free', 'PSRAM');
    check(get('system-capabilities').textContent.includes('RTC'), 'system capabilities');
    check(get('update-bt-button').disabled, 'BT update requires explicit profile capability');
    const firmwareTransfer = new DataTransfer();
    firmwareTransfer.items.add(new File(['firmware-image'], 'VoxOne-firmware.bin',
      {type:'application/octet-stream'}));
    get('update-firmware-file').files = firmwareTransfer.files;
    get('update-firmware-file').dispatchEvent(new Event('change'));
    updateTransportOutcomes.push({event:'error'});
    get('update-firmware-button').click();
    check(get('update-progress').hidden && get('update-progress').value === 0 &&
      !get('update-firmware-button').disabled &&
      get('update-status').textContent.includes('ponowić'),
      'MAIN disconnect clears progress and permits retry');
    updateTransportOutcomes.push({event:'abort'});
    get('update-firmware-button').click();
    check(get('update-progress').hidden && !get('update-firmware-button').disabled &&
      get('update-status').textContent.includes('przerwana'),
      'MAIN abort clears busy state');
    const spiffsTransfer = new DataTransfer();
    spiffsTransfer.items.add(new File(['spiffs-image'], 'VoxOne-spiffs.bin',
      {type:'application/octet-stream'}));
    get('update-spiffs-file').files = spiffsTransfer.files;
    get('update-spiffs-file').dispatchEvent(new Event('change'));
    updateTransportOutcomes.push({event:'load', status:400, text:'Update.write failed'});
    get('update-spiffs-button').click();
    check(get('update-progress').hidden && !get('update-spiffs-button').disabled &&
      get('update-status').textContent.includes('Update.write failed'),
      'SPIFFS Error clears busy state and permits retry');
    check(updateTransportRequests.length === 3 &&
      updateTransportRequests[0].body.get('updatetarget') === 'firmware' &&
      updateTransportRequests[2].body.get('updatetarget') === 'spiffs',
      'MAIN and SPIFFS requests remain distinct');
    systemInfo.btFirmwareUpdateSupported = true;
    btTestSocket.receiveSystem(systemInfo);
    status.btModule.capabilities = 'AVRCP VU FW_UPDATE';
    btTestSocket.receive(status);
    check(get('update-bt-button').disabled, 'active BT source blocks firmware update');
    status.source = 'WEB';
    status.activeSource = 'radio';
    btTestSocket.receive(status);
    const btTransfer = new DataTransfer();
    btTransfer.items.add(new File(['image'], 'voxonebt.bin', {type:'application/octet-stream'}));
    get('update-bt-file').files = btTransfer.files;
    get('update-bt-file').dispatchEvent(new Event('change'));
    check(!get('update-bt-button').disabled, 'L0 enables BT upload only with capability, resources and link');
    btUpdateResponses.push({receiving:false, pending:false, active:true,
      state:3, phase:1, error:0, confirmedBytes:1024, totalBytes:2048, percent:50});
    get('update-bt-button').click();
    check(btUpdateRequest.url === '/update/bt' && btUpdateRequest.method === 'POST', 'dedicated BT endpoint');
    check(btUpdateRequest.body.get('filesize') === '5', 'declared image size');
    check(get('update-status').textContent.includes('sendera'), 'accepted staging waits for sender');
    await new Promise(resolve => setTimeout(resolve, 0));
    check(get('update-status').textContent.includes('ACK 1024/2048'), 'BT transfer progress');
    btUpdateResponses.push({receiving:false, pending:false, active:false,
      state:9, phase:5, error:0, confirmedBytes:2048, totalBytes:2048, percent:100});
    await btPollTest();
    check(get('update-status').textContent.includes('zakończona') &&
      !get('update-status').textContent.includes('błąd sendera 0') &&
      get('update-status').dataset.kind === 'status', 'BT Success=9 with error=None');
    for (const [terminalState, errorCode, expected] of [
      [10, 11, 'błąd sendera 11'], [10, 0, 'brak kodu błędu sendera'],
      [11, 19, 'przerwana']
    ]) {
      btUpdateResponses.push({receiving:false, pending:false, active:true,
        state:3, phase:1, error:0, confirmedBytes:0, totalBytes:2048, percent:0});
      get('update-bt-button').click();
      await new Promise(resolve => setTimeout(resolve, 0));
      btUpdateResponses.push({receiving:false, pending:false, active:false,
        state:terminalState, phase:6, error:errorCode,
        confirmedBytes:0, totalBytes:2048, percent:0});
      await btPollTest();
      check(get('update-status').textContent.includes(expected) &&
        !get('update-status').textContent.includes('błąd sendera 0') &&
        get('update-status').dataset.kind === 'error', 'BT terminal state ' + terminalState);
    }
    btTestSocket.close();
    check(selector.querySelectorAll('button').length === 0 &&
      get('current-station-name').textContent === '—' && get('source').textContent === '—',
      'disconnect clears stale source and metadata');
    check(muteButtons.every(button => button.disabled && button.getAttribute('aria-pressed') === 'false'), 'disconnect clears mute state');
    check(get('footer-connection').textContent === 'Rozłączono', 'footer WebSocket disconnected');
    check(get('footer-details').textContent === 'VoxOne 0.8.0 · L0', 'offline footer hides stale IP');
    check(get('network-active-ssid').textContent === '—', 'offline network info cleared');
    const transfer = new DataTransfer();
    transfer.items.add(new File(['image'], 'voxonebt.bin', {type:'application/octet-stream'}));
    get('update-bt-file').files = transfer.files;
    get('update-bt-file').dispatchEvent(new Event('change'));
    check(get('update-bt-file-name').textContent.includes('voxonebt.bin'), 'file name');
    check(get('update-bt-button').disabled, 'update disabled after disconnect');
    status.btModule = {online:false, firmware:'', protocol:0, name:'', capabilities:''};
    btTestSocket.receive(status);
    check(get('update-bt-online').textContent === 'Offline', 'compact update status offline');
    check(get('system-bt-online').textContent === 'NIE', 'system BT offline');
    ['firmware','protocol','name','capabilities'].forEach(field =>
      check(get('system-bt-' + field).textContent === '—', 'system ' + field + ' placeholder'));
    setTimeout(async () => {
      try {
        check(btTestSocket.readyState === MockWebSocket.OPEN, 'WebSocket reconnected');
        check(muteButtons.every(button => button.disabled), 'reconnect waits for snapshot');
        check(selector.querySelectorAll('button').length === 0, 'source reconnect waits for snapshot');
        btTestSocket.receive(sourceSnapshot('WEB', ['radio','bt'], true));
        check(sourceButton('radio').getAttribute('aria-pressed') === 'true' &&
          !sourceButton('bt').disabled, 'source restored after reconnect');
        check(muteButtons.every(button => !button.disabled && button.getAttribute('aria-pressed') === 'true'),
          'reconnect mute restored atomically with source');
        get('hardware-save-restart').click();
        await new Promise(resolve => setTimeout(resolve, 0));
        check(hardwareRequests.at(-1).display === 'none' && hardwareRequests.at(-1).input === 'ec11',
          'supported hardware save request');
        check(Object.keys(hardwareRequests.at(-1)).sort().join(',') === 'display,input',
          'informational hardware never enters save payload');
        check(btTestSocket.sent.some(item => item === 'reboot=1'),
          'successful hardware save uses existing restart path');
        result.textContent = 'PASS';
      } catch (error) { result.textContent = 'FAIL: ' + error.message; }
    }, 1100);
  } catch (error) { result.textContent = 'FAIL: ' + error.message; }
}, 100);
</script>
"""


def main():
    chrome = resolve_chrome()
    html = (ROOT / "web-src/voxone.html").read_text(encoding="utf-8")
    route = (ROOT / "src/core/netserver.cpp").read_text(encoding="utf-8")
    assert re.search(r'webserver\.on\("/favicon\.ico", HTTP_GET,.*?'
                     r'request->redirect\("/voxone-logo\.svg"\);', route, re.S), \
        "favicon.ico must redirect to the existing logo"
    logo = (ROOT / "web-src/voxone-logo.svg").read_bytes()
    assert gzip.decompress((ROOT / "data/www/voxone-logo.svg.gz").read_bytes()) == logo, \
        "served logo asset must match its source"
    scripts = [
        (ROOT / "web-src" / name).read_text(encoding="utf-8")
        for name in ("advanced-audio.js", "dsp-client.js", "voxone.js")
    ]
    html = re.sub(r'<script src="/variables.js"></script>', MOCK, html, count=1)
    html = re.sub(
        r'<script src="/(?:advanced-audio|dsp-client|voxone)\.js[^"]*" defer></script>',
        "", html)
    html = html.replace("</body>", '<output id="headless-result">PENDING</output>' +
                        "".join("<script>" + script + "</script>" for script in scripts) +
                        CHECK + "</body>")
    with tempfile.TemporaryDirectory(prefix="voxone-web-test-", ignore_cleanup_errors=True) as temporary:
        page = Path(temporary) / "test.html"
        page.write_text(html, encoding="utf-8")
        command = [str(chrome), "--headless", "--disable-gpu", "--disable-gpu-compositing",
                   "--disable-features=Vulkan,UseSkiaRenderer,CanvasOopRasterization",
                   "--disable-extensions", "--no-first-run",
                   "--no-default-browser-check", "--allow-file-access-from-files",
                   "--user-data-dir=" + str(Path(temporary) / "profile"),
                   "--virtual-time-budget=2500", "--dump-dom", page.as_uri()]
        result = subprocess.run(command, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=30)
    match = re.search(r'<output id="headless-result">([^<]+)</output>', result.stdout)
    outcome = match.group(1) if match else "FAIL: Chrome did not return the test result"
    print(outcome)
    if outcome != "PASS":
        print(result.stderr[-2000:])
        raise SystemExit(1)


if __name__ == "__main__":
    try:
        main()
    except FileNotFoundError as error:
        raise SystemExit(str(error)) from error
