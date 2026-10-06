#ifndef netserver_h
#define netserver_h
#include "../AsyncWebServer/ESPAsyncWebServer.h"
#define APPEND_GROUP(name) strcat(nsBuf, "\"" name "\",")

enum requestType_e : uint8_t  { PLAYLIST=1, STATION=2, STATIONNAME=3, ITEM=4, TITLE=5, VOLUME=6, NRSSI=7, BITRATE=8, MODE=9, EQUALIZER=10, BALANCE=11, PLAYLISTSAVED=12, STARTUP=13, GETINDEX=14, GETACTIVE=15, GETSYSTEM=16, GETSCREEN=17, GETTIMEZONE=18, DSPON=21, SDPOS=22, SDLEN=23, SDSNUFFLE=24, SDINIT=25, GETPLAYERMODE=26, CHANGEMODE=27, WEBSTATUS=28 };
enum import_e      : uint8_t  { IMDONE=0, IMPL=1, IMWIFI=2 };
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
input[type=file]{display:block;max-width:100%;margin:8px 0 18px;color:var(--muted)}
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
<p>Wprowad&#378; dane swojej sieci Wi-Fi. VoxOne zapisze ustawienia i uruchomi si&#281; ponownie.</p>
<form action="/" method="post">
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
<p>Je&#347;li pliki WWW s&#261; niedost&#281;pne, prze&#347;lij je z obrazu projektu. Mo&#380;esz te&#380; przywr&#243;ci&#263; kopi&#281; wifi.csv.</p>
<form action="/webboard" method="post" enctype="multipart/form-data">
<label for="www">Pliki WWW</label>
<input id="www" name="www" type="file" multiple>
<label for="data">Kopia wifi.csv</label>
<input id="data" name="data" type="file">
<button type="submit">Prze&#347;lij pliki</button>
</form>
<p><a href="/emergency">Awaryjna aktualizacja firmware</a></p>
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
const char index_html[] PROGMEM = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <meta name="theme-color" content="#e3d25f">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-status-bar-style" content="default">
  <link rel="icon" type="image/png" href="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAMAAACdt4HsAAAAYFBMVEUAAADYw1PcyVjYxFTaxlXYxFTbx1bcyVjZxVXbyFfcyFfaxlbax1bcyVjcyVjbyFfbyFfZxVXaxlbbx1fcyFjcyVjbx1fZxVXcyFjcyVjax1bbyFfcyVjbyFfax1bWwVKMlHGzAAAAH3RSTlMA+wv0zu6dBeVqSryjMRaCU97Fjz8liNk5HbFdDnWsEHoUsAAAAeFJREFUWMPtlllyrDAMRS1P2NjMQzc9RPvf5Ut1IPYjDRbJR1KVnD8Z7i1ZsgXsh1JW3usrC9Ta+2og620DiCjaaY65U4AIpqLqBb7R3B5xJucYRpI+U7jgHwsVLgjSLu74DmSvMTdhQVMMHAYeBhiQFAO5Y3CiGFzWBhDilmKQ4zsqm5uwQGvkCRfsytFkJIOhWWo+vz8uCfWMRqEVAJwsn+PsKgFA+YJR4UWe50Oc1Gt8vrFfyGC19153+afUvVMA+ADAaH5QXhvA/wB3yEICfgAqsvys8BngiPor4AaSpM8BN7lQRrrAbcBSLvMeKqmvVhtYh8mxqjCi7Tnnk4YDKYzRy9DPA2Uy9CoYDBShsCrKitxCnUUnm7qHFwyUYTlOAXYHWxP0TTzBbm1UBGIPfMkDZRcMur1bFPdAxEQPXhI1TNLSj+HxK9l9u8H41RrcKQZub5THbdxA7M3WAZL/EvRp0PDPGEgM9CxBqo9mYMcpAAPyzNZMx2aysUUWzYSi7lzSwALGGG3rvO/zurajM4BQJh0aXAGglACYg2v6uw64h2ZJfOIcp2lxh4ZgkEncRjAKF8AtYCI53M2mQc1IlNrAM7lyZ0akHKURsVaokxuLYxfD6ot8w+nOFuyP5/wDsZKME0E1GogAAAAASUVORK5CYII=">
  <link rel="stylesheet" href="theme.css" type="text/css" />
  <link rel="stylesheet" href="style.css" type="text/css" />
  <script type="text/javascript" src="variables.js"></script>
  <script type="text/javascript" src="script.js"></script>
  <script type="text/javascript" src="dragpl.js"></script>
  </head>
<body>
<div id="content" class="hidden progmem">
</div><!--content-->
<div id="progress"><span id="loader"></span></div>
<div id="heap"></div>
</body>
</html>
)";
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
    import_e importRequest;
    bool resumePlay;
    char chunkedPathBuffer[40];
    char nsBuf[BUFLEN], nsBuf2[BUFLEN];
  public:
    NetServer() {};
    bool begin(bool quiet=false);
    void loop();
    void requestOnChange(requestType_e request, uint32_t clientId);
    void setRSSI(int val) { rssi = val; };
    int  getRSSI()        { return rssi; };
    void chunkedHtmlPage(const String& contentType, AsyncWebServerRequest *request, const char * path);
    void onWsMessage(void *arg, uint8_t *data, size_t len, uint32_t clientId);
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
    void getPlaylist(uint32_t clientId);
    bool importPlaylist();
    static size_t chunkedHtmlPageCallback(uint8_t* buffer, size_t maxLen, size_t index);
    void processQueue();
    void processVolumeUpdate();
    int _readPlaylistLine(File &file, char * line, size_t size);
};

bool restoreWebUpdateData();
bool systemRestartPending();
void requestSystemRestart();
extern NetServer netserver;
extern AsyncWebSocket websocket;

#endif
