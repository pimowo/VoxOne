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
window.voxOneProfile = 'a0';
window.voxOneVersion = '0.2.0';
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
</script>
"""
CHECK = r"""
<script>
setTimeout(() => {
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
    check(get('footer-details').textContent === 'VoxOne 0.2.0 · A0', 'footer identity');
    check(get('footer-connection').textContent === 'Połączono', 'footer WebSocket connected');
    check(get('system').querySelector('h3').textContent === 'VoxOne', 'VoxOne system card');
    check([...get('system').querySelectorAll('h3')].some(node => node.textContent === 'VoxOneBT'), 'VoxOneBT system card');
    ['Czas pracy', 'Wolna pamięć heap', 'Możliwości', 'Protokół', 'Nazwa BT'].forEach(label =>
      check([...get('system').querySelectorAll('dt')].some(node => node.textContent === label), 'system label ' + label));
    ['VoxOne Firmware', 'VoxOne system plików', 'VoxOneBT Firmware'].forEach(label =>
      check([...get('update').querySelectorAll('h3')].some(node => node.textContent === label), label));
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
    btTestSocket.receiveNetwork({hostname:'voxone-a0', activeSsid:'HomeNet', profiles:[
      {ssid:'HomeNet', passwordSet:true, password:'secret', order:1},
      {ssid:'backup', passwordSet:false, order:2}
    ]});
    check(get('network-hostname').textContent === 'voxone-a0', 'network hostname');
    check(get('network-active-ssid').textContent === 'HomeNet', 'active Wi-Fi SSID');
    check(get('network-profiles').textContent.includes('Hasło: zapisane'), 'passwordSet true');
    check(get('network-profiles').textContent.includes('Hasło: brak'), 'passwordSet false');
    check(get('network-profiles').textContent.includes('kolejność prób: 2'), 'profile order');
    check(!networkCard.querySelector('input,button,select,textarea'), 'network card has no unsupported edit controls');
    check(!get('network-profiles').textContent.includes('secret'), 'no password value rendered');
    check(get('update-bt-online').textContent === 'TAK', 'online, phone disconnected');
    check(get('update-bt-firmware').textContent === '0.6.1-dev', 'firmware');
    check(get('update-bt-protocol').textContent === '2', 'protocol');
    check(get('update-bt-name').textContent === 'VoxOneBT-EFF35A', 'name');
    check(get('update-bt-capabilities').textContent === 'AVRCP,VU', 'capabilities');
    check(get('system-bt-online').textContent === 'TAK', 'system BT online while phone disconnected');
    check(get('system-bt-firmware').textContent === '0.6.1-dev', 'system BT firmware');
    location.hash = '#system';
    window.dispatchEvent(new Event('hashchange'));
    check(btTestSocket.sent.some(item => item === 'getsystem=1'), 'system snapshot request');
    check(btTestSocket.sent.some(item => item === 'getrssi=1'), 'system RSSI request');
    btTestSocket.onmessage({data:JSON.stringify({ipaddr:'192.168.1.42',
      payload:[{id:'rssi',value:-57}]})});
    check(get('footer-details').textContent === 'VoxOne 0.2.0 · A0 · 192.168.1.42', 'footer IP');
    check(get('system-version').textContent === '0.2.0-dev', 'identity firmware fallback');
    check(get('system-build').textContent === '192a400', 'identity build');
    check(get('update-version').textContent === 'VoxOne 0.2.0-dev', 'update firmware');
    check(get('update-build').textContent === '192a400', 'update build');
    check(get('update-profile').textContent === 'A0', 'update profile');
    check(get('system-profile').textContent === 'A0', 'identity profile fallback');
    check(get('system-ip').textContent === '192.168.1.42', 'legacy IP field');
    check(get('system-rssi').textContent === '-57 dBm', 'legacy RSSI field');
    check(get('system-psram').textContent !== 'Brak', 'missing snapshot is not no-PSRAM');
    const systemInfo = {mac:'AA:BB:CC:DD:EE:FF', rssi:-57,
      uptimeSeconds:4*3600+31*60+18,
      freeHeap:256000, minimumFreeHeap:128000, psramTotal:8388608, psramFree:4194304,
      capabilities:'DISPLAY, ENCODER, BT, VU, RTC'};
    btTestSocket.receiveSystem(systemInfo);
    check(get('system-version').textContent === '0.2.0-dev', 'system firmware');
    check(get('system-profile').textContent === 'A0', 'system profile');
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
    btTestSocket.close();
    check(selector.querySelectorAll('button').length === 0 &&
      get('current-station-name').textContent === '—' && get('source').textContent === '—',
      'disconnect clears stale source and metadata');
    check(muteButtons.every(button => button.disabled && button.getAttribute('aria-pressed') === 'false'), 'disconnect clears mute state');
    check(get('footer-connection').textContent === 'Rozłączono', 'footer WebSocket disconnected');
    check(get('footer-details').textContent === 'VoxOne 0.2.0 · A0', 'offline footer hides stale IP');
    check(get('network-active-ssid').textContent === '—', 'offline network info cleared');
    const transfer = new DataTransfer();
    transfer.items.add(new File(['image'], 'voxonebt.bin', {type:'application/octet-stream'}));
    get('update-bt-file').files = transfer.files;
    get('update-bt-file').dispatchEvent(new Event('change'));
    check(get('update-bt-file-name').textContent === 'voxonebt.bin', 'file name');
    check(get('update-bt-button').disabled, 'update must stay disabled');
    status.btModule = {online:false, firmware:'', protocol:0, name:'', capabilities:''};
    btTestSocket.receive(status);
    check(get('update-bt-online').textContent === 'NIE', 'offline');
    check(get('system-bt-online').textContent === 'NIE', 'system BT offline');
    ['firmware','protocol','name','capabilities'].forEach(field =>
      check(get('update-bt-' + field).textContent === '—', field + ' placeholder'));
    ['firmware','protocol','name','capabilities'].forEach(field =>
      check(get('system-bt-' + field).textContent === '—', 'system ' + field + ' placeholder'));
    setTimeout(() => {
      try {
        check(btTestSocket.readyState === MockWebSocket.OPEN, 'WebSocket reconnected');
        check(muteButtons.every(button => button.disabled), 'reconnect waits for snapshot');
        check(selector.querySelectorAll('button').length === 0, 'source reconnect waits for snapshot');
        btTestSocket.receive(sourceSnapshot('WEB', ['radio','bt'], true));
        check(sourceButton('radio').getAttribute('aria-pressed') === 'true' &&
          !sourceButton('bt').disabled, 'source restored after reconnect');
        check(muteButtons.every(button => !button.disabled && button.getAttribute('aria-pressed') === 'true'),
          'reconnect mute restored atomically with source');
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
