# VOXONE-BT-AUDIO-PROBE-WWW-RETRY-1

Data: 2026-11-02
Wynik: **PARTIAL — oba OTA PASS, celowany HIL FAIL przed PCM, przyczyna `peak=0` UNVERIFIED, produkcyjny restore PASS**

## Zakres

Wykonano jedną nową próbę wdrożenia diagnostycznego VoxOneBT przez produkcyjny endpoint WWW, bez zmiany SECURITY, endpointu, MAIN i SPIFFS. Po terminalnym sukcesie wdrożenia rozpoczęto dokładnie jedną krótką sesję fizyczną. Sesję zatrzymano na jedynej próbie A2DP CONNECT; nie ponawiano połączenia ani HIL. Następnie, przy zachowanej komunikacji MAIN↔VoxOneBT, wykonano zatwierdzony powrót do produkcyjnego obrazu `d76b139` przez ten sam endpoint WWW.

Nie używano USB do programowania, esptool, OTA MAIN, SPIFFS, kasowania NVS/bondingu, resetu fabrycznego, zmian DSP/SECURITY ani automatycznych retry. Nie wykonano commit ani push.

## Przyczyna poprzedniego HTTP 403 i poprawione żądanie

`POST /update/bt` jest trasą `ProtectedUpdate`, mutującą i multipart (`src/core/security_access_policy.cpp`). Produkcyjny adapter wymaga dla metody mutującej jednego poprawnego nagłówka `Origin`; `sameOrigin()` w `src/core/security_api.cpp` akceptuje wyłącznie `http://` i porównuje znormalizowany authority `Origin` z `Host`. Panel wysyła `FormData` przez same-origin `XMLHttpRequest("POST", "/update/bt")` z polami `filesize` i `update` (`data/www/voxone.js.gz`).

Poprzedni 403 wynikał z braku `Origin`. Nowe, zaakceptowane żądanie zachowało produkcyjny enforcement i użyło:

- URL `http://192.168.1.56/update/bt`;
- `Host: 192.168.1.56`;
- `Origin: http://192.168.1.56`;
- `Referer: http://192.168.1.56/` oraz `Accept: */*`;
- multipart z `filesize=<rozmiar>` i `update=@firmware.bin`.

Preflight SECURITY: `state=open`, `protectionEnforced=true`, `enforcementMode=production`. Nie wyłączano ani nie obchodzono ochrony Host/Origin.

## Obrazy

| Obraz | Bajty | SHA-256 | Wynik kontroli |
|---|---:|---|---|
| diagnostyczny `esp32dev-audio-path-probe` | 1 149 472 | `72659798c65e8839b811b5fcda946e8028898c3fe833df7106ef378a6da9b645` | zgodny lokalnie i na bridge |
| produkcyjny VoxOneBT `d76b139` | 1 147 872 | `6cd1e9f52527a931799cddd10f6e2e7b287a3722e42c6049388ee342031ba744` | zgodny lokalnie i na bridge |
| syntetyczny WAV PCM16 stereo 48 kHz, 60 s, peak 1642 | 11 520 044 | `70cecf0641087896c75f126235f3ab1b32247c7fef3cd8a91bc6aa409a6fc6e1` | format i SHA PASS; nie został wysłany z powodu STOP na CONNECT |

## Preflight fizyczny

- oba globalne locki HIL były wolne; brak innego harnessu i procesu A2DP;
- Bluetooth, BlueALSA i `bluealsa-aplay` były aktywne;
- peer `30:76:F5:EF:F3:5A`, `VoxOneBT-EFF35A`: Paired/Bonded yes, Connected no;
- logiczne porty `VOXONEBT_USB` i `L0_LOG_RX` były wolne;
- L0: build `4ef1e53`, app1 VALID, READY, WWW READY/verified, `bootId=e7582463`;
- VoxOneBT: `0.6.7-dev`, online, PROTO 2;
- stan początkowy: `Italo4you` #3, RADIO/PLAY, volume100=12, MUTE OFF, balance 0.

Dwa ciągłe, pasywne loggery UART otwarto przed pierwszym OTA. Samo otwarcie nie zmieniło `bootId` i nie wywołało markerów boot/panic/assert/WDT.

## Aktualizacja diagnostyczna — PASS

Wysłano dokładnie jeden POST. Odpowiedź HTTP `202 STAGED` nie została uznana za koniec aktualizacji. Terminalny `/api/bt/update` potwierdził:

| Pole | Wartość |
|---|---|
| state / phase | `9 / 5` |
| active / error | `false / 0` |
| total / confirmed | `1149472 / 1149472` B |
| percent | `100` |
| confirmedVersion | `0.6.7-dev` |

Ciągły UART L0 zawiera `FW_VERIFY`, `FW_OK`, pełny snapshot tożsamości z `otaState=PENDING_VERIFY` i późniejsze `otaState=VALID`. UART VoxOneBT zawiera `OTA pending verification`, start `0.6.7-dev`, aktywne linie `[AUDIO_PATH]`, `OTA health check passed` i `OTA image confirmed VALID`. Zaprojektowany restart MAIN zmienił `bootId` na `604de4c8`; po powrocie L0 był READY/app1 VALID/WWW verified, a moduł online/PROTO 2.

## Celowany HIL sondy — FAIL przed PCM

Po restarcie MAIN jego stan startowy wynosił RADIO/STOP i volume100=20. Zgodnie z ograniczeniem poziomu jako pierwszą komendę wykonano obniżenie do volume100=12, a dopiero później przywrócono początkowe RADIO/PLAY. Każda korekta miała readback.

| Etap | Wynik | Dowód |
|---|---|---|
| przygotowanie stanu początkowego | PASS | RADIO/PLAY, stacja #3, volume100=12, MUTE OFF, balance 0 |
| A2DP CONNECT | **FAIL / STOP** | pojedynczy `bluetoothctl connect 30:76:F5:EF:F3:5A` osiągnął bounded timeout 20 s, exit 124 |
| CONNECT bez autoplay | NOT RUN | brak ustanowionego A2DP |
| PLAY / PCM | NOT RUN | WAV nie został uruchomiony |
| PAUSE | NOT RUN | STOP po CONNECT |
| PLAY po PAUSE | NOT RUN | STOP po CONNECT |
| dwa pełne okna RAW/post-volume/MAIN | NOT RUN | nie było callbacków PCM ani trasy BT |
| cleanup po STOP | PASS | peer disconnected; dokładny RADIO/PLAY/volume/MUTE/balance restore |

W czasie timeoutu MAIN nadal raportował VoxOneBT online/PROTO 2, a UART modułu nadal emitował sondę co około sekundę. Wszystkie dostępne w tym odcinku wartości sondy były stanem bez połączenia: `connected=0`, `playback=0`, `rawBytes=postBytes=i2sBytes=0`, `rawPeak=postPeak=0`, `absvol=0`, `factor=0/4096`. Nie są to pomiary audio ani reprodukcja anomalii `peak=0` po resume. Nie wykonano drugiego CONNECT i nie próbowano wymusić poziomu sygnału.

Nie ustalono, dlaczego BlueZ nie zestawił A2DP w tej pojedynczej próbie. Brak produkcyjnego retestu A2DP po restore oznacza, że wyniku nie wolno przypisać ani obrazowi diagnostycznemu, ani bridge, ani bondingowi.

## Diagnoza `peak=0`

**UNVERIFIED.** Sesja nie osiągnęła A2DP CONNECT ani PCM, więc nie powstały wymagane równoczesne okna:

- A2DP callback RAW peak/nonzero/bytes;
- post-volume peak/nonzero, ABSVOL i rzeczywisty `volumeFactor`;
- I2S writes/bytes/shortWrites modułu;
- MAIN I2S1 RX/peak i I2S0 TX.

Zera sondy dotyczą wyłącznie prawidłowego idle `connected=0`; nie rozstrzygają, czy historyczne `peak=0` powstaje przed gain, w gain/kwantyzacji, w transporcie I2S, w MAIN czy tylko w diagnostyce. Dotychczasowe ustalenie offline — pojedynczy potwierdzony niejednostkowy gain VoxOneBT i brak dowodu podwójnego tłumienia w uchwyconych cyklach — pozostaje bez zmian.

## Przywrócenie produkcyjne — PASS

Kanał MAIN↔VoxOneBT pozostał zdrowy, więc zatwierdzony produkcyjny restore wykonano jako cleanup, a nie retry HIL. Wysłano dokładnie jeden POST obrazu `d76b139`. Terminalny wynik:

| Pole | Wartość |
|---|---|
| state / phase | `9 / 5` |
| active / error | `false / 0` |
| total / confirmed | `1147872 / 1147872` B |
| percent | `100` |
| confirmedVersion | `0.6.7-dev` |

UART ponownie potwierdził `FW_VERIFY`, `FW_OK`, `PENDING_VERIFY -> VALID`; po końcowym boot modułu występują `OTA health check passed` i `OTA image confirmed VALID`, a linie `[AUDIO_PATH]` już nie występują. Zaprojektowany restart MAIN zmienił `bootId` na `e6d1bcd3`.

## Stan końcowy

| Element | Stan |
|---|---|
| MAIN | `4ef1e53`, app1 VALID, READY, WWW READY/verified, `bootId=e6d1bcd3` |
| VoxOneBT | przywrócony dokładnym obrazem `d76b139` o SHA podanym wyżej; runtime `0.6.7-dev`, online, PROTO 2, OTA VALID |
| RADIO | `Italo4you` #3, PLAY/playing |
| nastawy | volume100=12 (`volume=7/50`), MUTE OFF, balance 0 |
| Bluetooth peer | Paired=yes, Bonded=yes, Connected=no |
| bezpieczeństwo runtime | brak panic/assert/WDT/heap-corruption w obu ciągłych UART |
| firmware MAIN/SPIFFS/NVS/SECURITY | bez zmian |

## Dowody i integralność

Katalog: `/home/piotrek/voxone-hil/VOXONE-BT-AUDIO-PROBE-WWW-RETRY-1-20261102T191354+0100/`

- `SHA256SUMS`: SHA-256 `95c4d769a01b24b23b5fdec31395e51b91b2885a7cbc441e78df292ed4b39ea6`;
- ciągły L0 UART: 210 284 B, SHA-256 `ae05c66e2ca5f39594e395c4eb3c27b5123a91533e81530108da76ead555bc82`;
- ciągły BT UART: 61 516 B, SHA-256 `25d4b86cef5f8eaeb86703bcc0bbdb5838933f3fe92d20530b787afa7b18b620`;
- oba UART są bajtowo identyczne z rekonstrukcją `uart-chunks.jsonl`, a manifesty mają `unwrittenReadBytes=0`;
- `bootIdChanged=true` w manifestach jest oczekiwanym skutkiem dwóch zaprojektowanych restartów MAIN po udanych aktualizacjach BT; lista `e7582463 -> 604de4c8 -> e6d1bcd3` jest kompletna;
- status loggerów `INTERRUPTED` oznacza kontrolowane SIGINT po zakończeniu pomiaru, nie utratę danych.

## Werdykt

- same-origin WWW retry: **PASS**;
- diagnostyczne OTA i VALID: **PASS**;
- fizyczny RAW/post-volume/VU HIL: **FAIL przed PCM** z powodu jednorazowego timeoutu A2DP CONNECT;
- przyczyna historycznego `peak=0`: **UNVERIFIED**;
- produkcyjny restore `d76b139` i VALID: **PASS**;
- stan końcowy obu urządzeń: **PASS**;
- formalne `RC1-BT-LIFECYCLE` i `RC1-SOURCE-MANAGER`: bez zmiany, nadal PARTIAL.
