# VoxOne — HTTP, WebSocket i stan urządzenia

## Zakres dokumentu

Dokument opisuje kontrakt wynikający z kodu źródłowego VoxOne w rewizji
`ba0819f0fbe61e89dcfd26e17230268abaf21fe1` (`1.0.0`, kanał `rc1`). Drzewo
robocze podczas audytu było zmodyfikowane; opis uwzględnia widoczne źródła,
ale nie przypisuje niezacommitowanym zmianom osobnej wersji API.

Główny serwer nasłuchuje nieszyfrowanego HTTP na porcie `80`. Nie ma HTTPS.
Adres bazowy to zatem `http://<IP-lub-hostname>/`, a WebSocket używa
`ws://<IP-lub-hostname>/ws`.

Najważniejsze źródła kontraktu:

- rejestracja tras, formaty odpowiedzi i WebSocket:
  `src/core/netserver.cpp`, `src/core/netserver.h`;
- klasyfikacja i autoryzacja tras:
  `src/core/security_access_policy.cpp`, `src/core/security_http_adapter.*`;
- komendy WebSocket: `src/core/commandhandler.cpp`,
  `src/core/websocket_command.cpp`, `src/core/dsp_transport_protocol.*`;
- stan SECURITY: `src/core/security_settings.*`, `src/core/security_api.*`,
  `src/core/security_session.*`;
- toolchain: `platformio.ini`.

## Wspólne zasady HTTP

W standardowych profilach W0, M0, S0, DIN0 i L0 działa centralna polityka
SECURITY. Każde żądanie musi mieć dokładnie jeden poprawny `Host`: lokalny
adres IP urządzenia, skonfigurowany hostname albo `hostname.local`, opcjonalnie
z portem `:80`. Mutacje (`POST`, `PUT`, `DELETE`, `PATCH`) wymagają również
dokładnie jednego zgodnego nagłówka `Origin: http://<Host>`. Nie jest włączone
CORS.

Formularze SECURITY wymagają
`Content-Type: application/x-www-form-urlencoded` i mają limit 256 B.
Pozostałe formularze WWW również są kodowane jako
`application/x-www-form-urlencoded` albo `multipart/form-data`, zgodnie z
opisem endpointu.

W stanie `protected` trasy chronione wymagają cookie:

```text
voxone_session=<32 małe znaki hex>
```

Cookie jest ustawiane jako `Path=/; HttpOnly; SameSite=Strict;
Max-Age=86400`. Nie ma flagi `Secure`, ponieważ transport jest HTTP. Sesje są
wyłącznie w RAM: maksymalnie 2, timeout bezczynności 8 h, limit absolutny 24 h;
restart urządzenia je usuwa. Pięć błędnych logowań w oknie 5 min blokuje
logowanie na 60 s.

Opcjonalne makra `HTTP_USER`/`HTTP_PASS` dodają starszą warstwę HTTP Basic
Auth, ale nie są zdefiniowane w repozytoryjnym `myoptions.h`; standardowy
kontrakt opiera się na SECURITY i cookie.

## Endpointy HTTP

### Bootstrap, panel i zasoby

| Metoda | Ścieżka | Parametry | Dostęp i opis |
|---|---|---|---|
| `GET` | `/` | brak | Publiczny bootstrap. W normalnym trybie przekierowuje do `/voxone.html`; w pierwszym uruchomieniu lub Wi-Fi recovery AP zwraca wbudowany formularz Wi-Fi; przy niedostępnym SPIFFS i `protected` zwraca wbudowany formularz logowania awaryjnego. Nie należy go mylić z osobnym SECURITY Recovery AP opisanym niżej. |
| `POST` | `/` | Formularz: `ssid`, `pass`; na pierwszym uruchomieniu także `display`, `input`, `audio`. W recovery AP alternatywnie `retry=saved`. | Tylko tryb provisioningu. Zapisuje Wi-Fi, a przy pierwszym uruchomieniu również wybór sprzętu, po czym planuje restart. SSID ma maks. 29 B UTF-8, hasło 39 B; znaki NUL/TAB/CR/LF są odrzucane. |
| `GET` | `/voxone.html` | brak | Publiczny asset bootstrap/logowania; właściwe API pozostaje chronione. Zasób z SPIFFS, bez cache. |
| `GET` | `/voxone.css`, `/voxone.js`, `/advanced-audio.js`, `/dsp-client.js`, `/voxone-symbol.svg` | brak | Publiczne zasoby bootstrap panelu, serwowane ze SPIFFS. |
| `GET` | `/voxone-logo.svg` | brak | Publiczny asset marki, potrzebny również w AP pierwszego uruchomienia. |
| `GET` | `/favicon.ico` | brak | Publiczne przekierowanie do `/voxone-symbol.svg`. |
| `GET` | `/variables.js` | query ignorowane | Trasa chroniona. Zwraca JavaScript z `voxOneVersion`, `voxOneChannel`, `voxOneBuild`, `yoRadioVersion`, `voxOneProfile` i `playMode`. |
| `GET` | `/data/stations.tsv` | brak | Trasa chroniona. Surowa playlista ze SPIFFS (`application/octet-stream`); to także URL publikowany przez MQTT. |
| `GET` Upgrade | `/ws` | handshake WebSocket v13 | Chroniony handshake opisany szczegółowo w sekcji „WebSocket”. Nie jest zwykłym endpointem REST. |
| `GET` | `/emergency` | brak | Strona awaryjnego uploadu osadzona w firmware; dostęp zależy od polityki update/recovery danego stanu SECURITY. |
| `GET` | `/update.html` | brak | Gdy WWW działa, przekierowuje do `/#update`; bez WWW zwraca wbudowaną stronę awaryjną. |
| `GET` | `/logout` | brak | Starsza trasa Basic Auth; przy zdefiniowanym `HTTP_USER/PASS` wymusza odpowiedź 401. Nie wylogowuje cookie SECURITY — do tego służy `/api/security/logout`. |

Bezpośrednie żądania skompresowanych nazw magazynowych
`/advanced-audio.js.gz`, `/dsp-client.js.gz`, `/voxone-logo.svg.gz`,
`/voxone-symbol.svg.gz`, `/voxone.css.gz`, `/voxone.html.gz` i
`/voxone.js.gz` są znanymi trasami, ale są celowo odrzucane. Serwer sam
wybiera plik `.gz` dla kanonicznego URL. Inne pliki `/www` nie są
automatycznie publicznym API: centralna polityka jest fail-closed i nieznane
ścieżki zwracają 404.

### Oddzielny SECURITY Recovery AP

Po fizycznej decyzji KEY podczas startu normalny stos HTTP/WS nie rusza.
Urządzenie tworzy otwarty AP `VoxOne-Recovery-<6_HEX>` i uruchamia osobny,
minimalny `WiFiServer` na porcie 80 (adres jest pobierany z `softAPIP()`,
standardowo `http://192.168.4.1/`). Ten serwer ma dokładnie trzy trasy:

| Metoda | Ścieżka | Parametry | Dostęp i działanie |
|---|---|---|---|
| `GET` | `/` | brak | Zwraca osadzony formularz SECURITY Recovery z losowym tokenem CSRF dla bieżącej sesji. |
| `POST` | `/password` | `application/x-www-form-urlencoded`: `csrf` (48 znaków hex), `new_password` | Ustawia nowe hasło SECURITY; obowiązują limity hasła administratora: 4 znaki Unicode, maks. 64 B UTF-8. Sukces kończy sesję recovery i prowadzi do restartu. |
| `POST` | `/factory-reset` | `application/x-www-form-urlencoded`: `csrf` | Uruchamia pełny factory reset i restart do FIRST BOOT. |

Nie ma tu cookie ani logowania WWW: uprawnienie wynika z fizycznego wejścia
w recovery, a POST-y chronią dokładny `Host`, zgodny `Origin` i CSRF. Serwer
akceptuje najwyżej jedną stację AP i jednego klienta naraz, limituje nagłówki
do 1024 B, body do 256 B, czas klienta do 2 s oraz całą sesję do 64 żądań.
Nie udostępnia normalnego REST, statycznego WWW ani WebSocket.

### SECURITY

| Metoda | Ścieżka | Parametry | Odpowiedź / działanie |
|---|---|---|---|
| `GET` | `/api/security/state` | brak | Publiczny odczyt JSON: `state`, `setupAllowed`, `loginAllowed`, `authenticated`, `protectionEnforced`, `enforcementMode`. |
| `POST` | `/api/security/setup` | `mode=open` bez hasła albo `mode=protected&password=...` | Dostępne tylko w `legacy-unclaimed`. Ustanawia pierwszy świadomy tryb SECURITY. Hasło: min. 4 znaki Unicode, maks. 64 B UTF-8. |
| `POST` | `/api/security/login` | `password` | Tylko `protected`. Sukces zwraca `{"ok":true}` i ustawia cookie; błędne hasło 401, rate limit 429 z `Retry-After: 60`. |
| `POST` | `/api/security/logout` | pusty body | Unieważnia bieżącą sesję, czyści cookie, zwraca 204 i zamyka powiązane chronione WS kodem 1008. |
| `POST` | `/api/security/manage` | `mode=protected&password=<nowe>` lub `mode=open` bez hasła | W `open` włącza ochronę; w `protected` ważna sesja może zmienić hasło albo przejść do `open`. Sukces unieważnia wszystkie sesje. |

### Stan, czas, sprzęt i konfiguracja

| Metoda | Ścieżka | Parametry | Odpowiedź / działanie |
|---|---|---|---|
| `GET` | `/api/ready` | query ignorowane | Publiczna sonda restartu. JSON: `ready`, losowy dla bootu `bootId`, obiekt `ota` (sloty, stan obrazu, rollback, health, błędy i gotowość aktualizacji) oraz `www` (integralność, zgodność kontraktu, asset-set i statystyka weryfikacji). |
| `GET` | `/api/hardware` | brak; w specjalnym buildzie diagnostycznym opcjonalnie `ttsDiagnostics=1` | Chroniony opis rodziny/MCU, aktywnego `display`/`input`/`audio`, wspieranych opcji i statusu roadmapy sprzętowej. Parametr diagnostyczny nie istnieje w zwykłym buildzie. |
| `POST` | `/api/hardware` | `display`, `input`, `audio` | Chroniony formularz. Waliduje capability profilu, zapisuje wybór runtime i zwraca `changed`, `restartRequired:true` oraz nową konfigurację. |
| `GET` | `/api/config/backup` | brak | Chroniony binarny backup VXCF (`application/octet-stream`, nazwa `VoxOne-<version>-backup.vxcf`), obejmujący Config, Wi-Fi, hardware i playlistę. |
| `POST` | `/api/config/restore` | surowy body VXCF, `Content-Type: application/octet-stream`; maks. ok. 193 KiB | Chroniony, transakcyjny restore. Zwraca `{"ok":true,"restartRequired":true}` albo JSON z błędem. Nie restartuje automatycznie. |
| `GET` | `/api/time` | brak | Chroniony JSON: `rtcSupported`, `rtcFound`, `systemTime`, `rtcTime`, `timeSyncInterval`, `timeSyncIntervalRTC`, `syncCount`. |
| `POST` | `/api/time/sync` | pusty body | Chronione wymuszenie synchronizacji czasu; tylko profil z RTC i połączeniem sieciowym. Zwraca 202 oraz licznik sprzed próby. |
| `GET` | `/api/wifi/scan` | opcjonalnie `refresh=1` | Tylko provisioning. Asynchroniczny scan: 202 `scanning`, 200 `ready` z maks. 32 unikalnymi rekordami `{ssid,rssi,secure}`, albo 504 timeout. |

### MQTT

| Metoda | Ścieżka | Parametry | Odpowiedź / działanie |
|---|---|---|---|
| `GET` | `/api/mqtt` | brak | Chroniony odczyt: `enabled`, `host`, `port`, `username`, `passwordSet`, `rootTopic`, `effectiveRoot`, `autoRoot`. Hasło nie jest ujawniane. |
| `POST` | `/api/mqtt` | wymagane: `enabled`, `host`, `port`, `username`, `rootTopic`; opcjonalne `password`, `clearPassword=true|false` | Chroniony zapis. Port 1–65535; host do 127 B, username 63 B, root 63 B, hasło 127 B. Sukces zwraca `rebooting:true` i planuje restart. |

### Stacje i katalog internetowy

Wszystkie mutacje playlisty używają optymistycznej blokady `revision` —
ośmiu znaków hex zwracanych przez `GET /api/stations`. Konflikt daje 409.
Identyfikator stacji `id` jest 16-znakowym hex i pozostaje stabilny przy
reorder.

| Metoda | Ścieżka | Parametry | Odpowiedź / działanie |
|---|---|---|---|
| `GET` | `/api/stations` | brak | Chroniony snapshot: `revision`, `current`, `count`, tablica `stations`; rekord ma `number`, `id`, `name`, `url`, `ovol`, `metadataMode` (`normal`/`swap`) i `swapArtistTitle`. |
| `GET` | `/api/stations/export` | brak | Chroniony download TSV w natywnym formacie playlisty. |
| `POST` | `/api/stations/import` | `multipart/form-data`: `revision` i dokładnie jedna część plikowa; nazwa pola pliku nie jest sprawdzana, limit 192 KiB | Chroniony, transakcyjny import TSV. Kod zawiera ścieżkę pustej listy przez `empty`, ale bieżący handler jawnie odrzuca obecność tego pola; publicznie wspierany jest import pliku. |
| `POST` | `/api/stations/add` | `revision`, `name`, `url`, `ovol` | Dodaje rekord i zwraca nową rewizję/liczność. `ovol` jest liczbą; pełne limity nazwy/URL/OVOL egzekwuje `PlaylistStore::validRecord`. |
| `POST` | `/api/stations/edit` | `revision`, `id`, `name`, `url`, `ovol` | Edytuje rekord; zmiana URL aktywnej, grającej stacji powoduje reconnect. |
| `POST` | `/api/stations/delete` | `revision`, `id` | Usuwa rekord i koryguje aktywną pozycję. |
| `POST` | `/api/stations/reorder` | `revision`, `id`, `targetPosition` (1..liczba stacji) | Przenosi rekord. |
| `POST` | `/api/stations/metadata` | `revision`, `id`, `swapArtistTitle=0|1` | Ustawia interpretację `Artist - Title` dla rekordu. |
| `GET` | `/api/directory/search` | Start: `q`, opcjonalnie `country=PL`, `limit=20`; polling: `job=<uint32>` | Chronione proxy Radio Browser. `q` ma 1–80 B, `country` to 2 litery, limit 1–20. Start zwraca 202 `{"status":"pending","job":N}`; polling zwraca 202 pending albo 200 `{"results":[...]}`. Równolegle działa tylko jedno zadanie. Upstream HTTPS jest używany z `setInsecure()`, więc certyfikat nie jest walidowany. |

Wynik katalogu zawiera dla każdej stacji: `directoryId`, `name`, `url`,
`country`, `countryCode`, `codec`, `bitrate`, `hls`, `favicon`. Do wyniku
wchodzą tylko rekordy oznaczone przez upstream jako działające i z poprawnym
HTTP(S) URL.

### Aktualizacje

| Metoda | Ścieżka | Parametry | Dostęp i działanie |
|---|---|---|---|
| `POST` | `/update` | `multipart/form-data`: plik `.bin` (pole `update`), `updatetarget=firmware|fw|spiffs`, `filesize=<bajty>` | Polityka `ProtectedUpdate`. W `protected` wymaga sesji; w `open` i `legacy-unclaimed` jest dozwolony; w `unconfigured` tylko w trybie recovery. Odrzuca `*-full.bin`, zły typ/nazwę/rozmiar i niezdrowy slot OTA. Sukces zwraca tekst `OK`, następnie urządzenie restartuje się kontrolowanie. |
| `POST` | `/update/bt` | `multipart/form-data`: `filesize`, jeden plik `.bin` w polu `update`, maks. 1 310 720 B | Kompilowany tylko gdy profil ma BT update (obecnie warunkowo ESP32-S3 + capability). Wymaga VoxOneBT online, PROTO 2, capability `FW_UPDATE` i aktywnego RADIO. Po weryfikacji zwraca 202 `STAGED`; dalszy transfer idzie UART-em. |
| `GET` | `/api/bt/update` | brak | Ten sam warunek kompilacji. Chroniony status: `receiving`, `pending`, `active`, numery `state`, `phase`, `error`, `startError`, rozmiary/postęp i `confirmedVersion`. |

### DSP (tylko L0)

| Metoda | Ścieżka | Parametry | Odpowiedź / działanie |
|---|---|---|---|
| `GET` | `/api/dsp/state` | brak | Tylko build `VOXONE_PROFILE_L0`. Chroniony pełny snapshot demonstracyjnego modelu DSP: capabilities, shared audio, global, PEQ/dynamics, presety, runtime/revision i wartości wyliczone. Bieżący kod raportuje demo, nie potwierdza obecności fizycznego DSPmini. |

## WebSocket

### Handshake i autoryzacja

- URL: `ws://<host>/ws`, port 80, RFC 6455 / WebSocket version 13.
- TLS (`wss://`) nie jest zaimplementowany.
- W standardowych buildach wymagany jest poprawny `Host` i dokładnie jeden
  zgodny `Origin: http://<Host>`.
- W `protected` handshake musi zawierać ważne cookie `voxone_session`.
- Subprotocol jest opcjonalny dla zgodności ze starszym WWW. Jeżeli klient go
  wysyła, jedyną dozwoloną wartością jest `voxone.v1`; serwer ją odsyła.
- `open` nie wymaga cookie, ale nadal obowiązują Host, Origin, poprawny
  handshake i whitelist komend.
- `unconfigured`, `legacy-unclaimed` i `locked` nie dopuszczają normalnego WS.
- Każda kompletna wiadomość aplikacyjna musi być pojedynczą ramką tekstową.
  Binarne, fragmentowane i niekompletne ramki kończą połączenie kodem 1003.
- Ogólny limit parsera to 64 B na nazwę komendy i 64 B na wartość (bufory po
  65 B razem z NUL). Przekroczenie kończy WS kodem 1009.

Przykład JavaScript:

```js
const ws = new WebSocket(`ws://${location.host}/ws`, "voxone.v1");
ws.onopen = () => {
  ws.send("getwebstatus=1");
  ws.send("getsystem=1");
};
ws.onmessage = event => console.log(JSON.parse(event.data));
```

W przeglądarce cookie jest dołączane automatycznie. Klient nieprzeglądarkowy
musi jawnie wysłać `Cookie` i `Origin`.

### Format klient → serwer

Zwykłe komendy są tekstem, nie JSON-em:

```text
command=value
```

Znak `=` jest obowiązkowy, nawet jeśli wartość jest ignorowana, np.
`ping=1`, `stop=1`. Nazwy są case-sensitive. Większość mutacji nie ma osobnej
odpowiedzi sukces/błąd; klient obserwuje broadcast stanu. Nieznana komenda z
whitelisty nie istnieje, a komenda spoza whitelisty zamyka standardowy WS
kodem 1008. Nieprawidłowa wartość często jest tylko ignorowana.

### Pełna lista zwykłych komend klient → serwer

#### Odczyt i synchronizacja

| Komenda | Wartość | Efekt |
|---|---|---|
| `ping` | dowolna | Odpowiedź `{"pong":1}`. |
| `getindex` | dowolna | Kolejka snapshotu: metadata, volume, EQ, balance, bitrate/codec, playback, nazwa i numer stacji. |
| `getrssi` | dowolna | Zwraca RSSI oraz pole `heap` opisane niżej. |
| `getsystem` | dowolna | Zwraca ustawienia systemowe, build, sieć, pamięć, uptime i capabilities. |
| `getwebstatus` | dowolna | Zwraca znormalizowany stan źródła/transportu/metadanych. |
| `getscreen` | dowolna | Zwraca ustawienia ekranu, timeouty UI i scroll. |

#### Transport, źródło i głośność

| Komenda | Wartość | Efekt |
|---|---|---|
| `source` | `radio` albo `bt` | Ręczny wybór źródła; `bt` istnieje tylko na profilu z VoxOneBT i jest odrzucane, gdy moduł nie jest dostępny. |
| `webtransport` | `prev`, `toggle`, `next` | Source-aware transport: stacja RADIO albo AVRCP BT. |
| `start` | ignorowana | PLAY ostatniej stacji RADIO. |
| `stop` | ignorowana | STOP RADIO. |
| `toggle` | ignorowana | Przełącza PLAY/STOP RADIO. |
| `prev`, `next` | ignorowana | Poprzednia/następna stacja RADIO. |
| `playstation`, `play` | liczba stacji | PLAY; liczba jest ograniczana do zakresu 1..liczność playlisty. |
| `volm`, `volp` | ignorowana | Krok głośności w dół/w górę. |
| `vol`, `volume` | liczba | Starsza skala raw 0..254; wartość jest clampowana. |
| `vol100` | 0..100 | Głośność użytkownika; przy aktywnym BT steruje głośnością sesji BT, w przeciwnym razie RADIO. |
| `mute` | `0` albo `1` | Wyłącza/włącza MUTE. |
| `maximumvolume` | 1..100 | Maksymalna głośność użytkownika. |
| `startupmode` | `0` (`LAST`) albo `1` (`FIXED`) | Polityka głośności przy starcie. |
| `startupfixedvolume` | 0..100 | Stała głośność startowa. |
| `balance`, `ballance` | -16..16 | Balans; błędna historyczna pisownia pozostaje aliasem. |

#### Audio, ekran i UI

| Komenda | Wartość | Efekt |
|---|---|---|
| `tone` | `bass,middle,treble`, każda liczba w `int8_t` | Atomowo zapisuje trzy regulatory tonu. |
| `bass`, `middle`, `trebble` | liczba | Zapisuje jedną składową; `trebble` jest historyczną pisownią kontraktu. |
| `dspon`, `screenon` | `0`/niezero | Wyłącza/włącza prezentację DSP/display. |
| `dim` | 0..100 (clamp) | Ustawia jasność runtime i zapisuje ją. |
| `brightness` | liczba rzutowana do `uint8_t` | Starszy setter jasności bez walidacji 0..100. Preferowane `dim`. |
| `flipscreen` | `0`/niezero | Flip ekranu, zapis i broadcast `getscreen`. |
| `contrast` | liczba rzutowana do `uint8_t` | Zapis kontrastu i zastosowanie na display. |
| `numplaylist` | `0`/niezero | Sposób numerowania/listy playlisty. |
| `vumeter` | `0`/niezero | Włącza VU i publikuje nowy `getsystem`; nie publikuje samych poziomów L/R. |
| `stationlisttimeout` | 0..120 s | Trwały timeout widoku listy stacji. |
| `bttransporttimeout` | 0..120 s | Trwały timeout widoku transportu BT. |
| `scrollsettings` | `true|false,slow|normal|fast,0|1000|2000|3000|4000|5000` | Trwały zapis scroll. Jedyna zwykła mutacja z własnym zdarzeniem wyniku. |
| `screensaverenabled` | `0`/niezero | Włącza screensaver. |
| `screensavertimeout` | liczba rzutowana do `uint16_t` | Timeout screensavera. |
| `screensaverblank` | `0`/niezero | Blank zamiast widoku screensavera. |
| `screensaverplayingenabled` | `0`/niezero | Osobna polityka podczas PLAY. |
| `screensaverplayingtimeout` | liczba rzutowana do `uint16_t` | Timeout podczas PLAY. |
| `screensaverplayingblank` | `0`/niezero | Blank podczas PLAY. |
| `abuff` | liczba rzutowana do `uint16_t` | Trwałe ustawienie rozmiaru bufora audio; skutek pełny po restarcie. |

#### Czas, sieć, wejście i system

| Komenda | Wartość | Efekt |
|---|---|---|
| `tzh`, `tzm` | liczba rzutowana do `int8_t` | Pola legacy strefy; bieżący runtime używa Europe/Warsaw. |
| `sntp1`, `sntp2` | nazwa serwera, do 34 B + NUL | Ustawia serwery SNTP. |
| `timeint` | 1..10080 min | Interwał synchronizacji sieciowej. |
| `timeintrtc` | 1..1000 h | Interwał synchronizacji RTC. |
| `volsteps` | liczba rzutowana do `uint8_t` | Liczba/kroków głośności konfiguracji legacy. |
| `encacc` | liczba rzutowana do `uint16_t` | Akceleracja enkodera. |
| `oneclickswitching` | `0`/niezero | Przełączanie playlisty jednym kliknięciem. |
| `softap` | liczba rzutowana do `uint8_t` | Opóźnienie/polityka SoftAP. |
| `mdnsname` | tekst do 23 B + NUL | Trwała nazwa hosta; zmiana wymaga ponownego uruchomienia usług sieciowych. |
| `smartstart` | `1` albo inna wartość | `1` zapisuje intencję PLAY tylko gdy player aktualnie działa; pozostałe wartości mapują się na tryb 2. |
| `reboot`, `boot` | ignorowana | Planowany restart urządzenia. |

Kod `CommandHandler` nadal zawiera destrukcyjne komendy legacy `format`,
`clearspiffs` i `reset`, ale centralna whitelist SECURITY ich nie dopuszcza;
nie są częścią publicznego protokołu WS standardowych buildów.

### Komendy DSP klient → serwer (tylko L0)

Ramki DSP są również tekstem, ale są rozpoznawane przed zwykłym parserem:

```text
dsp.set=<requestId>,<parameterId>,<index>,<value>
dsp.activate=<requestId>,<presetId>
dsp.restore=<requestId>
dsp.save=<requestId>,<presetId>[,<nazwa UTF-8>]
dsp.rename=<requestId>,<presetId>,<nazwa UTF-8>
```

Maksymalna długość to 80 B. `requestId` służy do korelacji. Identyfikatory
`parameterId`:

| ID | Parametr | ID | Parametr |
|---:|---|---:|---|
| 1 | MasterTrim | 17 | MasterMute |
| 2 | ProcessingEnabled | 18 | PeqEnabled |
| 3 | OutputMode | 19 | PeqFrequency |
| 4 | Crossover | 20 | PeqGain |
| 5 | HpfMains | 21 | PeqQ |
| 6 | SubRouting | 22 | DynamicsEnabled |
| 7 | OutputTrim | 23 | DynamicsThreshold |
| 8 | OutputDelay | 24 | DynamicsRatio |
| 9 | OutputPolarity | 25 | DynamicsAttack |
| 10 | OutputUserMute | 26 | DynamicsRelease |
| 11 | LoudnessEnabled | 27 | DynamicsMakeup |
| 12 | LoudnessIntensity | 28 | SubTrim |
| 13 | LimiterEnabled | 29 | SubDelay |
| 14 | LimiterThreshold | 30 | SubPolarity |
| 15 | LimiterAttack | 31 | SubMute |
| 16 | LimiterRelease |  |  |

`index` wybiera wyjście lub pasmo PEQ; dla parametrów globalnych musi wynosić
0. Boolean przyjmuje dokładnie 0/1, polaryzacja 0 lub 180, a dalsze zakresy
waliduje `DspService`. Mimo nazwy `dsp.save` bieżący runtime zapisuje preset
użytkownika tylko do RAM na czas danego uruchomienia. Kod zawiera format
serializacji `dsp_storage_format.*`, ale nie ma podłączonego adaptera trwałego
storage.

### Format serwer → klient

Serwer wysyła wyłącznie tekstowe ramki JSON. Po samym connect nie ma
gwarantowanego automatycznego pełnego snapshotu — klient powinien wysłać
`getwebstatus`, `getindex`, `getsystem`, `getscreen` i ewentualnie `getrssi`.

Pełna lista kształtów wiadomości:

| Zdarzenie / odpowiedź | Przykład i pola |
|---|---|
| Pong | `{"pong":1}` |
| Nazwa stacji/peer | `{"payload":[{"id":"nameset","value":"Italo4you"}]}` |
| Numer stacji | `{"current":3}` |
| Metadata | `{"payload":[{"id":"meta","value":"Artist - Title"}]}` |
| Głośność | `payload` z `volume` (raw 0..254), `volume100`, `muted`, `maximumVolume`, `startupMode`, `startupFixedVolume`. |
| RSSI/bufor | `{"payload":[{"id":"rssi","value":-58},{"id":"heap","value":74}]}`. Uwaga: `heap` nie jest stertą; to procent zapełnienia bufora audio, albo 0. |
| Format audio | `payload` z `bitrate` i `fmt` (`MP3`, `AAC`, `FLAC`, `OGG`, `WAV` albo `bitrate`). |
| Playback RADIO | `{"payload":[{"id":"playerwrap","value":"playing|stopped"}]}`. Stan `paused` występuje w `webStatus` dla BT, nie tutaj. |
| EQ | `payload` z `bass`, `middle`, `trebble`. |
| Balance | `{"payload":[{"id":"balance","value":0}]}`. |
| `webStatus` | Obiekt opisany poniżej. |
| `getsystem` | Obiekt ustawień i `systemInfo`/`networkInfo`, opisany poniżej. |
| `getscreen` | Obiekt display/timeout/scroll, opisany poniżej. |
| Wynik scroll | `{"event":"scrollSettings.result","ok":true|false,"error":"...","changed":...,"persisted":...,"runtimePublished":...,"activeApplied":...,"scrollCoreActive":...}`. |
| DSP zmienione | `{"event":"dsp.changed","requestId":N,"operation":...,"parameterId":...,"index":...,"value":...,"activePresetId":...,"revision":...,"appliedRevision":...,"dirty":...}`; broadcast do wszystkich autoryzowanych klientów. |
| DSP błąd | `{"event":"dsp.error","requestId":N,"code":...,"revision":...}`. Kody transportu 100–106 oznaczają kolejno malformed, too large, invalid parameter, invalid name, busy, tone persist failure, unavailable; inne kody pochodzą z `DspServiceError`. |

`webStatus` ma postać:

```json
{
  "webStatus": {
    "source": "WEB",
    "activeSource": "radio",
    "name": "Italo4you",
    "metadata": "Artist - Title",
    "artist": "Artist",
    "title": "Title",
    "codec": "MP3",
    "playback": "PLAY",
    "transport": "playing",
    "btModule": {
      "firmware": "0.6.7-dev",
      "name": "VoxOneBT-...",
      "capabilities": "...",
      "online": true,
      "protocol": 2
    },
    "bitrate": 256,
    "sampleRate": 0,
    "btConnected": false,
    "availableSources": ["radio", "bt"],
    "volume100": 20,
    "muted": false
  }
}
```

Dla BT `source="BT"`, `activeSource="bt"`, `name` jest peer-em,
`artist`/`title` pochodzą z AVRCP, `codec` i `bitrate` są puste/0,
`sampleRate` pochodzi z VoxOneBT, a `transport` ma
`playing|paused|stopped|unavailable`.

`getsystem` zwraca pola legacy `sst`, `vu`, `canVu`, `softr`, `vut`, `mdns`,
`ipaddr`, `abuff`; obiekt `systemInfo` z `version`, `channel`, `build`,
`profile`, `mac`, `rssi`, `uptimeSeconds`, `freeHeap`, `minimumFreeHeap`,
`psramTotal`, `psramFree`, `btFirmwareUpdateSupported`, `capabilities`; oraz
`networkInfo` z `hostname`, `activeSsid` i listą profili
`{ssid,passwordSet,order}`. Hasła Wi-Fi nie są wysyłane.

`getscreen` zwraca `flip`, `canFlip`, `canBrightness`, `br`, `nump`, `dspon`,
`con`, ustawienia screensavera (`scre`, `scrt`, `scrb`, `scrpe`, `scrpt`,
`scrpb`), `stationListTimeout`, `btTransportTimeout`, `canBtTransport` oraz
`canScrollSettings`, `scrollEnabled`, `scrollSpeed`, `scrollPauseMs`,
`scrollSettingsSource`, `scrollCoreActive`.

Zmiana wspólnego tonu poza komendą DSP ma skrócony, nieskorelowany wariant
zdarzenia DSP (bez `operation`, `index`, `value` i `activePresetId`):

```json
{"event":"dsp.changed","requestId":0,"parameterId":0,"revision":12,"appliedRevision":11,"dirty":true}
```

### Broadcasty i zamknięcia

Zmiana stacji, metadata, playback, volume, EQ, balance, źródła/BT albo części
ustawień display powoduje broadcast odpowiedniego kształtu. Snapshot i
broadcast używają tych samych obiektów; nie ma osobnej koperty typu/event dla
starych wiadomości `payload`.

Istotne kody zamknięcia:

- 1000 — zwykłe zamknięcie klienta;
- 1001 — urządzenie restartuje się;
- 1003 — tylko kompletne ramki tekstowe są obsługiwane;
- 1008 — brak/wygaśnięcie autoryzacji, zła lub niedozwolona komenda;
- 1009 — komenda lub wartość przekracza bufor;
- 1013 — przeciążenie/backpressure; klient powinien po chwili połączyć się
  ponownie i odtworzyć snapshot.

## Tryby SECURITY i ich wpływ na API

### Trwałe stany urządzenia

| Stan API | Znaczenie | HTTP/WS |
|---|---|---|
| `unconfigured` | Brak skonfigurowanego Wi-Fi i brak rekordu SECURITY. | Dostępne są bootstrap `/`, provisioning `POST /`, `/api/wifi/scan`, marka, `/api/ready` i `/api/security/state`. Normalne REST/WS są zamknięte. Update/recovery jest dopuszczany tylko w przewidzianym trybie recovery. Pierwsza poprawna transakcja provisioning kończy się stanem `open`. |
| `legacy-unclaimed` | Urządzenie ma Wi-Fi ze starszej wersji, ale nie ma rekordu SECURITY. | Publiczny panel bootstrap pozwala wybrać `open` albo `protected` przez `/api/security/setup`. Normalne REST i WS są zamknięte. `/update` pozostaje dostępny dla kompatybilności migracji. |
| `open` | Świadomie wybrany brak hasła. | Chronione klasy tras HTTP i WS są dostępne bez cookie, ale nadal obowiązuje Host/Origin, ścisła lista tras i whitelist WS. Ustawienie hasła odbywa się przez `/api/security/manage`. |
| `protected` | Ustawione hasło administratora. | Chronione WWW, REST, update i `/ws` wymagają ważnej sesji. Publiczne pozostają minimalny bootstrap, `/api/ready`, `/api/security/state` i login. Wygaśnięcie/logout/zmiana generation zamyka WS kodem 1008. |
| `locked` | Rekord SECURITY jest uszkodzony albo storage nie dał się bezpiecznie odczytać. | Fail-closed: normalne API, login i update są blokowane (423/403 zależnie od warstwy). Odzyskanie wymaga fizycznej ścieżki SECURITY Recovery/KEY, nie zdalnego obejścia. |

Rekord SECURITY jest trwały w osobnym namespace NVS: wersja, `generation`,
CRC, 16 B soli i 32 B verifiera PBKDF2-HMAC-SHA256 (10 000 iteracji). Hasło
nie jest zapisywane jawnie. `generation` wiąże sesję i unieważnia ją po
zmianie stanu/hasła.

### Build-time enforcement

Niezależnie od trwałego stanu istnieje tryb kompilacji:

| `enforcementMode` | Użycie | `protectionEnforced` |
|---|---|---:|
| `production` | Wszystkie standardowe profile W0/M0/S0/DIN0/L0. Wyłączenie production w takim profilu jest błędem kompilacji. | `true` |
| `test` | Wyłącznie `m0-security-test`; uruchamia ten sam gate, ale nie może być przedstawiane jako ochrona produkcyjna. | `true` |
| `disabled` | Niestandardowy/non-standard build bez obu flag. Centralny gate przepuszcza żądania; nie wymusza Host/Origin/cookie ani whitelisty WS. Oznacza to również osiągalność legacy komend `format`, `clearspiffs` i `reset`, jeżeli pozostają w `CommandHandler`. Stan SECURITY może nadal być raportowany, ale nie chroni transportu. | `false` |

MQTT ma osobną autoryzację brokera i nie używa cookie SECURITY.

## Stan urządzenia: co jest eksponowane i gdzie żyje

### Stan już eksponowany

| Grupa / pola | Interfejs | Źródło i miejsce przechowywania |
|---|---|---|
| Aktywna stacja: numer, stabilne ID, nazwa, URL, OVOL, tryb metadata | REST `/api/stations`; WS `current`, `nameset`, `webStatus` | Trwała playlista `/data/stations.tsv` + `/data/stations.idx` zarządzana przez `PlaylistStore`; aktywny rekord RAM `Config::station`; numer trwały `Config::store.lastStation` i `config_runtime::Snapshot::lastStation`. |
| Metadata, artysta, tytuł RADIO | WS `meta`, `webStatus` | `Config::station.title`; rozdział runtime przez `station_metadata.*`. |
| Źródło RADIO/BT, playback/transport, dostępne źródła | WS `webStatus` | `SourceManagerState::active_` i intent RADIO; fizyczny RADIO w `Player::_status`; BT w `BtLink::publishedState_`. |
| Peer BT, artysta, tytuł, sample rate, online/protocol/firmware/name/capabilities | WS `webStatus` | `BtLinkState`, publikowany przez `BtLink`; widok aktywnego źródła w `SourceWebSnapshot`. |
| Głośność raw i 0–100, MUTE, max, startup mode/fixed, balance | WS `volume`, `webStatus`, `balance`; DSP REST shared audio | Trwałe pola `Config::store`, lock-free `config_runtime::Snapshot`; rzeczywisty output/mute w `Player::_mute` (`MuteState`). |
| Bass/middle/treble | WS EQ; DSP REST | `Config::store` + `config_runtime::Snapshot`. |
| Codec, bitrate | WS `fmt`, `bitrate`, `webStatus` | `Config::configFmt` i `Config::station.bitrate`, aktualizowane przez callbacki audio. |
| RSSI | WS `getrssi`, `getsystem`, scan Wi-Fi | Bezpośrednio `WiFi.RSSI()`; dodatkowy cache `NetServer::rssi`. |
| IP, hostname, aktywny SSID, profile Wi-Fi bez haseł | WS `getsystem` | ESP Wi-Fi driver, `Config::ssids[]`, `Config::store.mdnsname` / runtime snapshot. |
| Uptime | WS `systemInfo.uptimeSeconds` | Wyliczany na żądanie z `esp_timer_get_time()`, nie jest trwale przechowywany. |
| MAC, wersja, kanał, build SHA, profil, capability | WS `getsystem`, `/variables.js` | eFuse (`esp_read_mac`), `version.h`, `hardware_descriptor.*`. |
| Free/min heap, PSRAM total/free | WS `getsystem` | Bieżące odczyty `heap_caps_*` i `ESP.get*Psram()`. |
| Zapełnienie bufora audio | WS `getrssi` jako historyczne ID `heap` | `Player::inBufferFilled()` względem `NetServer::playerBufMax`. Nazwa pola jest myląca. |
| Display, brightness, contrast, flip, screensaver, VU enabled, timeouty i scroll | WS `getscreen`/`getsystem`, REST `/api/hardware` | Config v7 + runtime snapshot; hardware w osobnym runtime hardware NVS; timeouty UI i scroll w ich własnych adapterach NVS. |
| Stan SECURITY i sesji bieżącego requestu | `/api/security/state` | Rekord NVS w `SecurityService`; sesje RAM w `SecuritySessionService`. Token ani verifier nie są ujawniane. |
| MQTT config bez hasła | `/api/mqtt` | `MqttSettings` w NVS; API zwraca tylko `passwordSet`. |
| Stan publikowany przez MQTT | Retained `<root>/status`, `<root>/volume`, `<root>/playlist` | `status` zawiera `status`, `station`, `name`, `title`, `on`; `volume` to historyczna skala 0..254; `playlist` to URL `/data/stations.tsv`. Dane pochodzą z `Config::station`, snapshotu Config i `Player`. Root jest ustawiony albo wyliczony z MAC. |
| Czas systemowy/RTC i licznik sync | `/api/time` | libc/system clock, `rtc`, atomowy `TimeKeeper::successfulSyncCount`; interwały w Config v7. |
| Slot OTA, health, rollback, poprzedni błąd | `/api/ready` | atomowe pola `ota_boot_runtime.cpp`; `lastFailure` w namespace NVS `vox_ota`. |
| WWW integrity/contract/asset-set | `/api/ready` | `Config::wwwAssetIntegrity`, wyliczone przy starcie z manifestu i plików SPIFFS. |
| `bootId` | `/api/ready` | Statyczny RAM `webBootId`, losowany raz na start serwera. |
| Postęp VoxOneBT update | `/api/bt/update` | `BtFirmwareSender::Progress`, staging i snapshot chronione `btUpdateMux`. |
| DSP demo state/presety/revision | `/api/dsp/state`, zdarzenia DSP WS | Statyczny `DspService` i `DspRuntimeState` w RAM; factory presets są w kodzie, a dwa user presets w `DspService::users_`. Nie są obecnie trwale wczytywane/zapisywane; tylko L0. |

### Stan istniejący w kodzie, ale niewystawiony lub wystawiony częściowo

To są naturalni kandydaci dla przyszłej integracji HA; ich publikacja wymaga
jednak ustabilizowania kontraktu, synchronizacji i polityki prywatności.

| Kandydat | Gdzie jest w kodzie | Obecna luka |
|---|---|---|
| Poziomy VU L/R, czas ostatniej próbki | `BtLinkState::rawVuLeft`, `rawVuRight`, `rawVuLastMs`; RADIO przez `sourceManagerGetVuLevel`/Audio | WS publikuje tylko `vu` enabled i `vut` threshold, nie aktualne kanały. |
| Album BT i absolutna głośność/revision BT | `BtLinkState::album`, `volume`, `volumeRevision` | `webStatus` publikuje artist/title i wspólne `volume100`, ale nie album ani surowy stan peer volume. |
| Diagnostyka VoxOneBT: reset reason, uptime, heap/minHeap | `BtLinkDiagnostics` | Dostępna wewnętrznie/UART, nie w HTTP/WS. |
| Szczegółowy postęp MAIN/SPIFFS update | globalny `UpdateProgressSnapshot`: target, phase, activity, bytes, percent, error, revision | `/api/ready` pokazuje boot health, nie bieżący upload. Panel zna upload z XHR; brak stabilnego endpointu ogólnego progress. |
| Błąd playera, temporary TTS/URL, restore i watchdog transportu | `Player::_hasError`, `_temporary`, `_temporaryProgress`, `_radioReconnect` | Częściowo tylko logi i stan końcowy; brak jawnego eventu error/result dla komend WS. |
| Faktyczny rozmiar bufora audio w bajtach i stan dekodera | `Audio`/`Player::inBufferFilled()`, pola transportu w `src/audioI2S/Audio.cpp` | WS wystawia jedynie procent pod ID `heap`; brak pojemności, underflow i liczników błędów. |
| Powód ostatniego disconnectu Wi-Fi, próba/profil reconnectu, gateway/DNS | `MyNetwork`, `NetworkReadyState`, `ReassocPolicy`, event info ESP Wi-Fi | Eksponowane są IP/SSID/RSSI i lista profili, lecz nie diagnostyka reconnectu. |
| AP/recovery presentation: SSID, adres, faza provisioningu | `first_boot::RuntimeSnapshotStore` w `MyNetwork::presentationState` | Używane do renderowania routingu, nie ma normalnego JSON API. |
| Pełny update/restart lock i liczba aktywnych HTTP/WS | `SystemOperationState`, `webSocketSessions`, `AsyncWebServer::activeRequestCount()` | Używane do bezpiecznego restartu, nie publikowane. |
| Bieżące poziomy stosów, loop latency, błędy I2S/BT i liczniki reconnect | rozproszone diagnostyki/logi w Audio, BtAudioInput, NetServer i testowych buildach | Nie mają stabilnego modelu publicznego. |
| Stan local UI: aktywna strona, idle/saver, lokalny focus | `ui_state.*`, `Display`, runtime widgetów | Częściowo pośrednio w `getscreen`, bez bieżącej strony/focus. |
| Stan NVS/storage: mode v7, dirty/read-only, status migracji | `ConfigPersistence`, `Config::storageMode()`, `startupStatus()` | Widoczny w logach i backupie, nie w normalnym API. |

Nie należy wystawiać: haseł Wi-Fi/MQTT, verifiera/salt SECURITY, tokenów sesji,
kluczy Bluetooth ani surowych prywatnych śladów diagnostycznych.

## Discovery w sieci

### mDNS

Mechanizm mDNS istnieje, ale w ograniczonej formie:

1. Przed połączeniem STA kod ustawia `WiFi.setHostname(runtime.mdnsName)`.
2. Po uzyskaniu gotowego połączenia wywołuje
   `MDNS.begin(runtime.mdnsName)` (`src/core/network.cpp`). Dzięki temu
   urządzenie może odpowiadać jako `<hostname>.local`.
3. W trybie AP ustawiany jest `WiFi.softAPsetHostname`, ale kod nie uruchamia
   tam osobnego `MDNS.begin`.
4. Nie ma żadnego `MDNS.addService`, w szczególności nie ma
   `_voxone._tcp`, `_http._tcp` ani rekordu TXT opisującego API/capabilities.

Wniosek: jest rozwiązywanie nazwy hosta mDNS w normalnym STA, ale nie ma
Zeroconf service discovery wystarczającego do automatycznego wykrywania
integracji Home Assistant. HA musiałby znać hostname/IP albo projekt musi
dodać dedykowaną usługę, np. `_voxone._tcp`.

### SSDP/UPnP i inne broadcasty

W kodzie nie ma SSDP, UPnP, `239.255.255.250`, WS-Discovery ani własnego UDP
broadcast discovery. Captive DNS (`DNSServer`) działa tylko w AP pierwszego
uruchomienia i odpowiada wildcardem na potrzeby portalu; nie jest mechanizmem
discovery w LAN.

`ArduinoOTA` potrafiłoby użyć discovery mDNS, ale `USE_OTA` ma domyślnie
wartość `false` i repozytoryjne `myoptions.h` jej nie nadpisuje. Standardowy
build nie uruchamia więc `ArduinoOTA.begin()` ani dodatkowej usługi OTA.

## Framework i biblioteka WebSocket

| Warstwa | Wersja / implementacja |
|---|---|
| System budowania | PlatformIO; repo przypina platformę `platformio/espressif32@6.12.0`. Lokalnie użyty PlatformIO Core raportuje `6.2.0`, ale Core nie jest przypięty w repo. |
| Framework | `framework = arduino`. Pakiet platformy: `framework-arduinoespressif32 3.20017.241212`, czyli Arduino-ESP32 `2.0.17`. |
| ESP-IDF pod Arduino | `4.4.7` (nagłówki SDK dla ESP32 i ESP32-S3). Projekt nie jest natywną aplikacją ESP-IDF, choć używa wielu API IDF. |
| Web server / WebSocket | Wersja źródłowa, zmodyfikowana i vendored w `src/AsyncWebServer`: `ESPAsyncWebServer` / `AsyncWebSocket` z własnym `AsyncTCP`. Kod nie ma `library.json`, `library.properties` ani numeru semver, więc nie da się uczciwie podać numeru wydania biblioteki. |
| Transport WS | `AsyncWebSocket` na `AsyncWebServer(80)`, endpoint `/ws`, WebSocket RFC version 13. |

Vendored biblioteka nie jest niezmienioną zależnością z `lib_deps`: zawiera
lokalne poprawki synchronizacji, lifecycle, OOM/backpressure, autoryzacji i
bezpiecznego close. Dlatego jej tożsamością dla tego projektu jest kod w repo
i commit VoxOne, a nie nieudokumentowany numer upstream.

## Ważne ograniczenia kontraktu

- API nie ma osobnego wersjonowania (`/v1`); kompatybilność WS jest umownie
  oznaczona subprotocol `voxone.v1`, a WWW ma osobny contract/integrity.
- Większość mutacji WS nie zwraca ACK ani skorelowanego błędu. Po reconnect
  klient musi pobrać snapshot i traktować stan serwera jako autorytatywny.
- Endpointy i pola zależą od profilu/capability: `/api/dsp/state` tylko L0,
  BT update tylko w profilach z włączoną ścieżką firmware update, pola BT mogą
  być puste/offline.
- Publiczne `/api/ready` celowo ujawnia tylko dane potrzebne do health/restart
  probe; nie zastępuje autoryzowanego snapshotu systemowego.
- HTTP i WS są nieszyfrowane. SECURITY chroni dostęp i CSRF/DNS rebinding,
  ale hasło sesyjne oraz treść sterowania płyną po lokalnej sieci bez TLS.
- Nazwa mDNS nie jest unikalnym ID. Do HA właściwym kandydatem na unique ID
  jest pełny MAC MAIN z `systemInfo.mac`, nie IP ani hostname.
