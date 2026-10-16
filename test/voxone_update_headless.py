"""Exercise the update tab with the production HTML/JS in headless Chrome."""

from pathlib import Path
import gzip
import hashlib
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
window.securityFinePointer = true;
const nativeMatchMedia = window.matchMedia.bind(window);
window.matchMedia = query => query.includes('(pointer: fine)') ? {
  matches:window.securityFinePointer, media:query,
  addEventListener() {}, removeEventListener() {}
} : nativeMatchMedia(query);
class MockWebSocket {
  static OPEN = 1;
  static CLOSING = 2;
  constructor() {
    this.readyState = 1;
    this.sent = [];
    window.webSocketInstances.push(this);
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
window.webSocketInstances = [];
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
window.mqttRequests = [];
window.mqttSnapshot = {enabled:true,host:'mqtt.local',port:1883,username:'voxone',
  passwordSet:true,rootTopic:'',effectiveRoot:'voxone/l0'};
window.timeRequests = 0;
window.configBackupRequests = 0;
window.configDownloadedName = '';
window.configRestoreRequests = [];
window.configRestoreResponses = [];
window.deviceReadinessBootId = 'boot-before-update';
window.securityStateRequests = 0;
window.securityStateResponses = [];
window.securityRequests = [];
window.securitySetupResponses = [];
window.securityLoginResponses = [];
window.securityServerState = {
  state:'legacy-unclaimed', setupAllowed:true, loginAllowed:false,
  authenticated:false, protectionEnforced:false
};
const securityResponse = (status, body = {}, headers = {}) => ({
  ok:status >= 200 && status < 300, status,
  _body:body,
  headers:{get:name => headers[name] || headers[name.toLowerCase()] || null},
  json:() => Promise.resolve(body)
});
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
window.stationListResponse = {
  count:24, current:12, revision:'a1b2c3d4',
  stations:Array.from({length:24}, (_, index) => ({
    number:index + 1,
    id:(index + 1).toString(16).toUpperCase().padStart(16, '0'),
    name:'Stacja testowa ' + String(index + 1).padStart(2, '0') +
      ' — długa nazwa kontrolna',
    url:'https://radio.example/' + (index + 1) + '/stream-with-long-address',
    ovol:index % 7 - 3, metadataMode:'normal', swapArtistTitle:false
  }))
};
const originalFetch = window.fetch.bind(window);
window.fetch = (url, options = {}) => {
  if (url === '/api/security/state') {
    window.securityStateRequests++;
    const prepared = window.securityStateResponses.shift();
    if (prepared) return Promise.resolve(prepared);
    return Promise.resolve(securityResponse(200, {...window.securityServerState}));
  }
  if (url === '/api/security/setup') {
    const request = Object.fromEntries(new URLSearchParams(options.body || ''));
    window.securityRequests.push({url, method:options.method, request, options});
    const prepared = window.securitySetupResponses.shift();
    if (prepared instanceof Error) return Promise.reject(prepared);
    if (prepared && !prepared.ok) return Promise.resolve(prepared);
    if (prepared && prepared._body?.ok !== true) return Promise.resolve(prepared);
    if (window.securityServerState.state !== 'legacy-unclaimed')
      return Promise.resolve(securityResponse(409, {error:'invalid_state'}));
    window.securityServerState = {
      state:request.mode, setupAllowed:false,
      loginAllowed:request.mode === 'protected', authenticated:false,
      protectionEnforced:false
    };
    return Promise.resolve(prepared || securityResponse(200, {ok:true}));
  }
  if (url === '/api/security/login') {
    const request = Object.fromEntries(new URLSearchParams(options.body || ''));
    window.securityRequests.push({url, method:options.method, request, options});
    const prepared = window.securityLoginResponses.shift() ||
      securityResponse(200, {ok:true});
    if (prepared.ok) window.securityServerState.authenticated = true;
    return Promise.resolve(prepared);
  }
  if (url === '/api/security/logout') {
    window.securityRequests.push({url, method:options.method, request:{}, options});
    window.securityServerState.authenticated = false;
    return Promise.resolve(securityResponse(204));
  }
  if (String(url).startsWith('/api/ready?'))
    return Promise.resolve({ok:true,status:200,json:() => Promise.resolve({
      ready:true, bootId:window.deviceReadinessBootId
    })});
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
  if (url === '/api/stations' && (options.method || 'GET') === 'GET')
    return Promise.resolve({ok:true,status:200,
      json:() => Promise.resolve(window.stationListResponse)});
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
  if (url === '/api/mqtt') {
    window.mqttRequests.push(options);
    if ((options.method || 'GET') === 'GET')
      return Promise.resolve({ok:true,status:200,json:() => Promise.resolve({...window.mqttSnapshot})});
    return Promise.resolve({ok:true,status:200,json:() => Promise.resolve({ok:true,restartRequired:true})});
  }
  if (url === '/api/time') {
    window.timeRequests++;
    return Promise.resolve({ok:true,status:200,json:() => Promise.resolve({rtcSupported:false})});
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
    const settleSecurity = async () => {
      await Promise.resolve();
      await new Promise(resolve => setTimeout(resolve, 0));
      await Promise.resolve();
    };
    await settleSecurity();
    check(!get('security-setup-form').hidden && get('app-shell').hidden,
      'legacy-unclaimed presents the first protection choice');
    check(webSocketInstances.length === 0,
      'normal WebSocket is not opened before security authorization');
    check(securityRequests.length === 0,
      'reading security state never submits setup automatically');
    get('security-setup-later').click();
    check(!get('app-shell').hidden && securityRequests.length === 0,
      'legacy user can defer setup without losing panel or OTA access');
    check(webSocketInstances.length === 1,
      'explicit legacy compatibility entry starts one WebSocket');
    await VoxOneSecurityUi.refresh();
    check(webSocketInstances[0].readyState === 3,
      'returning to the security gate closes an existing WebSocket');
    let passwordInfo = VoxOneSecurityUi.passwordInfo('żółw');
    check(passwordInfo.valid && passwordInfo.weak && passwordInfo.characters === 4 &&
      passwordInfo.bytes === 7, 'Unicode password counts characters and UTF-8 bytes');
    passwordInfo = VoxOneSecurityUi.passwordInfo('ą'.repeat(32));
    check(passwordInfo.valid && passwordInfo.bytes === 64,
      '64-byte UTF-8 password accepted by UI');
    check(!VoxOneSecurityUi.passwordInfo('ą'.repeat(33)).valid,
      'over-64-byte UTF-8 password rejected by UI');

    const setupForm = get('security-setup-form');
    const protectedMode = setupForm.querySelector('input[value="protected"]');
    const openMode = setupForm.querySelector('input[value="open"]');
    const setupPassword = get('security-setup-password');
    const setupPasswordToggle = document.querySelector(
      '[data-password-target="security-setup-password"]');
    setupPassword.value = 'żółw';
    setupPassword.focus();
    setupPassword.setSelectionRange(1, 3, 'forward');
    setupPasswordToggle.click();
    check(setupPassword.type === 'text' && setupPassword.value === 'żółw' &&
      setupPassword.selectionStart === 1 && setupPassword.selectionEnd === 3 &&
      setupPasswordToggle.getAttribute('aria-pressed') === 'true' &&
      setupPasswordToggle.getAttribute('aria-label') === 'Ukryj hasło',
      'show password preserves value, selection and accessible state');
    setupPasswordToggle.click();
    check(setupPassword.type === 'password' &&
      setupPasswordToggle.tagName === 'BUTTON' && setupPasswordToggle.type === 'button' &&
      setupPasswordToggle.getAttribute('aria-label') === 'Pokaż hasło',
      'native keyboard-accessible button hides password');
    openMode.checked = true;
    openMode.dispatchEvent(new Event('change', {bubbles:true}));
    setupForm.dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await settleSecurity();
    check(securityRequests.length === 0 &&
      get('security-setup-message').textContent.includes('Potwierdź'),
      'OPEN requires explicit confirmation and sends no premature POST');

    protectedMode.checked = true;
    protectedMode.dispatchEvent(new Event('change', {bubbles:true}));
    get('security-setup-password').value = 'żółw';
    get('security-setup-repeat').value = 'żółw';
    get('security-setup-password').dispatchEvent(new Event('input'));
    check(!get('security-weak-warning').hidden, '4-7 character password warning');
    window.securitySetupResponses.push(securityResponse(500, {error:'storage_error'}));
    setupForm.dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await settleSecurity();
    check(get('security-save-confirmation').hidden && !get('security-setup-form').hidden,
      'backend setup error never shows success');
    get('security-setup-password').value = 'żółw';
    get('security-setup-repeat').value = 'żółw';
    window.securitySetupResponses.push(new DOMException('timed out', 'AbortError'));
    setupForm.dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await settleSecurity();
    check(get('security-save-confirmation').hidden &&
      get('security-setup-message').textContent.includes('nie został potwierdzony'),
      'setup timeout never shows uncertain success');
    get('security-setup-password').value = 'żółw';
    get('security-setup-repeat').value = 'żółw';
    window.securitySetupResponses.push(securityResponse(200, {}));
    setupForm.dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await settleSecurity();
    check(get('security-save-confirmation').hidden &&
      get('security-setup-message').textContent.includes('nie potwierdziło'),
      'ambiguous HTTP 200 without backend confirmation never shows success');
    get('security-setup-password').value = 'żółw';
    get('security-setup-repeat').value = 'żółw';
    setupForm.dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await settleSecurity();
    const setupRequest = securityRequests.filter(item => item.url === '/api/security/setup').at(-1);
    check(setupRequest.method === 'POST' && setupRequest.request.mode === 'protected' &&
      setupRequest.request.password === 'żółw' &&
      setupRequest.options.credentials === 'same-origin' &&
      setupRequest.options.headers['Content-Type'] === 'application/x-www-form-urlencoded',
      'explicit PROTECTED setup uses UTF-8 form encoding');
    check(get('security-setup-password').value === '' &&
      get('security-setup-repeat').value === '' && !get('security-login-form').hidden,
      'setup passwords are cleared and protected state enters login');
    check(get('security-login-form').querySelectorAll('input').length === 1 &&
      get('security-login-password').type === 'password', 'login contains only a password field');
    check(document.querySelectorAll('.security-password-toggle').length === 3 &&
      [...document.querySelectorAll('.security-password-toggle')].every(button =>
        button.getAttribute('aria-pressed') === 'false' && button.tabIndex === 0),
      'all password fields have keyboard-accessible eye controls');
    check(document.activeElement === get('security-login-password'),
      'desktop login focuses password after display');
    check(!get('security-save-confirmation').hidden &&
      get('security-save-confirmation').textContent === 'Ustawienia ochrony zostały zapisane',
      'confirmed PROTECTED setup shows exact success message');
    get('security-save-confirmation').hidden = true;
    await VoxOneSecurityUi.refresh();
    check(get('security-save-confirmation').hidden,
      'ordinary state refresh does not repeat setup success');
    get('security-login-password').value = 'żółw';
    get('security-login-password').focus();
    get('security-login-password').setSelectionRange(2, 4);
    await VoxOneSecurityUi.refresh();
    check(document.activeElement === get('security-login-password') &&
      get('security-login-password').value === 'żółw' &&
      get('security-login-password').selectionStart === 2,
      'state refresh does not steal focus while user is typing');
    const loginPasswordToggle = document.querySelector(
      '[data-password-target="security-login-password"]');
    loginPasswordToggle.click();
    check(get('security-login-password').type === 'text' &&
      get('security-login-password').selectionStart === 2 &&
      loginPasswordToggle.getAttribute('aria-label') === 'Ukryj hasło',
      'login eye preserves current value and cursor');
    loginPasswordToggle.click();
    check(!location.href.includes('żółw') && localStorage.length === 0 && sessionStorage.length === 0,
      'password is absent from URL and browser storage');

    securityLoginResponses.push(securityResponse(401, {error:'authentication_failed'}));
    get('security-login-password').value = 'ą'.repeat(32);
    get('security-login-form').dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await settleSecurity();
    check(get('security-login-message').textContent === 'Nieprawidłowe hasło.' &&
      get('security-login-password').value === '', '401 is reported and password input cleared');
    check(securityRequests.at(-1).request.password === 'ą'.repeat(32),
      '64-byte UTF-8 password survives form encoding');
    securityLoginResponses.push(securityResponse(429, {error:'rate_limited'}, {'Retry-After':'60'}));
    get('security-login-password').value = 'żółw';
    get('security-login-form').dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await settleSecurity();
    check(get('security-login-message').textContent.includes('60 s'), '429 rate limit message');
    securityLoginResponses.push(securityResponse(200, {ok:true}));
    get('security-login-password').value = 'żółw';
    get('security-login-form').dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await settleSecurity();
    check(!get('app-shell').hidden && !get('security-logout').hidden,
      'successful login reveals panel and logout');
    const authenticatedSocket = window.btTestSocket;
    check(authenticatedSocket.readyState === MockWebSocket.OPEN,
      'authenticated UI opens WebSocket only after session confirmation');
    await VoxOneSecurityUi.refresh();
    check(!get('app-shell').hidden, 're-entering authenticated UI is deterministic');
    get('security-logout').click();
    await settleSecurity();
    check(!get('security-login-form').hidden &&
      securityRequests.at(-1).url === '/api/security/logout' &&
      securityRequests.at(-1).method === 'POST', 'logout invalidates current browser session');
    check(authenticatedSocket.readyState === 3,
      'logout closes the active WebSocket and suppresses reconnect');
    window.securityFinePointer = false;
    get('security-login-password').blur();
    window.securityServerState.authenticated = false;
    await VoxOneSecurityUi.refresh();
    await settleSecurity();
    check(document.activeElement !== get('security-login-password'),
      'mobile pointer does not force password focus or virtual keyboard');
    const securityGate = get('security-gate');
    securityGate.style.height = '180px';
    securityGate.style.minHeight = '0';
    check(getComputedStyle(securityGate).overflowY === 'auto' &&
      securityGate.scrollHeight > securityGate.clientHeight,
      'small dynamic viewport keeps login gate scrollable: ' +
      getComputedStyle(securityGate).overflowY + '/' + securityGate.scrollHeight +
      '/' + securityGate.clientHeight + '/hidden=' + securityGate.hidden);
    get('security-login-submit').scrollIntoView({block:'nearest'});
    const gateRect = securityGate.getBoundingClientRect();
    const loginButtonRect = get('security-login-submit').getBoundingClientRect();
    check(loginButtonRect.bottom <= gateRect.bottom + 1,
      'login button can be scrolled above mobile keyboard viewport');
    securityGate.removeAttribute('style');
    window.securityFinePointer = true;

    window.securityServerState = {
      state:'legacy-unclaimed', setupAllowed:true, loginAllowed:false,
      authenticated:false, protectionEnforced:false
    };
    await VoxOneSecurityUi.refresh();
    openMode.checked = true;
    openMode.dispatchEvent(new Event('change', {bubbles:true}));
    const setupCount = securityRequests.length;
    setupForm.dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await settleSecurity();
    check(securityRequests.length === setupCount, 're-entered OPEN still requires confirmation');
    get('security-open-confirm').checked = true;
    setupForm.dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await settleSecurity();
    check(window.securityServerState.state === 'open' && !get('app-shell').hidden,
      'confirmed OPEN setup returns to existing panel');
    check(!get('security-save-confirmation').hidden &&
      get('security-save-confirmation').textContent === 'Ustawienia ochrony zostały zapisane',
      'confirmed OPEN setup shows exact success message');
    window.securityServerState = {
      state:'unconfigured', setupAllowed:false, loginAllowed:false,
      authenticated:false, protectionEnforced:false
    };
    await VoxOneSecurityUi.refresh();
    check(!get('security-unconfigured').hidden, 'UNCONFIGURED state recognized');
    window.securityServerState.state = 'locked';
    await VoxOneSecurityUi.refresh();
    check(!get('security-locked').hidden, 'LOCKED state recognized');
    window.securityStateResponses.push(securityResponse(404, {error:'not_found'}));
    await VoxOneSecurityUi.refresh();
    check(!get('app-shell').hidden && !get('security-api-warning').hidden,
      'old MAIN without Security API fails open for panel and OTA migration');
    window.securityServerState = {
      state:'open', setupAllowed:false, loginAllowed:false,
      authenticated:false, protectionEnforced:false
    };
    await VoxOneSecurityUi.refresh();
    check(get('security-api-warning').hidden, 'successful re-entry clears compatibility warning');

    const deterministicWaiter = (responses, timeout = 2000) => {
      let clock = 0;
      const waiter = window.VoxOneRestartWaiter.create({
        now: () => clock,
        sleep: delay => { clock += delay; return Promise.resolve(); },
        probe: () => {
          const response = responses.shift();
          if (response instanceof Error) return Promise.reject(response);
          return Promise.resolve(response || {ready:true, bootId:'old-boot'});
        }
      });
      return waiter.wait({previousBootId:'old-boot', timeout, interval:500});
    };
    let restartResult = await deterministicWaiter([
      {ready:true, bootId:'old-boot'}, new Error('offline'),
      {ready:true, bootId:'new-boot'}
    ]);
    check(restartResult.status === 'ready' && restartResult.bootId === 'new-boot',
      'restart waiter observes a new boot after network loss');
    restartResult = await deterministicWaiter([{ready:true, bootId:'fast-new-boot'}]);
    check(restartResult.status === 'ready' && restartResult.bootId === 'fast-new-boot',
      'fast restart succeeds without requiring a sampled outage');
    window.deviceReadinessBootId = 'post-ota-boot';
    window.securityServerState = {
      state:'protected', setupAllowed:false, loginAllowed:true,
      authenticated:false, protectionEnforced:false
    };
    const postOtaReady = await (await fetch('/api/ready?update=post-ota')).json();
    check(postOtaReady.ready === true && postOtaReady.bootId === 'post-ota-boot',
      'public readiness reports new boot after OTA session loss');
    await VoxOneSecurityUi.refresh();
    check(!get('security-login-form').hidden && get('app-shell').hidden,
      'post-OTA session loss returns protected UI to login');
    window.securityServerState = {
      state:'open', setupAllowed:false, loginAllowed:false,
      authenticated:false, protectionEnforced:false
    };
    await VoxOneSecurityUi.refresh();
    await new Promise(resolve => setTimeout(resolve, 0));
    restartResult = await deterministicWaiter([], 1000);
    check(restartResult.status === 'timeout', 'unchanged HTTP readiness cannot report success');
    restartResult = await deterministicWaiter([
      new Error('offline'), new Error('offline'), new Error('offline')
    ], 1000);
    check(restartResult.status === 'timeout', 'network errors remain bounded by timeout');
    let repeatCalls = 0;
    const repeatWaiter = window.VoxOneRestartWaiter.create({
      probe: signal => {
        if (++repeatCalls > 1) return Promise.resolve({ready:true, bootId:'second-boot'});
        return new Promise((resolve, reject) => signal.addEventListener('abort',
          () => reject(new Error('cancelled')), {once:true}));
      }
    });
    const staleWait = repeatWaiter.wait({previousBootId:'old-boot'});
    await Promise.resolve();
    const currentWait = repeatWaiter.wait({previousBootId:'old-boot'});
    const [staleResult, currentResult] = await Promise.all([staleWait, currentWait]);
    check(staleResult.status === 'cancelled' && currentResult.status === 'ready',
      'repeated update cancels stale polling');
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
    check(get('metadata').hidden && !get('bt-metadata').hidden &&
      [...get('bt-metadata').querySelectorAll('.status-metadata-label')]
        .map(node => node.textContent).join(',') === 'Artysta,Utwór' &&
      get('playback-state').textContent === 'PLAY', 'BT metadata fields and playback');
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
    const headerBrand = document.querySelector('.topbar .brand .brand-logo');
    const securityBrand = document.querySelector('.security-brand .security-logo');
    check(icon?.type === 'image/svg+xml' && icon.href.includes('/voxone-symbol.svg?v=') &&
      headerBrand?.getAttribute('src')?.includes('/voxone-logo.svg?v=') &&
      headerBrand?.alt === 'VoxOne' && securityBrand?.getAttribute('src')?.includes('/voxone-logo.svg?v=') &&
      securityBrand?.alt === 'VoxOne', 'official VoxOne logo and symbol favicon');
    check(get('header-version').classList.contains('profile') &&
      get('header-version').textContent === 'Wersja 0.8.0-dev' &&
      get('header-profile').textContent === 'Profil L0', 'header version and hardware profile tiles');
    const footer = document.querySelector('.site-footer');
    check(footer.children.length === 2 && footer.children[0].textContent === 'VoxOne © 2026 PMW' &&
      footer.children[1].textContent === 'Support: pmw.voxone@gmail.com' &&
      footer.children[1].getAttribute('href') === 'mailto:pmw.voxone@gmail.com' &&
      !get('footer-details') && !get('footer-connection'), 'static support footer without runtime identity');
    const stationTransfer = document.querySelector('.station-transfer-actions');
    check(stationTransfer?.contains(get('station-import')) && stationTransfer.contains(get('station-export')) &&
      get('station-import').textContent === 'Import listy' &&
      get('station-export').textContent === 'Eksport listy' &&
      !!(get('station-list').compareDocumentPosition(stationTransfer) & Node.DOCUMENT_POSITION_FOLLOWING),
      'station import and export follow the station list');
    check([...get('hardware').querySelectorAll('h3')].some(node => node.textContent === 'Wyświetlacz') &&
      [...get('system').querySelectorAll('h3')].some(node => node.textContent === 'Przywracanie konfiguracji') &&
      [...get('update').querySelectorAll('dt')].some(node => node.textContent === 'Profil sprzętu') &&
      getComputedStyle(get('system').querySelector('h3')).textTransform === 'none',
      'approved card labels use sentence case');
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
    location.hash = '#stations';
    window.dispatchEvent(new Event('hashchange'));
    await settleSecurity();
    const longStationRows = [...get('station-list').querySelectorAll('.station-row')];
    const stationTransferActions = document.querySelector('.station-transfer-actions');
    check(longStationRows.length === 24 &&
      stationTransferActions.getBoundingClientRect().top >=
        longStationRows.at(-1).getBoundingClientRect().bottom,
      'long station list keeps import and export below its final row');
    location.hash = '#settings';
    window.dispatchEvent(new Event('hashchange'));
    await settleSecurity();
    check(btTestSocket.sent.some(item => item === 'getsystem=1'), 'network snapshot requested on Settings');
    check(get('mqtt-enabled').checked && get('mqtt-host').value === 'mqtt.local' &&
      get('mqtt-port').value === '1883' && get('mqtt-username').value === 'voxone',
      'existing MQTT form loads its current configuration');
    check(get('mqtt-password').value === '' &&
      get('mqtt-password-state').textContent.includes('Hasło zapisane'),
      'MQTT password remains write-only');
    get('mqtt-form').requestSubmit();
    await settleSecurity();
    const mqttPost = mqttRequests.find(request => request.method === 'POST');
    const mqttBody = new URLSearchParams(mqttPost?.body || '');
    check(!!mqttPost && mqttBody.get('host') === 'mqtt.local' &&
      !mqttBody.has('password') && !mqttBody.has('clearPassword') &&
      get('mqtt-feedback').textContent.includes('Konfiguracja zapisana'),
      'existing MQTT save preserves an undisclosed password');
    check(timeRequests > 0 && get('rtc-card').hidden,
      'RTC card follows the unsupported capability response');
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
    const settingsLayout = get('settings').querySelector('.settings-layout');
    const settingsColumns = [...settingsLayout.querySelectorAll(':scope > .settings-column')];
    check(settingsColumns.length === 2 &&
      settingsColumns[0].querySelectorAll(':scope > .card').length === 3 &&
      settingsColumns[1].querySelectorAll(':scope > .card').length === 3,
      'settings cards use balanced independent columns');
    check(get('network-profiles').querySelector('.is-active')?.querySelector('strong')?.textContent === 'HomeNet' &&
      get('network-profiles').querySelector('.is-active')?.getAttribute('aria-current') === 'true',
      'active Wi-Fi profile is identified without exposing credentials');
    check(networkCard.textContent.includes('Tylko podgląd') &&
      networkCard.textContent.includes('Edycja i włączanie profili nie są obecnie obsługiwane'),
      'network card describes the read-only contract');
    btTestSocket.onmessage({data:JSON.stringify({canFlip:1,flip:0,canBrightness:1,br:72,
      canVu:0,stationListTimeout:15,canBtTransport:0})});
    check(!get('display-settings-card').hidden && !get('brightness-control').hidden &&
      get('brightness-slider').value === '72' && get('brightness-value').textContent === '72%' &&
      get('brightness-unavailable').hidden,
      'TFT brightness follows the existing capability and value');
    get('brightness-slider').value = '64';
    get('brightness-slider').dispatchEvent(new Event('change'));
    get('station-list-timeout').value = '20';
    get('station-list-timeout').dispatchEvent(new Event('change'));
    get('flip-screen-toggle').checked = true;
    get('flip-screen-toggle').dispatchEvent(new Event('change'));
    check(btTestSocket.sent.some(item => item === 'dim=64') &&
      btTestSocket.sent.some(item => item === 'stationlisttimeout=20') &&
      btTestSocket.sent.some(item => item === 'flipscreen=1'),
      'existing display settings keep their WebSocket commands');
    btTestSocket.onmessage({data:JSON.stringify({flip:1,br:64,stationListTimeout:20})});
    btTestSocket.onmessage({data:JSON.stringify({canFlip:1,canBrightness:0,canVu:0})});
    check(!get('display-settings-card').hidden && get('brightness-control').hidden &&
      !get('brightness-unavailable').hidden,
      'display without brightness capability has no active brightness control');
    btTestSocket.onmessage({data:JSON.stringify({canFlip:0,canBrightness:0,canVu:0})});
    check(get('display-settings-card').hidden, 'display card remains capability gated');
    const scrollCommandsBeforeSnapshot = btTestSocket.sent.filter(item => item.startsWith('scrollsettings=')).length;
    const scrollSnapshot = (overrides = {}) => ({canScrollSettings:true,scrollEnabled:true,
      scrollSpeed:'normal',scrollPauseMs:5000,scrollSettingsSource:'rendererDefaults',
      scrollCoreActive:true,...overrides});
    btTestSocket.onmessage({data:JSON.stringify(scrollSnapshot())});
    check(!get('display-settings-card').hidden && !get('scroll-settings-form').hidden &&
      get('scroll-enabled').value === 'true' && get('scroll-pause').value === '5000' &&
      get('scroll-speed').value === 'normal' && get('scroll-settings-save').disabled &&
      get('scroll-settings-source').textContent.includes('wartości domyślne renderera'),
      'GETSCREEN loads renderer defaults without enabling an unnecessary save');
    check(btTestSocket.sent.filter(item => item.startsWith('scrollsettings=')).length ===
      scrollCommandsBeforeSnapshot, 'opening display settings does not write scroll settings');
    get('scroll-pause').value = '4000';
    get('scroll-pause').dispatchEvent(new Event('change'));
    btTestSocket.onmessage({data:JSON.stringify(scrollSnapshot())});
    check(get('scroll-pause').value === '4000' && !get('scroll-settings-save').disabled,
      'background GETSCREEN does not overwrite an unsaved complete scroll draft');
    get('scroll-pause').value = '5000';
    get('scroll-pause').dispatchEvent(new Event('change'));
    check(get('scroll-settings-save').disabled,
      'returning the draft to the saved values avoids an NVS write');
    for (const [profile, coreActive] of [
      ['L0',false],['M0',true],['S0',false],['DIN0',false],['W0',false]
    ]) {
      btTestSocket.onmessage({data:JSON.stringify(scrollSnapshot({scrollCoreActive:coreActive}))});
      check(!get('scroll-settings-form').hidden &&
        get('scroll-settings-status').textContent.includes(coreActive ? 'aktywnie stosowane' : 'nie stosuje ich aktywnie'),
        profile + ' follows GETSCREEN scroll capability and active-core state');
    }
    let expectedScrollCommands = scrollCommandsBeforeSnapshot;
    let savedScroll = {enabled:true,speed:'normal',pauseMs:5000};
    for (const enabled of [true,false]) for (const speed of ['slow','normal','fast'])
      for (const pauseMs of [0,1000,2000,3000,4000,5000]) {
        get('scroll-enabled').value = String(enabled);
        get('scroll-speed').value = speed;
        get('scroll-pause').value = String(pauseMs);
        get('scroll-speed').dispatchEvent(new Event('change'));
        const unchanged = enabled === savedScroll.enabled && speed === savedScroll.speed &&
          pauseMs === savedScroll.pauseMs;
        if (unchanged) {
          check(get('scroll-settings-save').disabled,
            'identical scroll settings do not enable a write');
          continue;
        }
        check(!get('scroll-settings-save').disabled, 'complete scroll selection enables save');
        get('scroll-settings-form').requestSubmit();
        expectedScrollCommands++;
        check(btTestSocket.sent.at(-1) ===
          'scrollsettings=' + enabled + ',' + speed + ',' + pauseMs &&
          btTestSocket.sent.filter(item => item.startsWith('scrollsettings=')).length === expectedScrollCommands &&
          get('scroll-settings-save').disabled,
          'atomic scroll command ' + enabled + '/' + speed + '/' + pauseMs);
        btTestSocket.onmessage({data:JSON.stringify({event:'scrollSettings.result',ok:true,error:'none',
          changed:true,persisted:true,runtimePublished:true,activeApplied:true,scrollCoreActive:true})});
        check(get('scroll-settings-status').textContent.includes('Zapisano i zastosowano') &&
          get('scroll-settings-save').disabled,
          'confirmed CORE=1 scroll update is active and no longer dirty');
        savedScroll = {enabled,speed,pauseMs};
      }
    btTestSocket.onmessage({data:JSON.stringify(scrollSnapshot({scrollEnabled:savedScroll.enabled,
      scrollSpeed:savedScroll.speed,scrollPauseMs:savedScroll.pauseMs,
      scrollSettingsSource:'userSettings',scrollCoreActive:false}))});
    get('scroll-enabled').value = String(!savedScroll.enabled);
    get('scroll-enabled').dispatchEvent(new Event('change'));
    get('scroll-settings-form').requestSubmit();
    expectedScrollCommands++;
    btTestSocket.onmessage({data:JSON.stringify({event:'scrollSettings.result',ok:true,error:'none',
      changed:true,persisted:true,runtimePublished:true,activeApplied:false,scrollCoreActive:false})});
    check(get('scroll-settings-status').textContent.includes('Zapisano ustawienia') &&
      get('scroll-settings-status').textContent.includes('nie stosuje ich aktywnie'),
      'CORE=0 distinguishes persistence from active application');
    savedScroll.enabled = !savedScroll.enabled;
    get('scroll-pause').value = savedScroll.pauseMs === 0 ? '1000' : '0';
    get('scroll-pause').dispatchEvent(new Event('change'));
    get('scroll-settings-form').requestSubmit();
    expectedScrollCommands++;
    btTestSocket.onmessage({data:JSON.stringify({event:'scrollSettings.result',ok:false,error:'busy',
      changed:false,persisted:false,runtimePublished:false,activeApplied:false,scrollCoreActive:false})});
    check(get('scroll-settings-status').textContent.includes('zajęte') &&
      !get('scroll-settings-save').disabled,
      'queue busy is explicit and keeps the validated draft available for retry');
    get('scroll-settings-form').requestSubmit();
    expectedScrollCommands++;
    btTestSocket.onmessage({data:JSON.stringify({event:'scrollSettings.result',ok:false,
      error:'storage_commit_failed',changed:false,persisted:false,runtimePublished:false,
      activeApplied:false,scrollCoreActive:false})});
    check(get('scroll-settings-status').textContent.includes('trwale zatwierdzić') &&
      !get('scroll-settings-save').disabled,
      'storage failure is not reported as a successful save');
    get('scroll-settings-form').requestSubmit();
    expectedScrollCommands++;
    btTestSocket.onmessage({data:JSON.stringify({event:'scrollSettings.result',ok:false,
      error:'reload_failed',changed:true,persisted:true,runtimePublished:false,
      activeApplied:false,scrollCoreActive:false})});
    check(get('scroll-settings-status').textContent.includes('przeładowanie') &&
      !get('scroll-settings-status').textContent.includes('Zapisano i zastosowano'),
      'persisted but unpublished settings are not reported as actively applied');
    const commandsBeforeInvalid = expectedScrollCommands;
    get('scroll-speed').value = '';
    get('scroll-settings-form').dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    check(btTestSocket.sent.filter(item => item.startsWith('scrollsettings=')).length === commandsBeforeInvalid &&
      get('scroll-settings-status').textContent.includes('poprawne wartości'),
      'invalid scroll form data is rejected before WebSocket send');
    btTestSocket.onmessage({data:JSON.stringify(scrollSnapshot({canScrollSettings:false,
      scrollEnabled:false,scrollSpeed:'normal',scrollPauseMs:0,
      scrollSettingsSource:'userSettings',scrollCoreActive:false}))});
    check(get('scroll-settings-form').hidden && get('display-settings-card').hidden,
      'Display NONE does not expose scroll controls');
    btTestSocket.onmessage({data:JSON.stringify(scrollSnapshot({scrollEnabled:savedScroll.enabled,
      scrollSpeed:savedScroll.speed,scrollPauseMs:savedScroll.pauseMs,
      scrollSettingsSource:'userSettings',scrollCoreActive:true}))});
    const sleepCard = get('sleep-card');
    check(!sleepCard.querySelector('input,button,select,textarea') &&
      sleepCard.textContent.includes('Niedostępne') &&
      sleepCard.textContent.includes('nie jest obecnie dostępna w WWW'),
      'sleep remains an explicit non-interactive placeholder');
    location.hash = '#hardware';
    window.dispatchEvent(new Event('hashchange'));
    await new Promise(resolve => setTimeout(resolve, 0));
    const hardwareDisplay = get('hardware-display');
    const hardwareInput = get('hardware-input');
    const hardwareAudio = get('hardware-audio');
    check(!get('hardware').hidden, 'SPRZĘT tab visible');
    check(get('hardware-family').textContent === 'M0' && get('hardware-mcu').textContent === 'ESP32-S3',
      'M0 hardware family from descriptor API');
    check([...hardwareDisplay.options].map(option => option.value).join(',') ===
      'none,ssd1306_128x64,ssd1309_128x64' &&
      [...hardwareDisplay.options].every(option => !option.disabled),
      'display dropdown contains only supported M0 capabilities');
    check([...hardwareInput.options].map(option => option.value).join(',') === 'none,ec11,buttons3',
      'input options follow capabilities');
    check([...hardwareInput.options].map(option => option.textContent).join(',') ===
      'Brak,Enkoder,Przyciski' &&
      [...hardwareInput.options].every(option => !option.disabled),
      'input labels use approved Polish names and remain selectable');
    check(document.querySelector('label[for="hardware-input"]').textContent === 'Tryb sterowania',
      'input field uses approved Polish label');
    const hardwareLayout = get('hardware').querySelector('.hardware-layout');
    const hardwareColumns = [...hardwareLayout.querySelectorAll(':scope > .hardware-column')];
    check(hardwareColumns.length === 2 &&
      hardwareColumns[0].querySelectorAll(':scope > .card').length === 2 &&
      hardwareColumns[1].querySelectorAll(':scope > .card').length === 2,
      'hardware cards use balanced independent columns');
    check(!hardwareDisplay.querySelector('option[value="ch1116_128x64"]') &&
      !hardwareAudio.querySelector('option[value="max98357a"]'),
      'planned display and audio choices are not presented');
    check([...hardwareAudio.options].map(option => option.value).join(',') ===
      'pcm5102a' &&
      [...hardwareAudio.options].map(option => option.textContent).join(',') ===
      'PCM5102A' && hardwareAudio.value === 'pcm5102a' && hardwareAudio.disabled,
      'audio is an informational disabled select containing only the current profile value');
    check(!hardwareAudio.name,
      'audio select remains excluded from configuration writes');
    check(!document.querySelector('.hardware-legend') &&
      !document.querySelector('.hardware-roadmap-status') &&
      !document.querySelector('.hardware-roadmap-current') &&
      !get('hardware-module-roadmap') &&
      !['DOSTĘPNE', 'DO TESTU', 'PLANOWANE', 'AKTUALNE'].some(label =>
        get('hardware').textContent.includes(label)),
      'hardware tab does not expose roadmap statuses, badges, or planned modules');
    check(hardwareDisplay.value === 'ssd1309_128x64' && hardwareInput.value === 'ec11',
      'M0 current SSD1309 and EC11 selections selected');
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
    check(!get('hardware-save-restart').disabled && !get('hardware-dirty-badge').hidden,
      'supported BUTTONS3 capability enables apply without exposing HIL status');
    hardwareInput.value = 'ec11';
    hardwareInput.dispatchEvent(new Event('change'));
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
    check(!document.querySelector('.site-footer').textContent.includes('192.168.1.42'),
      'footer ignores runtime IP');
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
    await new Promise(resolve => setTimeout(resolve, 0));
    check(get('update-progress').hidden && get('update-progress').value === 0 &&
      !get('update-firmware-button').disabled &&
      get('update-status').textContent.includes('ponowić'),
      'MAIN disconnect clears progress and permits retry');
    updateTransportOutcomes.push({event:'abort'});
    get('update-firmware-button').click();
    await new Promise(resolve => setTimeout(resolve, 0));
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
    await new Promise(resolve => setTimeout(resolve, 0));
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
    get('scroll-enabled').value = String(!savedScroll.enabled);
    get('scroll-enabled').dispatchEvent(new Event('change'));
    get('scroll-settings-form').requestSubmit();
    check(get('scroll-settings-status').textContent.includes('Zapisywanie'),
      'scroll save waits for the device result');
    btTestSocket.close();
    check(selector.querySelectorAll('button').length === 0 &&
      get('current-station-name').textContent === '—' && get('source').textContent === '—',
      'disconnect clears stale source and metadata');
    check(get('scroll-settings-form').hidden &&
      !get('scroll-settings-status').textContent.includes('Zapisano'),
      'WebSocket loss never reports an unconfirmed scroll save as success');
    check(muteButtons.every(button => button.disabled && button.getAttribute('aria-pressed') === 'false'), 'disconnect clears mute state');
    check(document.querySelector('.site-footer a').getAttribute('href') ===
      'mailto:pmw.voxone@gmail.com', 'footer remains static after disconnect');
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

RECOVERY_MOCK = r"""
<script>
window.recoveryLoginResponses = [];
window.recoveryStateResponses = [];
window.recoveryRequests = [];
window.recoveryRedirect = '';
window.recoveryResponse = (status, body = {}, headers = {}) => ({
  ok:status >= 200 && status < 300, status,
  headers:{get:name => headers[name] || headers[name.toLowerCase()] || null},
  json:() => Promise.resolve(body)
});
window.fetch = (url, options = {}) => {
  window.recoveryRequests.push({url, options});
  const queue = url === '/api/security/login'
    ? window.recoveryLoginResponses : window.recoveryStateResponses;
  const prepared = queue.shift();
  if (prepared instanceof Error) return Promise.reject(prepared);
  return Promise.resolve(prepared || window.recoveryResponse(500));
};
</script>
"""

RECOVERY_CHECK = r"""
<output id="recovery-headless-result">PENDING</output>
<script>
const recoveryCheck = (condition, message) => {
  if (!condition) throw new Error(message);
};
const recoverySettle = async () => {
  await new Promise(resolve => setTimeout(resolve, 0));
  await new Promise(resolve => setTimeout(resolve, 0));
};
setTimeout(async () => {
  const output = document.getElementById('recovery-headless-result');
  try {
    const form = document.getElementById('recovery-login');
    const password = document.getElementById('recovery-password');
    const message = document.getElementById('recovery-message');
    recoveryCheck(form.querySelectorAll('input').length === 1 &&
      password.type === 'password', 'recovery login must contain only password');

    recoveryLoginResponses.push(recoveryResponse(401));
    password.value = 'wrong-secret';
    form.dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await recoverySettle();
    recoveryCheck(message.textContent === 'Nieprawidłowe hasło.' &&
      password.value === '', '401 must clear password and show an error');
    const rejected = recoveryRequests.at(-1);
    recoveryCheck(rejected.url === '/api/security/login' &&
      rejected.options.method === 'POST' &&
      rejected.options.credentials === 'same-origin' &&
      rejected.options.headers['Content-Type'] ===
        'application/x-www-form-urlencoded' &&
      new URLSearchParams(rejected.options.body).get('password') ===
        'wrong-secret', 'recovery login must use the existing form API');

    recoveryLoginResponses.push(recoveryResponse(429, {}, {'Retry-After':'17'}));
    password.value = 'rate-limited';
    form.dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await recoverySettle();
    recoveryCheck(message.textContent.includes('17 s') && password.value === '',
      '429 must show retry time and clear password');

    recoveryLoginResponses.push(new Error('offline'));
    password.value = 'network-error';
    form.dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await recoverySettle();
    recoveryCheck(message.textContent === 'Brak połączenia z urządzeniem.' &&
      password.value === '', 'network failure must be reported');

    recoveryLoginResponses.push(recoveryResponse(200, {ok:true}));
    recoveryStateResponses.push(recoveryResponse(200, {
      state:'protected', authenticated:false
    }));
    password.value = 'valid-but-unconfirmed';
    form.dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await recoverySettle();
    recoveryCheck(recoveryRedirect === '' &&
      message.textContent === 'Nie udało się potwierdzić sesji.',
      'login response alone must not open recovery');

    recoveryLoginResponses.push(recoveryResponse(200, {ok:true}));
    recoveryStateResponses.push(recoveryResponse(200, {
      state:'protected', authenticated:true
    }));
    password.value = 'valid-secret';
    form.dispatchEvent(new Event('submit', {bubbles:true,cancelable:true}));
    await recoverySettle();
    recoveryCheck(recoveryRedirect === '/emergency' && password.value === '',
      'confirmed session must enter embedded updater');
    output.textContent = 'PASS';
  } catch (error) { output.textContent = 'FAIL: ' + error.message; }
}, 0);
</script>
"""

FIRST_BOOT_MOCK = r"""
<script>
window.scanRequests = [];
window.scanPollTimers = new Map();
let scanTimerSequence = 10000;
const scanNativeSetTimeout = window.setTimeout.bind(window);
const scanNativeClearTimeout = window.clearTimeout.bind(window);
window.setTimeout = (handler, delay, ...args) => {
  if (delay === 700) {
    const id = ++scanTimerSequence;
    window.scanPollTimers.set(id, () => handler(...args));
    return id;
  }
  return scanNativeSetTimeout(handler, delay, ...args);
};
window.clearTimeout = id => {
  if (window.scanPollTimers.delete(id)) return;
  scanNativeClearTimeout(id);
};
window.runNextScanPoll = () => {
  const next = window.scanPollTimers.entries().next();
  if (next.done) throw new Error('missing scan poll timer');
  const [id, handler] = next.value;
  window.scanPollTimers.delete(id);
  handler();
};
window.fetch = (url, options = {}) => new Promise((resolve, reject) => {
  const overlapAtStart = window.scanRequests.filter(
    request => !request.settled && !request.aborted).length;
  const request = {
    url:String(url), options, overlapAtStart, settled:false, aborted:false,
    resolve(status, body = {}) {
      request.settled = true;
      resolve({
        ok:status >= 200 && status < 300, status,
        json:() => Promise.resolve(body)
      });
    },
    reject(error) {
      request.settled = true;
      reject(error);
    }
  };
  options.signal?.addEventListener('abort', () => { request.aborted = true; });
  window.scanRequests.push(request);
});
</script>
"""

FIRST_BOOT_CHECK = r"""
<output id="first-boot-headless-result">PENDING</output>
<script>
const firstBootCheck = (condition, message) => {
  if (!condition) throw new Error(message);
};
const firstBootSettle = async () => {
  await Promise.resolve();
  await new Promise(resolve => setTimeout(resolve, 0));
  await Promise.resolve();
};
const scanReady = networks => ({status:'ready', networks});
setTimeout(async () => {
  const output = document.getElementById('first-boot-headless-result');
  const get = id => document.getElementById(id);
  try {
    const form = get('wifi-form');
    const list = get('ssid-list');
    const manual = get('manual-ssid');
    const hiddenSsid = get('ssid');
    const password = get('pass');
    const eye = get('pass-toggle');
    const scan = get('scan');
    const status = get('scan-status');
    const submit = () => {
      const event = new Event('submit', {bubbles:true, cancelable:true});
      form.dispatchEvent(event);
      return event;
    };

    firstBootCheck(window.scanRequests.length === 1 &&
      window.scanRequests[0].url === '/api/wifi/scan',
      'first boot must start one initial scan');
    window.scanRequests[0].resolve(200, scanReady([
      {ssid:'Home',rssi:-41,secure:true},
      {ssid:' Edge ',rssi:-53,secure:false},
      {ssid:'Spec & = + ąć',rssi:-62,secure:true}
    ]));
    await firstBootSettle();
    firstBootCheck(!scan.disabled && list.options.length === 4,
      'initial scan results');

    let submitted = 0;
    form.addEventListener('submit', () => { submitted++; });
    password.value = 'unchanged-secret';
    password.focus();
    password.setSelectionRange(2, 8, 'forward');
    eye.click();
    firstBootCheck(password.type === 'text' &&
      password.value === 'unchanged-secret' && password.selectionStart === 2 &&
      password.selectionEnd === 8 && eye.type === 'button' && submitted === 0 &&
      eye.getAttribute('aria-pressed') === 'true' &&
      eye.getAttribute('aria-label') === 'Ukryj hasło',
      'eye shows password without submit or value change');
    eye.focus();
    eye.click();
    firstBootCheck(password.type === 'password' && eye.tabIndex === 0 &&
      eye.getAttribute('aria-label') === 'Pokaż hasło' &&
      eye.getBoundingClientRect().width >= 40 &&
      eye.getBoundingClientRect().height >= 40,
      'native button is keyboard focusable and hides password');

    list.value = 'Home';
    list.dispatchEvent(new Event('change', {bubbles:true}));
    firstBootCheck(manual.value === '' && !submit().defaultPrevented &&
      hiddenSsid.value === 'Home', 'list selection is authoritative');

    password.value = 'preserve-selected-secret';
    scan.click();
    let request = window.scanRequests.at(-1);
    request.resolve(200, scanReady([
      {ssid:' Edge ',rssi:-54,secure:false},
      {ssid:'Spec & = + ąć',rssi:-63,secure:true}
    ]));
    await firstBootSettle();
    firstBootCheck(list.value === 'Home' && manual.value === '' &&
      password.value === 'preserve-selected-secret' &&
      [...list.options].some(option => option.value === 'Home'),
      'rescan never overwrites an explicit list selection');

    manual.value = ' Hidden SSID ';
    manual.dispatchEvent(new Event('input', {bubbles:true}));
    firstBootCheck(list.value === '' && !submit().defaultPrevented &&
      hiddenSsid.value === ' Hidden SSID ',
      'manual mode preserves legal surrounding spaces');

    list.value = ' Edge ';
    list.dispatchEvent(new Event('change', {bubbles:true}));
    firstBootCheck(manual.value === '' && !submit().defaultPrevented &&
      hiddenSsid.value === ' Edge ',
      'switching back to the list clears stale manual SSID');

    list.value = 'Spec & = + ąć';
    list.dispatchEvent(new Event('change', {bubbles:true}));
    password.value = 'p&=+ ąć';
    firstBootCheck(!submit().defaultPrevented, 'special characters accepted');
    const encoded = new URLSearchParams(new FormData(form)).toString();
    const decoded = new URLSearchParams(encoded);
    firstBootCheck(decoded.get('ssid') === 'Spec & = + ąć' &&
      decoded.get('pass') === 'p&=+ ąć' && form.method === 'post' &&
      form.getAttribute('action') === '/' && !location.href.includes('p%26') &&
      !location.href.includes('ąć') && localStorage.length === 0 &&
      sessionStorage.length === 0,
      'native POST encoding preserves special characters without URL/storage leak');

    list.value = '';
    list.dispatchEvent(new Event('change', {bubbles:true}));
    manual.value = '';
    manual.dispatchEvent(new Event('input', {bubbles:true}));
    firstBootCheck(submit().defaultPrevented && hiddenSsid.value === '' &&
      status.textContent.includes('Wybierz sieć'), 'empty SSID rejected');

    manual.value = 's'.repeat(29);
    manual.dispatchEvent(new Event('input', {bubbles:true}));
    password.value = 'p'.repeat(39);
    firstBootCheck(!submit().defaultPrevented &&
      hiddenSsid.value === 's'.repeat(29), '29/39-byte boundary accepted');
    manual.value = 's'.repeat(30);
    manual.dispatchEvent(new Event('input', {bubbles:true}));
    firstBootCheck(submit().defaultPrevented && status.textContent.includes('30 bajtów'),
      'overlong ASCII SSID rejected without truncation');
    manual.value = 'ą'.repeat(15);
    manual.dispatchEvent(new Event('input', {bubbles:true}));
    firstBootCheck(submit().defaultPrevented && status.textContent.includes('30 bajtów'),
      'UTF-8 SSID limit counts bytes instead of characters');
    manual.value = 'Hidden';
    manual.dispatchEvent(new Event('input', {bubbles:true}));
    password.value = 'p'.repeat(40);
    firstBootCheck(submit().defaultPrevented && status.textContent.includes('40 bajtów'),
      'overlong password rejected without truncation');

    password.value = 'keep-during-scan';
    scan.click();
    request = window.scanRequests.at(-1);
    firstBootCheck(request.url === '/api/wifi/scan?refresh=1',
      'rescan starts a fresh server cycle');
    request.resolve(200, scanReady([]));
    await firstBootSettle();
    firstBootCheck(!scan.disabled && password.value === 'keep-during-scan' &&
      manual.value === 'Hidden' && list.value === '' &&
      status.textContent.includes('Nie znaleziono'),
      'empty scan preserves password and manual hidden SSID');

    scan.click();
    request = window.scanRequests.at(-1);
    request.resolve(500, {status:'error'});
    await firstBootSettle();
    firstBootCheck(!scan.disabled && password.value === 'keep-during-scan' &&
      status.textContent.includes('Nie udało się'),
      'scan error permits manual entry and retry');

    scan.click();
    request = window.scanRequests.at(-1);
    request.resolve(504, {status:'error',error:'timeout',networks:[]});
    await firstBootSettle();
    firstBootCheck(!scan.disabled &&
      status.textContent.includes('przekroczyło limit czasu') &&
      password.value === 'keep-during-scan',
      'server timeout is explicit and permits retry');

    scan.click();
    request = window.scanRequests.at(-1);
    request.resolve(202, {status:'scanning',networks:[]});
    await firstBootSettle();
    let timeoutGuard = 0;
    while (scan.disabled && timeoutGuard++ < 30) {
      firstBootCheck(window.scanPollTimers.size === 1,
        'one pending poll timer per cycle');
      window.runNextScanPoll();
      await firstBootSettle();
      request = window.scanRequests.at(-1);
      request.resolve(202, {status:'scanning',networks:[]});
      await firstBootSettle();
    }
    firstBootCheck(timeoutGuard < 30 && !scan.disabled &&
      status.textContent.includes('przekroczyło limit czasu') &&
      password.value === 'keep-during-scan',
      'timeout is explicit and retains credentials');

    scan.click();
    request = window.scanRequests.at(-1);
    request.resolve(200, scanReady([{ssid:'After timeout',rssi:-50,secure:true}]));
    await firstBootSettle();
    firstBootCheck(!scan.disabled &&
      [...list.options].some(option => option.value === 'After timeout'),
      'rescan after timeout resets the poll counter');

    scan.click();
    const stale = window.scanRequests.at(-1);
    scan.dispatchEvent(new Event('click'));
    const fresh = window.scanRequests.at(-1);
    firstBootCheck(stale !== fresh && stale.aborted &&
      fresh.overlapAtStart === 0,
      'new cycle aborts the previous request before starting');
    fresh.resolve(200, scanReady([{ssid:'Fresh',rssi:-48,secure:true}]));
    await firstBootSettle();
    stale.resolve(200, scanReady([{ssid:'Stale',rssi:-20,secure:false}]));
    await firstBootSettle();
    firstBootCheck([...list.options].some(option => option.value === 'Fresh') &&
      ![...list.options].some(option => option.value === 'Stale') &&
      window.scanRequests.every(item => item.overlapAtStart === 0),
      'late result from an obsolete scan is ignored');

    output.textContent = 'PASS';
  } catch (error) {
    output.textContent = 'FAIL: ' + error.message;
  }
}, 0);
</script>
"""


def main():
    chrome = resolve_chrome()
    html = (ROOT / "web-src/voxone.html").read_text(encoding="utf-8")
    css = (ROOT / "web-src/voxone.css").read_text(encoding="utf-8")
    route = (ROOT / "src/core/netserver.cpp").read_text(encoding="utf-8")
    access_policy = (ROOT / "src/core/security_access_policy.cpp").read_text(
        encoding="utf-8")
    http_adapter = (ROOT / "src/core/security_http_adapter.cpp").read_text(
        encoding="utf-8")
    http_adapter_header = (ROOT / "src/core/security_http_adapter.h").read_text(
        encoding="utf-8")
    netserver_header = (ROOT / "src/core/netserver.h").read_text(
        encoding="utf-8")
    platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    assert '"/voxone.html"' in access_policy and \
           'SecurityRouteClass::PublicBootstrap' in access_policy, \
        "login bootstrap assets must remain public in the prepared policy"
    compressed_assets = (
        "advanced-audio.js", "dsp-client.js", "voxone-logo.svg",
        "voxone-symbol.svg", "voxone.css", "voxone.html", "voxone.js")
    expected_assets = {
        asset: (ROOT / "web-src" / asset).read_bytes()
        for asset in compressed_assets
    }
    generated_html = expected_assets["voxone.html"].decode("utf-8")
    for asset in compressed_assets:
        if asset == "voxone.html":
            continue
        digest = hashlib.sha256(expected_assets[asset]).hexdigest()[:12]
        generated_html = generated_html.replace(f"{{{asset}-hash}}", digest)
    expected_assets["voxone.html"] = generated_html.encode("utf-8")
    for asset in compressed_assets:
        assert f'"/{asset}.gz"' in access_policy, \
            f"security policy inventory missing rejected direct /{asset}.gz"
        stored = gzip.decompress((ROOT / "data/www" / f"{asset}.gz").read_bytes())
        assert stored == expected_assets[asset], \
            f"canonical gzip fallback asset {asset} must match its source"
    assert "RejectedDirectCompressedAsset" in access_policy and \
           "DirectCompressedAssetDenied" in access_policy, \
        "direct compressed asset URLs must have an explicit deny policy"
    for protected_path in ("/variables.js", "/api/stations", "/ws",
                           "/update", "/update/bt"):
        assert f'"{protected_path}"' in access_policy, \
            f"security policy inventory missing {protected_path}"
    ready_rule = re.search(
        r'"/api/ready"\s*,\s*SecurityEndpoint::Ready\s*,\s*'
        r'SecurityRouteClass::PublicBootstrap', access_policy)
    assert ready_rule, \
        "/api/ready must remain a minimal public restart probe"
    gate_index = route.find("webserver.addHandler(new SecurityHttpAccessGate())")
    update_guard_index = route.find("webserver.addHandler(new WebUpdateRequestGuard())")
    first_route_index = route.find('webserver.on("/", HTTP_ANY')
    assert -1 < gate_index < update_guard_index < first_route_index, \
        "security HTTP gate must precede update guard and every route"
    assert "securityWebSocketHandshakeStatus" in http_adapter and \
           "websocketHandshake" in http_adapter_header and \
           "validateBinding" in route, \
        "SECURITY-ENFORCE-1C must enforce handshake and live WS bindings"
    assert "VOXONE_SECURITY_HTTP_ENFORCEMENT 0" in http_adapter_header and \
           "VOXONE_SECURITY_HTTP_ENFORCEMENT_TEST 0" in http_adapter_header, \
        "ordinary builds must keep HTTP enforcement disabled"
    assert "[env:m0-security-test]" in platformio and \
           "-DVOXONE_SECURITY_HTTP_ENFORCEMENT_TEST=1" in platformio, \
        "dedicated M0 enforcement test profile is missing"
    recovery_match = re.search(
        r'const char security_recovery_login\[\] PROGMEM = '
        r'R"VOXONE_RECOVERY\((.*?)\)VOXONE_RECOVERY";',
        netserver_header, re.S)
    assert recovery_match, "embedded recovery login page is missing"
    recovery_html = recovery_match.group(1)
    assert len(re.findall(r'<input\b', recovery_html)) == 1 and \
           'type="password"' in recovery_html, \
        "embedded recovery login must contain only a password input"
    assert '<script src=' not in recovery_html and \
           'https://' not in recovery_html and 'http://' not in recovery_html, \
        "embedded recovery login must not use external assets"
    assert "fetch('/api/security/login'" in recovery_html and \
           "fetch('/api/security/state'" in recovery_html and \
           "window.location.replace('/emergency')" in recovery_html, \
        "embedded recovery login must confirm the cookie session before recovery"
    first_boot_match = re.search(
        r'const char first_boot_setup_html\[\] PROGMEM = R"\((.*?)\)";',
        netserver_header, re.S)
    assert first_boot_match, "embedded first-boot Wi-Fi page is missing"
    first_boot_html = first_boot_match.group(1)
    assert '<script src=' not in first_boot_html and \
           '<link rel="stylesheet"' not in first_boot_html and \
           'https://' not in first_boot_html and 'http://' not in first_boot_html, \
        "first-boot form must remain independent of SPIFFS and Internet assets"
    assert 'method="post"' in first_boot_html and \
           "fetch('/api/wifi/scan'" in first_boot_html and \
           'new TextEncoder()' in first_boot_html and \
           'manual.value.trim()' not in first_boot_html and \
           'maxlength=' not in first_boot_html, \
        "first-boot form must use byte validation without silent truncation"
    assert 'id="pass-toggle"' in first_boot_html and \
           'class="eye-slash"' in first_boot_html and \
           'type="button"' in first_boot_html and \
           'aria-label="Poka&#380; has&#322;o"' in first_boot_html, \
        "first-boot password eye must be an accessible native button"
    assert 'console.' not in first_boot_html and \
           'localStorage' not in first_boot_html and \
           'sessionStorage' not in first_boot_html, \
        "first-boot form must not log or persist credentials"
    assert re.search(r'webserver\.on\("/favicon\.ico", HTTP_GET,.*?'
                     r'request->redirect\("/voxone-symbol\.svg"\);', route, re.S), \
        "favicon.ico must redirect to the official symbol"
    scripts = [
        (ROOT / "web-src" / name).read_text(encoding="utf-8")
        for name in ("advanced-audio.js", "dsp-client.js", "voxone.js")
    ]
    html = re.sub(r'<link rel="stylesheet" href="/voxone\.css[^"]*">',
                  "<style>" + css + "</style>", html, count=1)
    html = re.sub(r'<script src="/variables.js"></script>', MOCK, html, count=1)
    html = re.sub(
        r'<script src="/(?:advanced-audio|dsp-client|voxone)\.js[^"]*" defer></script>',
        "", html)
    html = html.replace("</body>", '<output id="headless-result">PENDING</output>' +
                        "".join("<script>" + script + "</script>" for script in scripts) +
                        CHECK + "</body>")
    recovery_test_html = recovery_html.replace(
        "window.location.replace('/emergency')",
        "window.recoveryRedirect='/emergency'")
    recovery_test_html = recovery_test_html.replace(
        "</head>", RECOVERY_MOCK + "</head>", 1)
    recovery_test_html = recovery_test_html.replace(
        "</body>", RECOVERY_CHECK + "</body>", 1)
    first_boot_test_html = first_boot_html.replace(
        "</head>", FIRST_BOOT_MOCK + "</head>", 1)
    first_boot_test_html = first_boot_test_html.replace(
        "</body>", FIRST_BOOT_CHECK + "</body>", 1)
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
        recovery_page = Path(temporary) / "recovery-test.html"
        recovery_page.write_text(recovery_test_html, encoding="utf-8")
        recovery_command = [
            str(chrome), "--headless", "--disable-gpu",
            "--disable-gpu-compositing",
            "--disable-features=Vulkan,UseSkiaRenderer,CanvasOopRasterization",
            "--disable-extensions", "--no-first-run",
            "--no-default-browser-check", "--allow-file-access-from-files",
            "--user-data-dir=" + str(Path(temporary) / "recovery-profile"),
            "--virtual-time-budget=1500", "--dump-dom", recovery_page.as_uri()
        ]
        recovery_result = subprocess.run(
            recovery_command, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=30)
        first_boot_page = Path(temporary) / "first-boot-test.html"
        first_boot_page.write_text(first_boot_test_html, encoding="utf-8")
        first_boot_command = [
            str(chrome), "--headless", "--disable-gpu",
            "--disable-gpu-compositing",
            "--disable-features=Vulkan,UseSkiaRenderer,CanvasOopRasterization",
            "--disable-extensions", "--no-first-run",
            "--no-default-browser-check", "--allow-file-access-from-files",
            "--user-data-dir=" + str(Path(temporary) / "first-boot-profile"),
            "--virtual-time-budget=2500", "--dump-dom", first_boot_page.as_uri()
        ]
        first_boot_result = subprocess.run(
            first_boot_command, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=30)
        first_boot_mobile_command = [
            str(chrome), "--headless", "--disable-gpu",
            "--disable-gpu-compositing",
            "--disable-features=Vulkan,UseSkiaRenderer,CanvasOopRasterization",
            "--disable-extensions", "--no-first-run",
            "--no-default-browser-check", "--allow-file-access-from-files",
            "--window-size=390,844",
            "--user-data-dir=" + str(Path(temporary) / "first-boot-mobile-profile"),
            "--virtual-time-budget=2500", "--dump-dom", first_boot_page.as_uri()
        ]
        first_boot_mobile_result = subprocess.run(
            first_boot_mobile_command, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=30)
    match = re.search(r'<output id="headless-result">([^<]+)</output>', result.stdout)
    outcome = match.group(1) if match else "FAIL: Chrome did not return the test result"
    print(outcome)
    if outcome != "PASS":
        print(result.stderr[-2000:])
        raise SystemExit(1)
    recovery_match = re.search(
        r'<output id="recovery-headless-result">([^<]+)</output>',
        recovery_result.stdout)
    recovery_outcome = recovery_match.group(1) if recovery_match else \
        "FAIL: Chrome did not return the recovery result"
    print("Recovery " + recovery_outcome)
    if recovery_outcome != "PASS":
        print(recovery_result.stderr[-2000:])
        raise SystemExit(1)
    first_boot_match = re.search(
        r'<output id="first-boot-headless-result">([^<]+)</output>',
        first_boot_result.stdout)
    first_boot_outcome = first_boot_match.group(1) if first_boot_match else \
        "FAIL: Chrome did not return the first-boot result"
    print("First boot " + first_boot_outcome)
    if first_boot_outcome != "PASS":
        print(first_boot_result.stderr[-2000:])
        raise SystemExit(1)
    first_boot_mobile_match = re.search(
        r'<output id="first-boot-headless-result">([^<]+)</output>',
        first_boot_mobile_result.stdout)
    first_boot_mobile_outcome = first_boot_mobile_match.group(1) \
        if first_boot_mobile_match else \
        "FAIL: Chrome did not return the mobile first-boot result"
    print("First boot mobile viewport " + first_boot_mobile_outcome)
    if first_boot_mobile_outcome != "PASS":
        print(first_boot_mobile_result.stderr[-2000:])
        raise SystemExit(1)


if __name__ == "__main__":
    try:
        main()
    except FileNotFoundError as error:
        raise SystemExit(str(error)) from error
