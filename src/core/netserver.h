#ifndef netserver_h
#define netserver_h
#include "../AsyncWebServer/ESPAsyncWebServer.h"
enum requestType_e : uint8_t  { STATION=2, STATIONNAME=3, ITEM=4, TITLE=5, VOLUME=6, NRSSI=7, BITRATE=8, MODE=9, EQUALIZER=10, BALANCE=11, GETINDEX=14, GETSYSTEM=16, GETSCREEN=17, WEBSTATUS=28 };
const char first_boot_setup_html[] PROGMEM = R"(
<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#0b121b">
<title>VoxOne &#8212; Konfiguracja Wi-Fi</title>
<style>
:root{color-scheme:dark;--bg:#0b121b;--panel:#151f2b;--panel-2:#1b2937;--line:#314456;--text:#edf5fa;--muted:#a2b5c5;--cyan:#71d8e8;--cyan-dark:#173f4b}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;background:var(--bg);color:var(--text);font:16px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif;display:flex;flex-direction:column}
main{width:calc(100% - 32px);max-width:480px;margin:auto}
.logo{display:block;width:min(260px,72%);height:auto;margin:0 auto 22px}
h2{margin:0 0 12px;font-size:1.3rem}
p{margin:0 0 20px;color:var(--muted)}
.card{background:var(--panel);border:1px solid var(--line);border-radius:15px;padding:24px;margin:20px 0}
label{display:block;margin:16px 0 6px;font-weight:600}
input[type=text],input[type=password],select{display:block;width:100%;min-width:0;min-height:46px;padding:12px;border:1px solid var(--line);border-radius:10px;background:var(--panel-2);color:var(--text);font:inherit}
input::placeholder{color:var(--muted);opacity:1}
input:focus-visible,select:focus-visible,button:focus-visible{outline:2px solid var(--cyan);outline-offset:2px}
button{width:100%;min-height:46px;margin-top:22px;padding:13px;border:1px solid var(--cyan);border-radius:10px;background:var(--cyan-dark);color:var(--text);font:650 1rem system-ui,sans-serif;cursor:pointer}
.secondary{margin-top:10px;background:transparent}.status{min-height:1.5em;margin:10px 0 0}
.status[data-kind=error]{color:#ffb4ae}
.password-control{position:relative}.password-control input{padding-right:54px}.password-toggle{position:absolute;top:3px;right:3px;width:42px;min-height:40px;margin:0;padding:8px;border:0;background:transparent}.password-toggle svg{display:block;width:23px;height:23px;margin:auto;fill:none;stroke:currentColor;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round}.password-toggle .eye-slash{display:none}.password-toggle[aria-pressed=true] .eye-slash{display:block}
details{margin:24px 0;color:var(--muted)}
summary{cursor:pointer;color:var(--cyan)}
.service{margin-top:16px;padding-top:16px;border-top:1px solid var(--line)}
a{color:var(--cyan)}
footer{text-align:center;padding:16px;color:var(--muted);font-size:.85rem}
.hidden{display:none}
@media(max-width:480px){.card{padding:20px}}
</style>
</head>
<body>
<main>
<img class="logo" src="/voxone-logo.svg" alt="VoxOne" width="207" height="50">
<section class="card" id="wifi-setup">
<h2>Konfiguracja</h2>
<p>Wybierz sie&#263; Wi-Fi. Po zapisaniu VoxOne uruchomi si&#281; ponownie i otworzy panel bez has&#322;a.</p>
<form id="wifi-form" action="/" method="post">
<label for="ssid-list">Dost&#281;pne sieci</label>
<select id="ssid-list"><option value="">Skanowanie sieci&#8230;</option></select>
<button id="scan" class="secondary" type="button">Skanuj ponownie</button>
<p id="scan-status" class="status" role="status" aria-live="polite"></p>
<details id="manual"><summary>Sie&#263; ukryta / wpisz SSID r&#281;cznie</summary>
<label for="manual-ssid">Nazwa sieci (SSID)</label>
<input id="manual-ssid" type="text" autocomplete="off" autocapitalize="off" spellcheck="false"></details>
<input id="ssid" name="ssid" type="hidden">
<label for="pass">Has&#322;o</label>
<div class="password-control"><input id="pass" name="pass" type="password" autocomplete="new-password" autocapitalize="off" spellcheck="false"><button id="pass-toggle" class="password-toggle" type="button" aria-label="Poka&#380; has&#322;o" aria-pressed="false"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M2.5 12s3.5-6 9.5-6 9.5 6 9.5 6-3.5 6-9.5 6-9.5-6-9.5-6Z"/><circle cx="12" cy="12" r="2.7"/><path class="eye-slash" d="m4 4 16 16"/></svg></button></div>
<button type="submit">Zapisz i po&#322;&#261;cz</button>
</form>
</section>
<details>
<summary>Tryb serwisowy / Recovery</summary>
<div class="service">
<p>Awaryjnie mo&#380;esz wgra&#263; firmware lub pe&#322;ny obraz systemu plik&#243;w z buildu VoxOne.</p>
<p><a href="/emergency">Awaryjna aktualizacja firmware / WWW</a></p>
</div>
</details>
</main>
<footer>VoxOne &#8212; Konfiguracja</footer>
<script>
(function(){
var list=document.getElementById('ssid-list'),status=document.getElementById('scan-status'),manual=document.getElementById('manual-ssid'),scan=document.getElementById('scan'),form=document.getElementById('wifi-form'),ssid=document.getElementById('ssid'),pass=document.getElementById('pass'),passToggle=document.getElementById('pass-toggle'),mode='',cycle=0,polls=0,pollTimer=0,requestController=null,maxPolls=24;
function show(text,error){status.textContent=text;status.dataset.kind=error&&text?'error':'status'}
function validUnicode(value){for(var index=0;index<value.length;++index){var unit=value.charCodeAt(index);if(unit>=0xd800&&unit<=0xdbff){if(++index>=value.length)return false;var low=value.charCodeAt(index);if(low<0xdc00||low>0xdfff)return false}else if(unit>=0xdc00&&unit<=0xdfff)return false}return true}
function byteLength(value){return validUnicode(value)?new TextEncoder().encode(value).length:-1}
function addOption(value,text){var option=document.createElement('option');option.value=value;option.textContent=text;list.appendChild(option)}
function renderNetworks(networks){var selected=mode==='list'?list.value:'',found=false;list.textContent='';addOption('',networks.length?'Wybierz sieć':'Nie znaleziono sieci');networks.forEach(function(network){if(!network||typeof network.ssid!=='string'||!network.ssid.length)return;addOption(network.ssid,network.ssid+' ('+network.rssi+' dBm, '+(network.secure?'zabezpieczona':'otwarta')+')');if(network.ssid===selected)found=true});if(selected&&!found)addOption(selected,selected+' (wybrana wcześniej)');list.value=mode==='list'?selected:''}
function finishScan(activeCycle){if(activeCycle!==cycle)return;if(pollTimer){clearTimeout(pollTimer);pollTimer=0}requestController=null;polls=0;scan.disabled=false}
async function pollScan(activeCycle,refresh){
 if(activeCycle!==cycle)return;
 requestController=new AbortController();
 try{
  var response=await fetch('/api/wifi/scan'+(refresh?'?refresh=1':''),{cache:'no-store',signal:requestController.signal});
  if(activeCycle!==cycle)return;
  requestController=null;
  if(response.status===202){if(++polls>=maxPolls){show('Skanowanie przekroczyło limit czasu. Spróbuj ponownie lub wpisz SSID ręcznie.',true);finishScan(activeCycle);return}pollTimer=setTimeout(function(){pollTimer=0;pollScan(activeCycle,false)},700);return}
  if(response.status===504){show('Skanowanie przekroczyło limit czasu. Spróbuj ponownie lub wpisz SSID ręcznie.',true);finishScan(activeCycle);return}
  if(!response.ok)throw new Error('scan');
  var data=await response.json();if(activeCycle!==cycle)return;if(!data||!Array.isArray(data.networks))throw new Error('response');renderNetworks(data.networks);show(data.networks.length?'Znaleziono sieci: '+data.networks.length:'Nie znaleziono sieci. Możesz wpisać SSID ręcznie.');finishScan(activeCycle)
 }catch(error){if(activeCycle!==cycle)return;requestController=null;if(error&&error.name==='AbortError')return;show('Nie udało się odczytać listy. Wpisz SSID ręcznie lub spróbuj ponownie.',true);finishScan(activeCycle)}
}
function startScan(refresh){++cycle;polls=0;if(pollTimer){clearTimeout(pollTimer);pollTimer=0}if(requestController)requestController.abort();requestController=null;scan.disabled=true;show('Skanowanie sieci Wi-Fi...');pollScan(cycle,refresh)}
function setPasswordVisible(visible){var start=pass.selectionStart,end=pass.selectionEnd,direction=pass.selectionDirection;pass.type=visible?'text':'password';passToggle.setAttribute('aria-pressed',visible?'true':'false');passToggle.setAttribute('aria-label',visible?'Ukryj hasło':'Pokaż hasło');pass.focus({preventScroll:true});if(start!==null&&end!==null)pass.setSelectionRange(start,end,direction||'none')}
scan.addEventListener('click',function(){startScan(true)});
list.addEventListener('change',function(){ssid.value='';if(list.value.length){mode='list';manual.value='';show('Wybrano sieć z listy.')}else{mode='';show('Wybierz sieć lub wpisz SSID ręcznie.')}});
manual.addEventListener('input',function(){ssid.value='';list.value='';mode=manual.value.length?'manual':'';show(mode==='manual'?'Tryb ręczny: użyte zostanie wpisane SSID.':'Wybierz sieć lub wpisz SSID ręcznie.')});
passToggle.addEventListener('click',function(){setPasswordVisible(pass.type==='password')});
form.addEventListener('submit',function(event){var value=mode==='manual'?manual.value:mode==='list'?list.value:'';var ssidBytes=byteLength(value),passBytes=byteLength(pass.value);ssid.value='';if(!value.length){event.preventDefault();show('Wybierz sieć lub wpisz SSID ręcznie.',true);return}if(ssidBytes<0||passBytes<0){event.preventDefault();show('SSID lub hasło zawiera nieprawidłowy tekst Unicode.',true);return}if(/[\u0000\t\r\n]/.test(value)||/[\u0000\t\r\n]/.test(pass.value)){event.preventDefault();show('SSID i hasło nie mogą zawierać tabulatorów ani znaków końca linii.',true);return}if(ssidBytes>29){event.preventDefault();show('SSID ma '+ssidBytes+' bajtów UTF-8. Maksymalnie 29.',true);return}if(passBytes>39){event.preventDefault();show('Hasło ma '+passBytes+' bajtów UTF-8. Maksymalnie 39.',true);return}ssid.value=value});
startScan(false);
})();
</script>
</body>
</html>
)";
const char emptyfs_html[] PROGMEM = R"(
<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#0b121b">
<title>VoxOne &#8212; Konfiguracja Wi-Fi</title>
<style>
:root{color-scheme:dark;--bg:#0b121b;--panel:#151f2b;--panel-2:#1b2937;--line:#314456;--text:#edf5fa;--muted:#a2b5c5;--cyan:#71d8e8;--cyan-dark:#173f4b}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;background:var(--bg);color:var(--text);font:16px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif;display:flex;flex-direction:column}
main{width:calc(100% - 32px);max-width:480px;margin:auto}
h1{margin:0;color:var(--cyan);font-size:2.4rem;letter-spacing:.03em}
h2{margin:0 0 12px;font-size:1.3rem}
p{margin:0 0 20px;color:var(--muted)}
.card{background:var(--panel);border:1px solid var(--line);border-radius:15px;padding:24px;margin:20px 0}
label{display:block;margin:16px 0 6px;font-weight:600}
input[type=text],input[type=password]{display:block;width:100%;min-width:0;padding:12px;border:1px solid var(--line);border-radius:10px;background:var(--panel-2);color:var(--text);font:inherit}
input::placeholder{color:var(--muted);opacity:1}
input:focus-visible,button:focus-visible{outline:2px solid var(--cyan);outline-offset:2px}
button{width:100%;min-height:46px;margin-top:22px;padding:13px;border:1px solid var(--cyan);border-radius:10px;background:var(--cyan-dark);color:var(--text);font:650 1rem system-ui,sans-serif;cursor:pointer}
details{margin:24px 0;color:var(--muted)}
summary{cursor:pointer;color:var(--cyan)}
.service{margin-top:16px;padding-top:16px;border-top:1px solid var(--line)}
a{color:var(--cyan)}
footer{text-align:center;padding:16px;color:var(--muted);font-size:.85rem}
.hidden{display:none}
@media(max-width:480px){.card{padding:20px}h1{font-size:2rem}}
</style>
<script src="/variables.js"></script>
</head>
<body>
<main>
<h1>VoxOne</h1>
<section class="card" id="wifi-setup">
<h2>Konfiguracja Wi-Fi</h2>
<p>Nie uda&#322;o si&#281; po&#322;&#261;czy&#263; z zapisanymi sieciami po kilku pr&#243;bach. Dane pozosta&#322;y zapisane.</p>
<form action="/" method="post" id="wifi-retry">
<input name="retry" type="hidden" value="saved">
<button type="submit">Pon&#243;w zapisane po&#322;&#261;czenie</button>
</form>
<p>Je&#380;eli dane s&#261; b&#322;&#281;dne lub sie&#263; zosta&#322;a zmieniona, wprowad&#378; nowe ustawienia.</p>
<form action="/" method="post" id="wifi-form">
<label for="ssid">Nazwa sieci (SSID)</label>
<input id="ssid" name="ssid" type="text" maxlength="29" autocomplete="off" required>
<label for="pass">Has&#322;o</label>
<input id="pass" name="pass" type="password" maxlength="39" autocomplete="new-password">
<button type="submit">Zapisz i po&#322;&#261;cz</button>
</form>
</section>
<details>
<summary>Tryb serwisowy / Recovery</summary>
<div class="service">
<p>Awaryjnie mo&#380;esz wgra&#263; firmware lub pe&#322;ny obraz systemu plik&#243;w z buildu VoxOne.</p>
<p><a href="/emergency">Awaryjna aktualizacja firmware / WWW</a></p>
</div>
</details>
</main>
<footer id="version">VoxOne</footer>
<script>
if(typeof playMode !== 'undefined' && playMode === 'player') document.getElementById('wifi-setup').classList.add('hidden');
if(typeof voxOneVersion !== 'undefined') document.getElementById('version').textContent = 'VoxOne ' + voxOneVersion;
</script>
</body>
</html>
)";
const char security_recovery_login[] PROGMEM = R"VOXONE_RECOVERY(
<!doctype html>
<html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0b121b"><title>VoxOne — logowanie awaryjne</title>
<style>:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;min-height:100vh;padding:24px;background:#0b121b;color:#edf5fa;font:16px/1.5 system-ui,sans-serif;display:flex}main{width:100%;max-width:480px;margin:auto}h1{margin:0;color:#71d8e8;font-size:2.4rem}.card{padding:24px;margin:20px 0;border:1px solid #314456;border-radius:15px;background:#151f2b}p{color:#a2b5c5}label,input,button{display:block;width:100%}label{margin:16px 0 6px;font-weight:600}input{padding:12px;border:1px solid #314456;border-radius:10px;background:#1b2937;color:#edf5fa;font:inherit}input:focus-visible,button:focus-visible{outline:2px solid #71d8e8;outline-offset:2px}button{min-height:46px;margin-top:22px;padding:13px;border:1px solid #71d8e8;border-radius:10px;background:#173f4b;color:#edf5fa;font:650 1rem system-ui,sans-serif;cursor:pointer}button:disabled{opacity:.65;cursor:wait}.message{min-height:1.5em;margin:14px 0 0;color:#ffb4ab}@media(max-width:480px){.card{padding:20px}h1{font-size:2rem}}</style>
</head><body><main><h1>VoxOne</h1><section class="card"><h2>Logowanie awaryjne</h2>
<p>Pliki WWW są niedostępne. Zaloguj się, aby otworzyć wbudowany aktualizator.</p>
<form id="recovery-login"><label for="recovery-password">Hasło</label>
<input id="recovery-password" name="password" type="password" autocomplete="current-password" required autofocus>
<button id="recovery-submit" type="submit">Zaloguj</button><p id="recovery-message" class="message" role="alert" aria-live="polite"></p></form>
</section></main><script>
(function(){
  'use strict';
  var form=document.getElementById('recovery-login');
  var password=document.getElementById('recovery-password');
  var submit=document.getElementById('recovery-submit');
  var message=document.getElementById('recovery-message');
  function show(text){message.textContent=text}
  form.addEventListener('submit',async function(event){
    event.preventDefault();
    submit.disabled=true;show('Logowanie…');
    var body=new URLSearchParams();body.set('password',password.value);password.value='';
    try{
      var response=await fetch('/api/security/login',{method:'POST',credentials:'same-origin',cache:'no-store',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body.toString()});
      if(response.status===401){show('Nieprawidłowe hasło.');return}
      if(response.status===429){var seconds=parseInt(response.headers.get('Retry-After')||'60',10);show('Zbyt wiele prób. Spróbuj ponownie za '+(Number.isFinite(seconds)?seconds:60)+' s.');return}
      if(!response.ok){show('Logowanie nie powiodło się.');return}
      var stateResponse=await fetch('/api/security/state',{credentials:'same-origin',cache:'no-store'});
      var state=stateResponse.ok?await stateResponse.json():null;
      if(!state||state.state!=='protected'||state.authenticated!==true){show('Nie udało się potwierdzić sesji.');return}
      window.location.replace('/emergency');
    }catch(error){show('Brak połączenia z urządzeniem.')}
    finally{password.value='';submit.disabled=false}
  });
})();
</script></body></html>
)VOXONE_RECOVERY";
const char emergency_form[] PROGMEM = R"(
<!doctype html>
<html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0b121b"><title>VoxOne — tryb awaryjny</title>
<style>:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;padding:24px;background:#0b121b;color:#edf5fa;font:16px/1.5 system-ui,sans-serif}main{max-width:640px;margin:auto}h1{color:#71d8e8}section{padding:20px;margin:16px 0;border:1px solid #314456;border-radius:14px;background:#151f2b}label,input,button{display:block;width:100%}label{margin:12px 0 5px;color:#a2b5c5}input{padding:10px;border:1px solid #314456;border-radius:9px;background:#1b2937;color:#edf5fa}button{margin-top:16px;padding:12px;border:1px solid #71d8e8;border-radius:9px;background:#173f4b;color:#edf5fa;font:inherit;font-weight:700}p{color:#a2b5c5}</style>
</head><body><main><h1>VoxOne</h1><p>Awaryjna aktualizacja. Ta strona działa bez plików WWW w pamięci urządzenia.</p>
<section><h2>Firmware</h2><form method="POST" action="/update" enctype="multipart/form-data">
<input type="hidden" name="updatetarget" value="firmware"><input type="hidden" name="filesize" value="0">
<label for="emergency-firmware">Plik firmware .bin</label><input id="emergency-firmware" type="file" name="update" accept=".bin" required>
<button type="submit">Aktualizuj firmware</button></form></section>
<section><h2>WWW / system plików</h2><form method="POST" action="/update" enctype="multipart/form-data">
<input type="hidden" name="updatetarget" value="spiffs"><input type="hidden" name="filesize" value="0">
<label for="emergency-spiffs">Obraz SPIFFS .bin</label><input id="emergency-spiffs" type="file" name="update" accept=".bin" required>
<button type="submit">Aktualizuj WWW</button></form></section>
<p>Użyj obrazu z buildu VoxOne. Plik full.bin służy wyłącznie do odzyskiwania przez esptool.</p>
</main><script>document.querySelectorAll('form').forEach(function(form){form.addEventListener('submit',function(event){var file=form.elements.update.files[0];if(!file||!file.name.toLowerCase().endsWith('.bin')||file.name.toLowerCase().endsWith('full.bin')){event.preventDefault();alert('Wybierz właściwy obraz .bin, nie full.bin.');return}form.elements.filesize.value=String(file.size)})});</script></body></html>
)";
struct nsRequestParams_t
{
  requestType_e type;
  uint32_t clientId;
};

class NetServer {
  public:
    char chunkedPathBuffer[40];
    char nsBuf[BUFLEN], nsBuf2[BUFLEN];
  public:
    NetServer() {};
    bool begin(bool quiet=false);
    bool started() const { return _started; }
    void loop();
    void serviceBtFirmwareUpdate();  // Called from the main loop after BtLink::loop().
    void serviceUpdateRestart();  // MAIN-loop-only, after update state publication.
    void serviceScrollSettings();  // MAIN-loop-only; may access NVS.
    void requestOnChange(requestType_e request, uint32_t clientId);
    void setRSSI(int val) { rssi = val; };
    int  getRSSI()        { return rssi; };
    void chunkedHtmlPage(const String& contentType, AsyncWebServerRequest *request, const char * path);
    void onWsMessage(void *arg, const uint8_t *data, size_t len,
                     uint32_t clientId);
    void resetQueue();
  private:
    bool _started = false;
    requestType_e request;
    QueueHandle_t nsQueue;
    char _wscmd[65], _wsval[65];
    char wsBuf[BUFLEN*14];
    int rssi;
    uint32_t playerBufMax;
    volatile bool _volumeUpdatePending = false;
    uint32_t _lastVolumeUpdate = 0;
    static size_t chunkedHtmlPageCallback(uint8_t* buffer, size_t maxLen, size_t index);
    void processQueue();
    void processVolumeUpdate();
};

bool restoreWebUpdateData();
bool systemRestartPending();
void requestSystemRestart();
bool securityWebSocketText(uint32_t clientId, const char* message);
void securityWebSocketTextAll(const char* message);
extern NetServer netserver;
extern AsyncWebSocket websocket;

#endif
