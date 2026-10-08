"""Exercise the update tab with the production HTML/JS in headless Chrome."""

from pathlib import Path
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
window.voxOneProfile = 'salon';
window.voxOneVersion = '0.2.0';
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
    check(get('footer-details').textContent === 'VoxOne 0.2.0 · SALON', 'footer identity');
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
    const status = {source:'BT', name:'Telefon', metadata:'', artist:'', title:'',
      codec:'', playback:'STOP', bitrate:0, sampleRate:44100, btConnected:false,
      btModule:{online:true, firmware:'0.6.1-dev', protocol:2,
                name:'VoxOneBT-EFF35A', capabilities:'AVRCP,VU'}};
    btTestSocket.receive(status);
    btTestSocket.receiveNetwork({hostname:'voxone-salon', activeSsid:'HomeNet', profiles:[
      {ssid:'HomeNet', passwordSet:true, password:'secret', order:1},
      {ssid:'backup', passwordSet:false, order:2}
    ]});
    check(get('network-hostname').textContent === 'voxone-salon', 'network hostname');
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
    check(get('footer-details').textContent === 'VoxOne 0.2.0 · SALON · 192.168.1.42', 'footer IP');
    check(get('system-version').textContent === 'VoxOne 0.2.0', 'identity firmware fallback');
    check(get('system-profile').textContent === 'SALON', 'identity profile fallback');
    check(get('system-ip').textContent === '192.168.1.42', 'legacy IP field');
    check(get('system-rssi').textContent === '-57 dBm', 'legacy RSSI field');
    check(get('system-psram').textContent !== 'Brak', 'missing snapshot is not no-PSRAM');
    const systemInfo = {mac:'AA:BB:CC:DD:EE:FF', rssi:-57,
      uptimeSeconds:4*3600+31*60+18,
      freeHeap:256000, minimumFreeHeap:128000, psramTotal:8388608, psramFree:4194304,
      capabilities:'DISPLAY, ENCODER, BT, VU, RTC'};
    btTestSocket.receiveSystem(systemInfo);
    check(get('system-version').textContent === 'VoxOne 0.2.0', 'system firmware');
    check(get('system-profile').textContent === 'SALON', 'system profile');
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
    check(get('footer-connection').textContent === 'Rozłączono', 'footer WebSocket disconnected');
    check(get('footer-details').textContent === 'VoxOne 0.2.0 · SALON', 'offline footer hides stale IP');
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
    result.textContent = 'PASS';
  } catch (error) { result.textContent = 'FAIL: ' + error.message; }
}, 100);
</script>
"""


def main():
    chrome = resolve_chrome()
    html = (ROOT / "web-src/voxone.html").read_text(encoding="utf-8")
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
